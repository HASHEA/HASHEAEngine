#include "Services/VegetationEditorService.h"

#include "Core/EditorCommand.h"
#include "Core/IEditorCommandExecutor.h"
#include "Core/VegetationCommands.h"
#include "Function/Asset/VegetationBaker.h"
#include "Function/Asset/VegetationCodec.h"
#include "Services/VegetationEditorTaskExecutor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <thread>
#include <utility>

namespace AshEditor
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using namespace std::chrono_literals;

		constexpr std::array<std::chrono::milliseconds, 8> k_retry_schedule{
			50ms, 100ms, 200ms, 400ms, 800ms, 1000ms, 1000ms, 1000ms
		};
		constexpr std::chrono::seconds k_operation_timeout{ 30 };

		bool is_zero_id(const AshEngine::VegetationId& id)
		{
			return std::all_of(id.begin(), id.end(),
				[](const uint8_t value) { return value == 0; });
		}

		bool is_zero_digest(const AshEngine::VegetationSha256& digest)
		{
			return std::all_of(digest.begin(), digest.end(),
				[](const uint8_t value) { return value == 0; });
		}

		bool same_surface_identity(
			const AshEngine::VegetationSurfaceIdentity& lhs,
			const AshEngine::VegetationSurfaceIdentity& rhs)
		{
			return lhs.surface_id == rhs.surface_id &&
				lhs.content_revision == rhs.content_revision &&
				lhs.residency_revision == rhs.residency_revision &&
				lhs.transform_revision == rhs.transform_revision;
		}

		bool try_surface_identity(
			const std::shared_ptr<const AshEngine::IVegetationSurfaceSnapshot>&
				snapshot,
			AshEngine::VegetationSurfaceIdentity& out_identity)
		{
			if (!snapshot)
			{
				return false;
			}
			try
			{
				out_identity = snapshot->identity();
				return true;
			}
			catch (...)
			{
				out_identity = {};
				return false;
			}
		}

		bool same_file_identity(
			const AshEngine::VegetationFileIdentity& lhs,
			const AshEngine::VegetationFileIdentity& rhs)
		{
			return lhs.available && rhs.available &&
				lhs.volume_serial_number == rhs.volume_serial_number &&
				lhs.file_index == rhs.file_index;
		}

		bool same_palette_identity(
			const std::vector<AshEngine::VegetationPaletteEntry>& lhs,
			const std::vector<AshEngine::VegetationPaletteEntry>& rhs)
		{
			if (lhs.size() != rhs.size())
			{
				return false;
			}
			for (size_t index = 0; index < lhs.size(); ++index)
			{
				if (lhs[index].species_id != rhs[index].species_id ||
					lhs[index].species_sha256 != rhs[index].species_sha256 ||
					lhs[index].species_asset_path != rhs[index].species_asset_path)
				{
					return false;
				}
			}
			return true;
		}

		bool valid_budget(const AshEngine::VegetationLoadBudget& budget)
		{
			return budget.max_file_bytes != 0 &&
				budget.max_payload_bytes != 0 &&
				budget.max_decoded_bytes != 0 &&
				budget.max_palette_records != 0 &&
				budget.max_tile_records != 0 &&
				budget.max_instance_records != 0;
		}

		bool valid_budget(const AshEngine::VegetationChunkSetLoadBudget& budget)
		{
			return valid_budget(budget.per_file) &&
				budget.max_manifest_entries != 0 &&
				budget.max_total_inspected_bytes != 0 &&
				budget.max_summary_bytes != 0;
		}

		bool valid_inspection_shape(
			const AshEngine::VegetationFileInspection& inspection)
		{
			const bool cleared_identity =
				!inspection.file_identity.available &&
				inspection.file_identity.volume_serial_number == 0 &&
				inspection.file_identity.file_index == 0;
			switch (inspection.status)
			{
			case AshEngine::VegetationFileResultStatus::Succeeded:
				return !inspection.canonical_relative_path.empty() &&
					!inspection.resolved_absolute_path.empty() &&
					!inspection.canonical_identity.empty() &&
					(!inspection.is_regular_file || inspection.exists) &&
					(inspection.exists
						? inspection.file_identity.available
						: cleared_identity);
			case AshEngine::VegetationFileResultStatus::NotFound:
			case AshEngine::VegetationFileResultStatus::InvalidPath:
			case AshEngine::VegetationFileResultStatus::LimitExceeded:
			case AshEngine::VegetationFileResultStatus::Failed:
				return inspection.canonical_relative_path.empty() &&
					inspection.resolved_absolute_path.empty() &&
					inspection.canonical_identity.empty() &&
					cleared_identity &&
					!inspection.exists &&
					!inspection.is_regular_file;
			default:
				return false;
			}
		}

		bool try_inspect_path(
			AshEngine::IVegetationFileOps* const file_ops,
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path,
			AshEngine::VegetationFileInspection& out_inspection)
		{
			if (!file_ops)
			{
				out_inspection = {};
				return false;
			}
			try
			{
				out_inspection = file_ops->InspectPath(asset_root, path);
				return true;
			}
			catch (...)
			{
				out_inspection = {};
				return false;
			}
		}

		AshEngine::VegetationLayerReadResult try_read_layer_snapshot(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path,
			const AshEngine::VegetationLoadBudget& budget,
			AshEngine::IVegetationFileOps* const file_ops)
		{
			if (!file_ops)
			{
				return {};
			}
			try
			{
				return AshEngine::read_vegetation_layer_snapshot(
					asset_root, path, budget, *file_ops);
			}
			catch (...)
			{
				AshEngine::VegetationLayerReadResult result{};
				result.error =
					"Vegetation Layer path inspection failed unexpectedly.";
				return result;
			}
		}

		std::string document_identity(const std::filesystem::path& path)
		{
			std::string value = path.generic_u8string();
			std::transform(value.begin(), value.end(), value.begin(),
				[](const unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			return value;
		}

		EditorCommandDocumentKey make_document_key(
			const std::filesystem::path& path)
		{
			return { "vegetation-layer", document_identity(path) };
		}

		AshEngine::VegetationOperationControl make_control(
			const Clock::time_point deadline)
		{
			AshEngine::VegetationOperationControl control{};
			control.cancel_requested = std::make_shared<std::atomic_bool>(false);
			control.deadline = deadline;
			return control;
		}

		VegetationOperationState operation_state(
			const AshEngine::VegetationStorageStatus status)
		{
			switch (status)
			{
			case AshEngine::VegetationStorageStatus::Succeeded:
				return VegetationOperationState::Succeeded;
			case AshEngine::VegetationStorageStatus::SourceChanged:
				return VegetationOperationState::SourceChanged;
			case AshEngine::VegetationStorageStatus::AlreadyExists:
				return VegetationOperationState::AlreadyExists;
			case AshEngine::VegetationStorageStatus::Cancelled:
				return VegetationOperationState::Cancelled;
			case AshEngine::VegetationStorageStatus::TimedOut:
				return VegetationOperationState::TimedOut;
			default:
				return VegetationOperationState::Failed;
			}
		}

		VegetationOperationState operation_state(
			const AshEngine::VegetationChunkSetCommitStatus status)
		{
			switch (status)
			{
			case AshEngine::VegetationChunkSetCommitStatus::Succeeded:
				return VegetationOperationState::Succeeded;
			case AshEngine::VegetationChunkSetCommitStatus::SourceChanged:
				return VegetationOperationState::SourceChanged;
			case AshEngine::VegetationChunkSetCommitStatus::AlreadyExists:
				return VegetationOperationState::AlreadyExists;
			case AshEngine::VegetationChunkSetCommitStatus::Cancelled:
				return VegetationOperationState::Cancelled;
			case AshEngine::VegetationChunkSetCommitStatus::TimedOut:
				return VegetationOperationState::TimedOut;
			default:
				return VegetationOperationState::Failed;
			}
		}

		VegetationOperationState operation_state(
			const AshEngine::VegetationActiveChunkSetReadStatus status)
		{
			switch (status)
			{
			case AshEngine::VegetationActiveChunkSetReadStatus::Cancelled:
				return VegetationOperationState::Cancelled;
			case AshEngine::VegetationActiveChunkSetReadStatus::TimedOut:
				return VegetationOperationState::TimedOut;
			default:
				return VegetationOperationState::Failed;
			}
		}

		VegetationOperationState operation_state(
			const AshEngine::VegetationBakeStatus status)
		{
			switch (status)
			{
			case AshEngine::VegetationBakeStatus::Cancelled:
				return VegetationOperationState::Cancelled;
			case AshEngine::VegetationBakeStatus::TimedOut:
				return VegetationOperationState::TimedOut;
			default:
				return VegetationOperationState::Failed;
			}
		}

		VegetationOperationState stopped_operation_state(
			const AshEngine::VegetationOperationControl& control)
		{
			if (control.cancel_requested &&
				control.cancel_requested->load(std::memory_order_acquire))
			{
				return VegetationOperationState::Cancelled;
			}
			if (Clock::now() >= control.deadline)
			{
				return VegetationOperationState::TimedOut;
			}
			return VegetationOperationState::Idle;
		}

		bool valid_surface_request(
			const AshEngine::VegetationSurfaceSampleRequest& request)
		{
			return std::isfinite(request.local_xz.x) &&
				std::isfinite(request.local_xz.y) &&
				request.local_xz.x >= 0.0 && request.local_xz.x < 256.0 &&
				request.local_xz.y >= 0.0 && request.local_xz.y < 256.0;
		}
	}

	struct VegetationEditorService::Impl
	{
		enum class AsyncKind : uint8_t
		{
			None = 0,
			Stroke,
			Save,
			CopyAs,
			Reload,
			Bake
		};

		struct StrokeContext
		{
			AshEngine::VegetationBrushStroke stroke{};
			AshEngine::VegetationSurfaceBinding binding{};
			std::shared_ptr<const AshEngine::IVegetationSurfaceSnapshot> surface{};
			AshEngine::VegetationSurfaceIdentity surface_identity{};
			uint64_t layer_generation = 0;
		};

		struct StrokeWorkerResult
		{
			AshEngine::VegetationOperationControl control{};
			bool succeeded = false;
			std::string error{};
		};

		struct SaveWorkerResult
		{
			AshEngine::VegetationOperationControl control{};
			std::optional<AshEngine::VegetationPreparedLayerWrite> prepared{};
		};

		struct ReloadWorkerResult
		{
			AshEngine::VegetationOperationControl control{};
			VegetationOperationState stopped =
				VegetationOperationState::Idle;
			AshEngine::VegetationLayerReadResult read{};
		};

		struct BakeWorkerResult
		{
			AshEngine::VegetationOperationControl control{};
			AshEngine::VegetationActiveChunkSetReadResult active_read{};
			AshEngine::VegetationBakeResult bake{};
			std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};
			std::string error{};
		};

		struct BakeRequestContext
		{
			AshEngine::VegetationSurfaceBinding binding{};
			std::shared_ptr<const AshEngine::VegetationLayerSnapshot> layer{};
			std::vector<std::shared_ptr<const AshEngine::VegetationSpecies>>
				species{};
			std::shared_ptr<const AshEngine::VegetationAssetResolverSnapshot>
				resolver{};
			AshEngine::VegetationAuthoringDirtyEvidence dirty{};
			std::filesystem::path source_path{};
			std::string source_identity{};
			std::optional<AshEngine::VegetationFileRevision> observed_revision{};
			std::filesystem::path source_resolved_absolute_path{};
			AshEngine::VegetationFileIdentity source_file_identity{};
		};

		explicit Impl(VegetationEditorServiceDeps dependencies)
			: deps(std::move(dependencies))
		{
		}

		bool BuildPaletteView(
			const AshEngine::VegetationLayerSnapshot& snapshot,
			std::shared_ptr<const VegetationPaletteView>& out_view,
			std::string& out_error)
		{
			VegetationPaletteView view{};
			view.reserve(snapshot.palette.size());
			for (const AshEngine::VegetationPaletteEntry& entry : snapshot.palette)
			{
				AshEngine::VegetationAssetLoadResult<AshEngine::VegetationSpecies>
					loaded = deps.pAssetDatabase->load_vegetation_species_by_path(
						entry.species_asset_path, deps.load_budget);
				if (loaded.state != AshEngine::AssetLoadState::Loaded ||
					!loaded.asset)
				{
					out_error = loaded.error.empty()
						? "Vegetation Species could not be loaded."
						: loaded.error;
					return false;
				}

				std::vector<uint8_t> canonical{};
				std::string codec_error{};
				if (!AshEngine::encode_vegetation_species(
					*loaded.asset, canonical, &codec_error))
				{
					out_error =
						"Vegetation Species canonical encode failed: " + codec_error;
					return false;
				}
				const AshEngine::VegetationSha256 digest =
					AshEngine::vegetation_sha256(
						canonical.data(), canonical.size());
				if (loaded.asset->species_id != entry.species_id ||
					digest != entry.species_sha256)
				{
					out_error =
						"Vegetation Species identity does not match the Layer palette.";
					return false;
				}
				view.push_back({
					entry.species_asset_path,
					entry.species_id,
					entry.species_sha256,
					loaded.asset->name,
					loaded.state,
					{}
				});
			}
			out_view =
				std::make_shared<const VegetationPaletteView>(std::move(view));
			return true;
		}

		bool CanStartOperation() const
		{
			return initialized && !shutting_down &&
				!cleanup_blocked &&
				async_kind == AsyncKind::None &&
				!active_stroke.has_value();
		}

		bool PublicationIsCoherent() const
		{
			return !working_set ||
				working_set->content_generation() ==
					published_view_generation;
		}

		AshEngine::VegetationSha256 ManifestDigestForLayer(
			const std::filesystem::path& layer_path,
			const AshEngine::VegetationLayerSnapshot& layer) const
		{
			AshEngine::VegetationSha256 digest{};
			try
			{
				const auto resolver =
					deps.pAssetDatabase
						? deps.pAssetDatabase->
							capture_vegetation_resolver_snapshot()
						: nullptr;
				if (!resolver || !p_file_ops)
				{
					return digest;
				}
				const AshEngine::VegetationActiveChunkSetReadResult active =
					AshEngine::read_active_vegetation_chunk_set(
						deps.asset_root,
						layer_path,
						*resolver,
						deps.chunk_set_load_budget,
						make_control(Clock::now() + k_operation_timeout),
						*p_file_ops);
				if (active.status ==
						AshEngine::VegetationActiveChunkSetReadStatus::Succeeded &&
					active.snapshot &&
					active.snapshot->layer_id == layer.layer_id)
				{
					digest = active.snapshot->manifest_sha256;
				}
			}
			catch (...)
			{
				digest.fill(0);
			}
			return digest;
		}

		bool PaletteIdentityMatches(
			const AshEngine::VegetationLayerSnapshot& snapshot) const
		{
			if (!palette_view ||
				palette_view->size() != snapshot.palette.size())
			{
				return false;
			}
			for (size_t index = 0; index < snapshot.palette.size(); ++index)
			{
				const AshEngine::VegetationPaletteEntry& entry =
					snapshot.palette[index];
				const VegetationPaletteViewEntry& view =
					(*palette_view)[index];
				if (view.species_path.generic_u8string() !=
						entry.species_asset_path ||
					view.species_id != entry.species_id ||
					view.species_digest != entry.species_sha256)
				{
					return false;
				}
			}
			return true;
		}

		bool SynchronizePalettePublication(
			const std::shared_ptr<const AshEngine::VegetationLayerSnapshot>&
				current)
		{
			if (!current)
			{
				return false;
			}
			if (!PaletteIdentityMatches(*current))
			{
				std::shared_ptr<const VegetationPaletteView> new_view{};
				std::string error{};
				if (!BuildPaletteView(*current, new_view, error))
				{
					operation = VegetationOperationState::Failed;
					detail = std::move(error);
					return false;
				}
				palette_view = std::move(new_view);
			}
			published_view_generation = current->content_generation;
			return true;
		}

		void SynchronizeWorkingSet()
		{
			if (!working_set)
			{
				return;
			}
			const uint64_t current_generation =
				working_set->content_generation();
			if (current_generation == published_view_generation)
			{
				return;
			}
			const std::shared_ptr<const AshEngine::VegetationLayerSnapshot>
				current = working_set->publish_snapshot();
			if (!current)
			{
				return;
			}
			if (session == VegetationSessionState::Clean)
			{
				if (current->content_generation != persisted_generation)
				{
					session = VegetationSessionState::Dirty;
				}
			}
			SynchronizePalettePublication(current);
		}

		VegetationEditorCapabilities CapabilitiesSnapshot() const
		{
			VegetationEditorCapabilities capabilities{};
			if (!initialized || shutting_down ||
				!PublicationIsCoherent())
			{
				return capabilities;
			}
			const bool idle = !cleanup_blocked &&
				async_kind == AsyncKind::None &&
				!active_stroke.has_value();
			const bool has_layer = working_set != nullptr;
			capabilities.can_create = idle &&
				(!has_layer || session == VegetationSessionState::Clean);
			capabilities.can_load = idle;
			capabilities.can_save = idle && has_layer &&
				(session == VegetationSessionState::Dirty ||
					session == VegetationSessionState::Clean);
			capabilities.can_save_copy_as = capabilities.can_save;
			capabilities.can_reload = idle && has_layer &&
				(session == VegetationSessionState::Clean ||
					session == VegetationSessionState::Dirty);
			capabilities.can_edit_palette = idle && has_layer &&
				session != VegetationSessionState::Saving;
			const bool surface_ready = deps.pSurfaceProvider != nullptr;
			capabilities.can_paint =
				surface_ready && capabilities.can_edit_palette;
			capabilities.can_erase = capabilities.can_paint;
			capabilities.can_bake =
				surface_ready && idle && has_layer &&
				observed_revision.has_value();
			if (!surface_ready)
			{
				capabilities.surface_unavailable_reason =
					"No vegetation surface provider is registered.";
			}
			return capabilities;
		}

		bool ApplyPaletteEdit(const AshEngine::VegetationPaletteEdit& edit)
		{
			if (!working_set || !CanStartOperation() ||
				session == VegetationSessionState::Saving)
			{
				return false;
			}
			const VegetationSessionState session_before_edit = session;
			const AshEngine::VegetationMutationResult applied =
				AshEngine::apply_vegetation_palette_edit(*working_set, edit);
			if (applied.status != AshEngine::VegetationMutationStatus::Applied)
			{
				return false;
			}

			auto command = std::make_unique<VegetationStrokeCommand>(
				make_document_key(source_path),
				working_set,
				applied.patch,
				applied.new_generation);
			const EditorCommandRecordResult recorded =
				deps.pCommandExecutor->RecordExecutedCommand(std::move(command));
			const std::shared_ptr<const AshEngine::VegetationLayerSnapshot> current =
				working_set->publish_snapshot();
			if (!current)
			{
				session = VegetationSessionState::Dirty;
				operation = VegetationOperationState::Failed;
				detail =
					"Vegetation edit did not publish a current working set.";
				return false;
			}
			switch (recorded)
			{
			case EditorCommandRecordResult::Recorded:
				session = VegetationSessionState::Dirty;
				break;
			case EditorCommandRecordResult::RolledBack:
				session = session_before_edit;
				if (session_before_edit == VegetationSessionState::Clean)
				{
					persisted_generation = current->content_generation;
				}
				break;
			case EditorCommandRecordResult::RollbackFailed:
			default:
				session = VegetationSessionState::Dirty;
				break;
			}
			if (!SynchronizePalettePublication(current))
			{
				if (recorded != EditorCommandRecordResult::RolledBack)
				{
					session = VegetationSessionState::Dirty;
				}
				return false;
			}
			if (recorded != EditorCommandRecordResult::Recorded)
			{
				operation = VegetationOperationState::Failed;
				detail = recorded == EditorCommandRecordResult::RolledBack
					? "Vegetation edit history recording failed and was rolled back."
					: "Vegetation edit history recording and rollback failed.";
				return false;
			}
			operation = VegetationOperationState::Succeeded;
			detail.clear();
			return true;
		}

		bool ScheduleStroke(const Clock::time_point now)
		{
			if (!active_stroke.has_value() || !deps.pTaskExecutor)
			{
				return false;
			}
			const std::shared_ptr<const StrokeContext> captured =
				std::make_shared<const StrokeContext>(*active_stroke);
			stroke_result = std::make_shared<StrokeWorkerResult>();
			const std::shared_ptr<StrokeWorkerResult> result = stroke_result;
			const Clock::time_point deadline = now + k_operation_timeout;
			task_id = deps.pTaskExecutor->Submit({
				deadline,
				[captured, result](
					AshEngine::VegetationOperationControl control)
				{
					result->control = control;
					for (size_t offset = 0;
						offset < captured->stroke.path.size();
						offset += 4096u)
					{
						const size_t count = std::min<size_t>(
							4096u, captured->stroke.path.size() - offset);
						std::vector<AshEngine::VegetationSurfaceSampleRequest>
							batch(
								captured->stroke.path.begin() +
									static_cast<std::ptrdiff_t>(offset),
								captured->stroke.path.begin() +
									static_cast<std::ptrdiff_t>(offset + count));
						const AshEngine::VegetationSurfaceBatchResult sampled =
							AshEngine::sample_vegetation_surface_batch(
								*captured->surface, batch, control);
						if (sampled.status !=
								AshEngine::VegetationSurfaceStatus::Ready ||
							sampled.samples.size() != count)
						{
							result->error = sampled.detail.empty()
								? "Vegetation stroke sampling failed."
								: sampled.detail;
							return;
						}
						for (const AshEngine::VegetationSurfaceSample& sample :
							sampled.samples)
						{
							if (sample.status !=
								AshEngine::VegetationSurfaceStatus::Ready)
							{
								result->error =
									"Vegetation stroke requires Ready surface samples.";
								return;
							}
						}
					}
					result->succeeded = true;
				}
			});
			if (task_id == 0)
			{
				stroke_result.reset();
				return false;
			}
			async_kind = AsyncKind::Stroke;
			operation = VegetationOperationState::Running;
			return true;
		}

		bool ScheduleSave(
			const std::filesystem::path& target,
			const bool copy_as,
			const Clock::time_point now)
		{
			if (!working_set || !CanStartOperation() || target.empty())
			{
				return false;
			}
			const std::shared_ptr<const AshEngine::VegetationLayerSnapshot> snapshot =
				working_set->publish_snapshot();
			if (!snapshot)
			{
				return false;
			}
			const uint64_t serial = operation_serial + 1;
			if (serial == 0)
			{
				return false;
			}
			const std::optional<AshEngine::VegetationFileRevision> expected =
				copy_as ? std::nullopt : observed_revision;
			const std::shared_ptr<AshEngine::VegetationOwnedStageCleanupRegistry>
				cleanup = cleanup_registry;
			AshEngine::IVegetationFileOps* const file_ops = p_file_ops;
			const std::filesystem::path root = deps.asset_root;
			save_result = std::make_shared<SaveWorkerResult>();
			const std::shared_ptr<SaveWorkerResult> result = save_result;
			const Clock::time_point deadline = now + k_operation_timeout;
			task_id = deps.pTaskExecutor->Submit({
				deadline,
				[root, target, snapshot, serial, expected, copy_as,
					cleanup, file_ops, result](
					AshEngine::VegetationOperationControl control)
				{
					result->control = control;
					if (copy_as)
					{
						result->prepared.emplace(
							AshEngine::prepare_vegetation_layer_copy_as(
								root,
								target,
								*snapshot,
								serial,
								control,
								*cleanup,
								*file_ops));
					}
					else
					{
						result->prepared.emplace(
							AshEngine::prepare_vegetation_layer_write(
								root,
								target,
								expected,
								*snapshot,
								serial,
								control,
								*cleanup,
								*file_ops));
					}
				}
			});
			if (task_id == 0)
			{
				save_result.reset();
				return false;
			}
			operation_serial = serial;
			async_kind = copy_as ? AsyncKind::CopyAs : AsyncKind::Save;
			operation = VegetationOperationState::Running;
			session_before_async = session;
			if (!copy_as)
			{
				session = VegetationSessionState::Saving;
			}
			captured_generation = snapshot->content_generation;
			operation_target = target;
			return true;
		}

		bool ScheduleReload(
			const Clock::time_point now, const bool discard_dirty)
		{
			if (!working_set || !CanStartOperation())
			{
				return false;
			}
			if (session == VegetationSessionState::Dirty && !discard_dirty)
			{
				operation = VegetationOperationState::DirtyConflict;
				detail =
					"Vegetation Layer has unsaved changes; reload requires confirmation.";
				return false;
			}
			if (session != VegetationSessionState::Clean &&
				!(discard_dirty && session == VegetationSessionState::Dirty))
			{
				return false;
			}

			const std::filesystem::path root = deps.asset_root;
			const std::filesystem::path path = source_path;
			const AshEngine::VegetationLoadBudget budget = deps.load_budget;
			AshEngine::IVegetationFileOps* const file_ops = p_file_ops;
			const uint64_t generation = working_set->content_generation();
			reload_result = std::make_shared<ReloadWorkerResult>();
			const std::shared_ptr<ReloadWorkerResult> result = reload_result;
			task_id = deps.pTaskExecutor->Submit({
				now + k_operation_timeout,
				[root, path, budget, file_ops, result](
					AshEngine::VegetationOperationControl control)
				{
					result->control = control;
					result->stopped = stopped_operation_state(control);
					if (result->stopped != VegetationOperationState::Idle)
					{
						return;
					}
					result->read = try_read_layer_snapshot(
						root, path, budget, file_ops);
					result->stopped = stopped_operation_state(control);
				}
			});
			if (task_id == 0)
			{
				reload_result.reset();
				return false;
			}
			async_kind = AsyncKind::Reload;
			operation = VegetationOperationState::Running;
			session_before_async = session;
			captured_generation = generation;
			return true;
		}

		bool BeginBakeRequest(
			const AshEngine::VegetationSurfaceBinding& binding,
			const Clock::time_point now)
		{
			const std::shared_ptr<const AshEngine::VegetationLayerSnapshot> layer =
				working_set ? working_set->publish_snapshot() : nullptr;
			const std::shared_ptr<const AshEngine::VegetationAssetResolverSnapshot>
				resolver = deps.pAssetDatabase
					? deps.pAssetDatabase->capture_vegetation_resolver_snapshot()
					: nullptr;
			if (!layer || !resolver || !observed_revision.has_value())
			{
				operation = VegetationOperationState::Failed;
				detail = "Vegetation bake immutable inputs are unavailable.";
				return false;
			}

			AshEngine::VegetationFileInspection source{};
			if (!try_inspect_path(
					p_file_ops, deps.asset_root, source_path, source) ||
				!valid_inspection_shape(source) ||
				source.status != AshEngine::VegetationFileResultStatus::Succeeded ||
				!source.exists || !source.is_regular_file ||
				source.canonical_relative_path != source_path ||
				source.canonical_identity != source_identity)
			{
				operation = VegetationOperationState::SourceChanged;
				detail = "Vegetation Layer source identity changed before bake.";
				return false;
			}

			BakeRequestContext request{};
			request.binding = binding;
			request.layer = layer;
			request.resolver = resolver;
			request.dirty = working_set->snapshot_bake_dirty_evidence();
			request.source_path = source_path;
			request.source_identity = source_identity;
			request.observed_revision = observed_revision;
			request.source_resolved_absolute_path =
				source.resolved_absolute_path;
			request.source_file_identity = source.file_identity;
			request.species.reserve(layer->palette.size());
			for (const AshEngine::VegetationPaletteEntry& entry : layer->palette)
			{
				const auto loaded = resolver->load_species_by_path(
					entry.species_asset_path, deps.load_budget);
				std::vector<uint8_t> canonical{};
				std::string codec_error{};
				if (loaded.state != AshEngine::AssetLoadState::Loaded ||
					!loaded.asset ||
					!AshEngine::encode_vegetation_species(
						*loaded.asset, canonical, &codec_error) ||
					loaded.asset->species_id != entry.species_id ||
					AshEngine::vegetation_sha256(
						canonical.data(), canonical.size()) !=
						entry.species_sha256)
				{
					operation = VegetationOperationState::Failed;
					detail = loaded.error.empty()
						? "Vegetation bake Species identity changed."
						: loaded.error;
					return false;
				}
				request.species.push_back(loaded.asset);
			}

			bake_request = std::move(request);
			bake_binding = binding;
			operation_deadline = now + k_operation_timeout;
			bake_retry_index = 0;
			next_retry_time = {};
			return CaptureAndScheduleBake(now);
		}

		void ClearBakeRequest()
		{
			bake_request.reset();
			bake_binding = {};
			operation_deadline = {};
			next_retry_time = {};
			bake_retry_index = 0;
		}

		bool CaptureAndScheduleBake(const Clock::time_point now)
		{
			if (!deps.pSurfaceProvider || !bake_request.has_value())
			{
				operation = VegetationOperationState::Failed;
				detail = deps.pSurfaceProvider
					? "Vegetation bake request context is unavailable."
					: "No vegetation surface provider is registered.";
				ClearBakeRequest();
				return false;
			}
			const AshEngine::VegetationSurfaceCaptureResult capture =
				AshEngine::capture_vegetation_surface(
					deps.pSurfaceProvider, bake_request->binding);
			if (capture.status == AshEngine::VegetationSurfaceStatus::Pending)
			{
				if (bake_retry_index >= k_retry_schedule.size() ||
					now >= operation_deadline)
				{
					async_kind = AsyncKind::None;
					operation = VegetationOperationState::TimedOut;
					detail =
						"Vegetation surface remained Pending after bounded retries.";
					ClearBakeRequest();
					return true;
				}
				next_retry_time = now + k_retry_schedule[bake_retry_index++];
				async_kind = AsyncKind::Bake;
				operation = VegetationOperationState::Pending;
				return true;
			}
			if (capture.status != AshEngine::VegetationSurfaceStatus::Ready ||
				!capture.snapshot)
			{
				async_kind = AsyncKind::None;
				operation = VegetationOperationState::Failed;
				detail = capture.detail.empty()
					? "Vegetation surface capture failed."
					: capture.detail;
				ClearBakeRequest();
				return false;
			}

			const BakeRequestContext& request = *bake_request;
			const std::shared_ptr<const AshEngine::VegetationLayerSnapshot>& layer =
				request.layer;
			const std::shared_ptr<const AshEngine::VegetationAssetResolverSnapshot>&
				resolver = request.resolver;
			std::vector<std::shared_ptr<const AshEngine::VegetationSpecies>>
				species = request.species;

			const uint64_t serial = operation_serial + 1;
			if (serial == 0)
			{
				async_kind = AsyncKind::None;
				operation = VegetationOperationState::Failed;
				detail = "Vegetation bake operation serial overflowed.";
				ClearBakeRequest();
				return false;
			}
			const AshEngine::VegetationAuthoringDirtyEvidence dirty =
				request.dirty;
			const std::shared_ptr<AshEngine::VegetationOwnedStageCleanupRegistry>
				cleanup = cleanup_registry;
			AshEngine::IVegetationFileOps* const file_ops = p_file_ops;
			const std::filesystem::path root = deps.asset_root;
			const std::filesystem::path path = request.source_path;
			const AshEngine::VegetationChunkSetLoadBudget budget =
				deps.chunk_set_load_budget;
			const std::shared_ptr<const AshEngine::IVegetationSurfaceSnapshot>
				surface = capture.snapshot;
			AshEngine::VegetationSurfaceIdentity surface_identity{};
			const bool identity_available =
				try_surface_identity(capture.snapshot, surface_identity);
			if (!identity_available ||
				is_zero_id(surface_identity.surface_id))
			{
				async_kind = AsyncKind::None;
				operation = VegetationOperationState::Failed;
				detail = identity_available
					? "Vegetation surface identity is invalid."
					: "Vegetation surface identity query failed.";
				ClearBakeRequest();
				return false;
			}
			bake_result = std::make_shared<BakeWorkerResult>();
			const std::shared_ptr<BakeWorkerResult> result = bake_result;
			task_id = deps.pTaskExecutor->Submit({
				operation_deadline,
				[root, path, layer, species = std::move(species),
					surface, surface_identity, dirty, serial, resolver,
					budget, cleanup, file_ops, result](
					AshEngine::VegetationOperationControl control)
				{
					result->control = control;
					result->active_read =
						AshEngine::read_active_vegetation_chunk_set(
							root, path, *resolver, budget, control, *file_ops);
					AshEngine::VegetationChunkSetSourceActiveIdentity source{};
					std::shared_ptr<const AshEngine::VegetationActiveChunkSetSnapshot>
						active{};
					if (result->active_read.status ==
						AshEngine::VegetationActiveChunkSetReadStatus::NoActive)
					{
						source.state =
							AshEngine::VegetationChunkSetSourceActiveState::NoActive;
					}
					else if (result->active_read.status ==
						AshEngine::VegetationActiveChunkSetReadStatus::Succeeded)
					{
						active = result->active_read.snapshot;
						if (!active)
						{
							result->error =
								"Vegetation active Chunk Set read returned no snapshot.";
							return;
						}
						source.state =
							AshEngine::VegetationChunkSetSourceActiveState::Existing;
						source.manifest_sha256 = active->manifest_sha256;
					}
					else
					{
						result->error = result->active_read.error.empty()
							? "Vegetation active Chunk Set read failed."
							: result->active_read.error;
						return;
					}

					AshEngine::VegetationBakeInput input{};
					input.operation_serial = serial;
					input.layer_snapshot = layer;
					input.species_snapshots = species;
					input.surface_snapshot = surface;
					input.source_active_identity = source;
					input.active_chunk_set = active;
					input.dirty_evidence = dirty;
					result->bake =
						AshEngine::bake_vegetation_chunks(input, control);
					if (result->bake.status !=
						AshEngine::VegetationBakeStatus::Succeeded)
					{
						result->error = result->bake.error;
						return;
					}
					result->prepared.emplace(
						AshEngine::prepare_vegetation_chunk_set(
							root,
							path,
							result->bake,
							control,
							*cleanup,
							*file_ops));
				}
			});
			if (task_id == 0)
			{
				bake_result.reset();
				async_kind = AsyncKind::None;
				operation = VegetationOperationState::Failed;
				detail = "Vegetation bake task submission failed.";
				ClearBakeRequest();
				return false;
			}
			operation_serial = serial;
			async_kind = AsyncKind::Bake;
			operation = VegetationOperationState::Running;
			captured_generation = layer->content_generation;
			captured_surface_identity = surface_identity;
			captured_dirty_generation = dirty.generation;
			return true;
		}

		bool RetryCleanup()
		{
			if (!cleanup_registry || !p_file_ops)
			{
				cleanup_blocked = false;
				return true;
			}
			const AshEngine::VegetationOwnedStageCleanupStatus cleanup =
				cleanup_registry->RetryAll(*p_file_ops);
			cleanup_blocked = !cleanup.all_removed;
			if (!cleanup.all_removed)
			{
				operation = VegetationOperationState::Failed;
				detail = "Vegetation owned stage cleanup remains incomplete.";
			}
			return cleanup.all_removed;
		}

		void ClearAsync()
		{
			task_id = 0;
			async_kind = AsyncKind::None;
			stroke_result.reset();
			save_result.reset();
			reload_result.reset();
			bake_result.reset();
			ClearBakeRequest();
			operation_target.clear();
		}

		void FinalizeAsync()
		{
			RetryCleanup();
			ClearAsync();
		}

		VegetationEditorServiceDeps deps{};
		AshEngine::IVegetationFileOps* p_file_ops = nullptr;
		std::shared_ptr<AshEngine::VegetationOwnedStageCleanupRegistry>
			cleanup_registry{};
		bool initialized = false;
		bool shutting_down = false;
		bool cleanup_blocked = false;

		std::shared_ptr<AshEngine::VegetationLayerWorkingSet> working_set{};
		std::filesystem::path source_path{};
		std::string source_identity{};
		std::optional<AshEngine::VegetationFileRevision> observed_revision{};
		uint64_t persisted_generation = 0;
		VegetationSessionState session = VegetationSessionState::Failed;
		VegetationOperationState operation = VegetationOperationState::Idle;
		std::string detail{};
		std::shared_ptr<const VegetationPaletteView> palette_view =
			std::make_shared<const VegetationPaletteView>();
		uint64_t published_view_generation = 0;
		AshEngine::VegetationSha256 active_manifest_digest{};
		AshEngine::VegetationSha256 last_known_good_manifest_digest{};

		std::optional<StrokeContext> active_stroke{};
		AshEngine::VegetationSurfaceBinding bake_binding{};
		AsyncKind async_kind = AsyncKind::None;
		uint64_t task_id = 0;
		uint64_t operation_serial = 0;
		uint64_t captured_generation = 0;
		uint64_t captured_dirty_generation = 0;
		AshEngine::VegetationSurfaceIdentity captured_surface_identity{};
		Clock::time_point operation_deadline{};
		Clock::time_point next_retry_time{};
		size_t bake_retry_index = 0;
		VegetationSessionState session_before_async =
			VegetationSessionState::Failed;
		std::filesystem::path operation_target{};
		std::shared_ptr<StrokeWorkerResult> stroke_result{};
		std::shared_ptr<SaveWorkerResult> save_result{};
		std::shared_ptr<ReloadWorkerResult> reload_result{};
		std::shared_ptr<BakeWorkerResult> bake_result{};
		std::optional<BakeRequestContext> bake_request{};
	};

	VegetationEditorService::VegetationEditorService(
		VegetationEditorServiceDeps deps)
		: m_impl(std::make_unique<Impl>(std::move(deps)))
	{
	}

	VegetationEditorService::~VegetationEditorService()
	{
		Shutdown();
	}

	AshEngine::VegetationLoadBudget
	VegetationEditorService::DefaultLoadBudget()
	{
		return {
			256ull * 1024ull * 1024ull,
			256ull * 1024ull * 1024ull,
			1024ull * 1024ull * 1024ull,
			65534u,
			262144u,
			8388608u
		};
	}

	AshEngine::VegetationChunkSetLoadBudget
	VegetationEditorService::DefaultChunkSetLoadBudget()
	{
		return {
			DefaultLoadBudget(),
			262144u,
			2ull * 1024ull * 1024ull * 1024ull,
			64ull * 1024ull * 1024ull
		};
	}

	bool VegetationEditorService::Initialize()
	{
		if (!m_impl || m_impl->initialized || m_impl->shutting_down ||
			!m_impl->deps.pAssetDatabase ||
			!m_impl->deps.pAssetDatabase->is_valid() ||
			m_impl->deps.asset_root.empty() ||
			!m_impl->deps.pCommandExecutor ||
			!m_impl->deps.pTaskExecutor ||
			!valid_budget(m_impl->deps.load_budget) ||
			!valid_budget(m_impl->deps.chunk_set_load_budget))
		{
			return false;
		}
		m_impl->p_file_ops = m_impl->deps.pFileOps
			? m_impl->deps.pFileOps
			: &AshEngine::get_default_vegetation_file_ops();
		m_impl->cleanup_registry =
			std::make_shared<AshEngine::VegetationOwnedStageCleanupRegistry>();
		m_impl->initialized = true;
		m_impl->session = VegetationSessionState::Failed;
		m_impl->operation = VegetationOperationState::Idle;
		m_impl->detail.clear();
		return true;
	}

	void VegetationEditorService::Shutdown()
	{
		if (!m_impl || !m_impl->initialized || m_impl->shutting_down)
		{
			return;
		}
		m_impl->shutting_down = true;
		std::exception_ptr exception{};
		if (m_impl->task_id != 0)
		{
			const uint64_t task_id = m_impl->task_id;
			m_impl->deps.pTaskExecutor->RequestCancel(task_id);
			while (!m_impl->deps.pTaskExecutor->IsComplete(task_id))
			{
				std::this_thread::yield();
			}
			exception = m_impl->deps.pTaskExecutor->GetException(task_id);
			m_impl->deps.pTaskExecutor->Join(task_id);
		}
		if (exception)
		{
			m_impl->operation = VegetationOperationState::Failed;
			m_impl->detail =
				"Vegetation worker task threw an exception during shutdown.";
			if (m_impl->async_kind == Impl::AsyncKind::Save)
			{
				m_impl->session = m_impl->session_before_async;
			}
		}
		m_impl->FinalizeAsync();
		m_impl->active_stroke.reset();
		m_impl->initialized = false;
	}

	void VegetationEditorService::Tick(const Clock::time_point now)
	{
		if (!m_impl || !m_impl->initialized || m_impl->shutting_down)
		{
			return;
		}
		if (m_impl->async_kind == Impl::AsyncKind::Bake &&
			m_impl->task_id == 0 &&
			m_impl->operation == VegetationOperationState::Pending)
		{
			if (now >= m_impl->operation_deadline)
			{
				m_impl->async_kind = Impl::AsyncKind::None;
				m_impl->operation = VegetationOperationState::TimedOut;
				m_impl->detail = "Vegetation bake operation deadline expired.";
				m_impl->ClearBakeRequest();
			}
			else if (now >= m_impl->next_retry_time)
			{
				m_impl->CaptureAndScheduleBake(now);
			}
			return;
		}
		if (m_impl->task_id == 0)
		{
			if (m_impl->cleanup_blocked)
			{
				m_impl->RetryCleanup();
				if (m_impl->cleanup_blocked)
				{
					return;
				}
			}
			m_impl->SynchronizeWorkingSet();
		}
		if (m_impl->task_id == 0 ||
			!m_impl->deps.pTaskExecutor->IsComplete(m_impl->task_id))
		{
			return;
		}

		const uint64_t task_id = m_impl->task_id;
		const std::exception_ptr exception =
			m_impl->deps.pTaskExecutor->GetException(task_id);
		m_impl->deps.pTaskExecutor->Join(task_id);
		if (exception)
		{
			m_impl->operation = VegetationOperationState::Failed;
			m_impl->detail = "Vegetation worker task threw an exception.";
			if (m_impl->async_kind == Impl::AsyncKind::Save)
			{
				m_impl->session = m_impl->session_before_async;
			}
			m_impl->active_stroke.reset();
			m_impl->FinalizeAsync();
			return;
		}

		switch (m_impl->async_kind)
		{
		case Impl::AsyncKind::Stroke:
		{
			const std::optional<Impl::StrokeContext>& stroke =
				m_impl->active_stroke;
			if (!stroke.has_value() || !m_impl->stroke_result ||
				!m_impl->stroke_result->succeeded ||
				!m_impl->working_set ||
				m_impl->working_set->content_generation() !=
					stroke->layer_generation)
			{
				const bool cancelled =
					m_impl->stroke_result &&
					m_impl->stroke_result->control.cancel_requested &&
					m_impl->stroke_result->control.cancel_requested->load(
						std::memory_order_acquire);
				m_impl->operation = cancelled
					? VegetationOperationState::Cancelled
					: VegetationOperationState::Failed;
				m_impl->detail = m_impl->stroke_result
					? m_impl->stroke_result->error
					: "Vegetation stroke result is unavailable.";
				m_impl->active_stroke.reset();
				m_impl->FinalizeAsync();
				return;
			}
			const AshEngine::VegetationSurfaceCaptureResult capture =
				AshEngine::capture_vegetation_surface(
					m_impl->deps.pSurfaceProvider, stroke->binding);
			AshEngine::VegetationSurfaceIdentity current_surface_identity{};
			if (capture.status != AshEngine::VegetationSurfaceStatus::Ready ||
				!capture.snapshot ||
				!try_surface_identity(
					capture.snapshot, current_surface_identity) ||
				!same_surface_identity(
					current_surface_identity, stroke->surface_identity))
			{
				m_impl->operation = VegetationOperationState::SourceChanged;
				m_impl->detail =
					"Vegetation surface identity changed before stroke commit.";
				m_impl->active_stroke.reset();
				m_impl->FinalizeAsync();
				return;
			}
			const VegetationSessionState session_before_mutation =
				m_impl->session;
			const AshEngine::VegetationMutationResult applied =
				AshEngine::apply_vegetation_brush_stroke(
					*m_impl->working_set, stroke->stroke);
			if (applied.status == AshEngine::VegetationMutationStatus::NoChange)
			{
				m_impl->operation = VegetationOperationState::Succeeded;
				m_impl->detail.clear();
			}
			else if (applied.status ==
				AshEngine::VegetationMutationStatus::Applied)
			{
				auto command = std::make_unique<VegetationStrokeCommand>(
					make_document_key(m_impl->source_path),
					m_impl->working_set,
					applied.patch,
					applied.new_generation);
				const EditorCommandRecordResult recorded =
					m_impl->deps.pCommandExecutor->RecordExecutedCommand(
						std::move(command));
				const std::shared_ptr<const AshEngine::VegetationLayerSnapshot>
					current = m_impl->working_set->publish_snapshot();
				if (!current)
				{
					m_impl->session = VegetationSessionState::Dirty;
					m_impl->operation = VegetationOperationState::Failed;
					m_impl->detail =
						"Vegetation stroke did not publish a current working set.";
				}
				else
				{
					switch (recorded)
					{
					case EditorCommandRecordResult::Recorded:
						m_impl->session = VegetationSessionState::Dirty;
						m_impl->operation =
							VegetationOperationState::Succeeded;
						m_impl->detail.clear();
						break;
					case EditorCommandRecordResult::RolledBack:
						m_impl->session = session_before_mutation;
						if (session_before_mutation ==
							VegetationSessionState::Clean)
						{
							m_impl->persisted_generation =
								current->content_generation;
						}
						m_impl->operation = VegetationOperationState::Failed;
						m_impl->detail =
							"Vegetation stroke history recording failed and "
							"was rolled back.";
						break;
					case EditorCommandRecordResult::RollbackFailed:
					default:
						m_impl->session = VegetationSessionState::Dirty;
						m_impl->operation = VegetationOperationState::Failed;
						m_impl->detail =
							"Vegetation stroke history recording and rollback "
							"failed.";
						break;
					}
					if (!m_impl->SynchronizePalettePublication(current))
					{
						if (recorded !=
							EditorCommandRecordResult::RolledBack)
						{
							m_impl->session =
								VegetationSessionState::Dirty;
						}
						m_impl->operation =
							VegetationOperationState::Failed;
					}
				}
			}
			else
			{
				m_impl->operation = VegetationOperationState::Failed;
				m_impl->detail = "Vegetation stroke mutation was rejected.";
			}
			m_impl->active_stroke.reset();
			m_impl->FinalizeAsync();
			return;
		}
		case Impl::AsyncKind::Save:
		case Impl::AsyncKind::CopyAs:
		{
			const bool copy_as =
				m_impl->async_kind == Impl::AsyncKind::CopyAs;
			if (!m_impl->save_result ||
				!m_impl->save_result->prepared.has_value())
			{
				m_impl->operation = VegetationOperationState::Failed;
				m_impl->detail = "Vegetation save prepare result is unavailable.";
				if (!copy_as)
				{
					m_impl->session = m_impl->session_before_async;
				}
			}
			else
			{
				const AshEngine::VegetationPreparedLayerWrite& prepared =
					*m_impl->save_result->prepared;
				if (prepared.status() !=
					AshEngine::VegetationStorageStatus::Prepared)
				{
					m_impl->operation = operation_state(prepared.status());
					m_impl->detail = prepared.error();
					if (!copy_as)
					{
						m_impl->session = m_impl->session_before_async;
					}
				}
				else if (!m_impl->working_set ||
					m_impl->working_set->content_generation() !=
						m_impl->captured_generation ||
					prepared.operation_serial() != m_impl->operation_serial)
				{
					m_impl->operation = VegetationOperationState::SourceChanged;
					m_impl->detail =
						"Vegetation Layer changed before save commit.";
					if (!copy_as)
					{
						m_impl->session = m_impl->session_before_async;
					}
					m_impl->cleanup_registry->CleanupStageFile(
						prepared.stage_path(), *m_impl->p_file_ops);
				}
				else
				{
					const AshEngine::VegetationStorageResult committed = copy_as
						? AshEngine::commit_vegetation_layer_copy_as(
							prepared,
							m_impl->operation_serial,
							m_impl->save_result->control,
							*m_impl->cleanup_registry,
							*m_impl->p_file_ops)
						: AshEngine::commit_vegetation_layer_write(
							prepared,
							m_impl->operation_serial,
							m_impl->save_result->control,
							*m_impl->cleanup_registry,
							*m_impl->p_file_ops);
					m_impl->operation = operation_state(committed.status);
					m_impl->detail = committed.error;
					if (committed.status ==
						AshEngine::VegetationStorageStatus::Succeeded)
					{
						if (!copy_as)
						{
							m_impl->observed_revision =
								committed.resulting_revision;
							m_impl->persisted_generation =
								m_impl->captured_generation;
							m_impl->session = VegetationSessionState::Clean;
						}
					}
					else if (!copy_as)
					{
						m_impl->session =
							committed.status ==
								AshEngine::VegetationStorageStatus::SourceChanged
							? VegetationSessionState::SourceChanged
							: m_impl->session_before_async;
					}
				}
			}
			m_impl->FinalizeAsync();
			return;
		}
		case Impl::AsyncKind::Reload:
		{
			if (m_impl->reload_result &&
				m_impl->reload_result->stopped !=
					VegetationOperationState::Idle)
			{
				m_impl->operation = m_impl->reload_result->stopped;
				m_impl->detail =
					m_impl->operation == VegetationOperationState::Cancelled
						? "Vegetation reload was cancelled."
						: "Vegetation reload timed out.";
				m_impl->FinalizeAsync();
				return;
			}
			if (!m_impl->working_set ||
				m_impl->working_set->content_generation() !=
					m_impl->captured_generation)
			{
				m_impl->operation = VegetationOperationState::SourceChanged;
				m_impl->detail =
					"Vegetation working set changed during reload.";
				m_impl->FinalizeAsync();
				return;
			}
			if (!m_impl->reload_result ||
				m_impl->reload_result->read.status !=
					AshEngine::VegetationStorageStatus::Succeeded ||
				!m_impl->reload_result->read.snapshot)
			{
				m_impl->operation = m_impl->reload_result
					? operation_state(m_impl->reload_result->read.status)
					: VegetationOperationState::Failed;
				m_impl->detail = m_impl->reload_result
					? m_impl->reload_result->read.error
					: "Vegetation reload result is unavailable.";
				m_impl->FinalizeAsync();
				return;
			}
			const AshEngine::VegetationLayerReadResult current =
				try_read_layer_snapshot(
					m_impl->deps.asset_root,
					m_impl->source_path,
					m_impl->deps.load_budget,
					m_impl->p_file_ops);
			if (current.status != AshEngine::VegetationStorageStatus::Succeeded ||
				!current.snapshot ||
				current.revision != m_impl->reload_result->read.revision ||
				current.canonical_relative_path !=
					m_impl->reload_result->read.canonical_relative_path ||
				current.canonical_identity !=
					m_impl->reload_result->read.canonical_identity)
			{
				m_impl->operation = VegetationOperationState::SourceChanged;
				m_impl->detail =
					"Vegetation Layer changed during reload.";
				m_impl->FinalizeAsync();
				return;
			}
			std::shared_ptr<const VegetationPaletteView> view{};
			std::string error{};
			if (!m_impl->BuildPaletteView(*current.snapshot, view, error))
			{
				m_impl->operation = VegetationOperationState::Failed;
				m_impl->detail = std::move(error);
				m_impl->FinalizeAsync();
				return;
			}
			const AshEngine::VegetationSha256 manifest_digest =
				m_impl->ManifestDigestForLayer(
					current.canonical_relative_path,
					*current.snapshot);
			const EditorCommandDocumentKey old_key =
				make_document_key(m_impl->source_path);
			m_impl->deps.pCommandExecutor->RemoveCommandsForDocument(old_key);
			m_impl->working_set =
				std::make_shared<AshEngine::VegetationLayerWorkingSet>(
					current.snapshot);
			m_impl->source_path = current.canonical_relative_path;
			m_impl->source_identity = current.canonical_identity;
			m_impl->observed_revision = current.revision;
			m_impl->persisted_generation =
				current.snapshot->content_generation;
			m_impl->palette_view = std::move(view);
			m_impl->published_view_generation =
				current.snapshot->content_generation;
			m_impl->active_manifest_digest = manifest_digest;
			m_impl->last_known_good_manifest_digest = manifest_digest;
			m_impl->session = VegetationSessionState::Clean;
			m_impl->operation = VegetationOperationState::Succeeded;
			m_impl->detail.clear();
			m_impl->FinalizeAsync();
			return;
		}
		case Impl::AsyncKind::Bake:
		{
			if (!m_impl->bake_result ||
				!m_impl->bake_result->prepared.has_value())
			{
				m_impl->operation = VegetationOperationState::Failed;
				if (m_impl->bake_result &&
					(m_impl->bake_result->active_read.status ==
							AshEngine::VegetationActiveChunkSetReadStatus::Cancelled ||
						m_impl->bake_result->active_read.status ==
							AshEngine::VegetationActiveChunkSetReadStatus::TimedOut))
				{
					m_impl->operation = operation_state(
						m_impl->bake_result->active_read.status);
				}
				else if (m_impl->bake_result &&
					(m_impl->bake_result->bake.status ==
							AshEngine::VegetationBakeStatus::Cancelled ||
						m_impl->bake_result->bake.status ==
							AshEngine::VegetationBakeStatus::TimedOut))
				{
					m_impl->operation = operation_state(
						m_impl->bake_result->bake.status);
				}
				m_impl->detail = m_impl->bake_result
					? m_impl->bake_result->error
					: "Vegetation bake result is unavailable.";
				m_impl->FinalizeAsync();
				return;
			}
			const AshEngine::VegetationPreparedChunkSet& prepared =
				*m_impl->bake_result->prepared;
			if (prepared.status() !=
				AshEngine::VegetationChunkSetPrepareStatus::Prepared)
			{
				switch (prepared.status())
				{
				case AshEngine::VegetationChunkSetPrepareStatus::Cancelled:
					m_impl->operation = VegetationOperationState::Cancelled;
					break;
				case AshEngine::VegetationChunkSetPrepareStatus::TimedOut:
					m_impl->operation = VegetationOperationState::TimedOut;
					break;
				default:
					m_impl->operation = VegetationOperationState::Failed;
					break;
				}
				m_impl->detail = prepared.error();
				m_impl->FinalizeAsync();
				return;
			}
			const std::shared_ptr<const AshEngine::VegetationLayerSnapshot>
				working_layer = m_impl->working_set
					? m_impl->working_set->publish_snapshot()
					: nullptr;
			const Impl::BakeRequestContext* const request =
				m_impl->bake_request
					? &*m_impl->bake_request
					: nullptr;
			const AshEngine::VegetationLayerReadResult current_layer =
				request
					? try_read_layer_snapshot(
						m_impl->deps.asset_root,
						request->source_path,
						m_impl->deps.load_budget,
						m_impl->p_file_ops)
					: AshEngine::VegetationLayerReadResult{};
			AshEngine::VegetationFileInspection current_source{};
			const bool current_source_inspected =
				request &&
				try_inspect_path(
					m_impl->p_file_ops,
					m_impl->deps.asset_root,
					request->source_path,
					current_source);
			const AshEngine::VegetationSurfaceCaptureResult capture =
				AshEngine::capture_vegetation_surface(
					m_impl->deps.pSurfaceProvider,
					request ? request->binding : m_impl->bake_binding);
			AshEngine::VegetationSurfaceIdentity current_surface_identity{};
			const bool current_surface_identity_valid =
				capture.status == AshEngine::VegetationSurfaceStatus::Ready &&
				try_surface_identity(
					capture.snapshot, current_surface_identity);
			bool stale =
				!request ||
				!request->layer ||
				prepared.expected_identity().operation_serial !=
					m_impl->operation_serial ||
				!working_layer ||
				working_layer->content_generation !=
					m_impl->captured_generation ||
				!same_palette_identity(
					working_layer->palette, request->layer->palette) ||
				current_layer.status !=
					AshEngine::VegetationStorageStatus::Succeeded ||
				!current_layer.snapshot ||
				current_layer.revision != request->observed_revision ||
				current_layer.canonical_relative_path !=
					request->source_path ||
				current_layer.canonical_identity !=
					request->source_identity ||
				!current_source_inspected ||
				!valid_inspection_shape(current_source) ||
				current_source.status !=
					AshEngine::VegetationFileResultStatus::Succeeded ||
				!current_source.exists ||
				!current_source.is_regular_file ||
				current_source.canonical_relative_path !=
					request->source_path ||
				current_source.resolved_absolute_path !=
					request->source_resolved_absolute_path ||
				current_source.canonical_identity !=
					request->source_identity ||
				!same_file_identity(
					current_source.file_identity,
					request->source_file_identity) ||
				!current_surface_identity_valid ||
				!same_surface_identity(
					current_surface_identity,
					m_impl->captured_surface_identity);
			AshEngine::VegetationChunkSetExpectedIdentity current_identity{};
			current_identity.operation_serial = m_impl->operation_serial;
			current_identity.cooker_version = 1;
			current_identity.format_version = 1;
			current_identity.surface_identity = current_surface_identity;
			if (working_layer)
			{
				current_identity.layer_id = working_layer->layer_id;
				current_identity.layer_generation =
					working_layer->content_generation;
			}
			if (m_impl->bake_result->bake.transaction.has_value())
			{
				current_identity.target_coords =
					m_impl->bake_result->bake.transaction
						->expected_identity.target_coords;
			}
			else
			{
				stale = true;
			}
			if (!stale)
			{
				const auto resolver =
					m_impl->deps.pAssetDatabase
						->capture_vegetation_resolver_snapshot();
				if (!resolver)
				{
					stale = true;
				}
				else
				{
					current_identity.species_identities.reserve(
						working_layer->palette.size());
					for (const AshEngine::VegetationPaletteEntry& entry :
						working_layer->palette)
					{
						const auto loaded = resolver->load_species_by_path(
							entry.species_asset_path,
							m_impl->deps.load_budget);
						std::vector<uint8_t> canonical{};
						std::string codec_error{};
						if (loaded.state != AshEngine::AssetLoadState::Loaded ||
							!loaded.asset ||
							!AshEngine::encode_vegetation_species(
								*loaded.asset, canonical, &codec_error) ||
							loaded.asset->species_id != entry.species_id)
						{
							stale = true;
							break;
						}
						current_identity.species_identities.push_back({
							loaded.asset->species_id,
							AshEngine::vegetation_sha256(
								canonical.data(), canonical.size())
						});
					}
				}
			}
			if (stale)
			{
				m_impl->operation = VegetationOperationState::SourceChanged;
				m_impl->detail =
					"Vegetation bake identity changed before pointer commit.";
				m_impl->cleanup_registry->CleanupStageFile(
					prepared.stage_path(), *m_impl->p_file_ops);
				m_impl->FinalizeAsync();
				return;
			}

			const AshEngine::VegetationChunkSetCommitResult committed =
				AshEngine::commit_vegetation_chunk_set(
					prepared,
					current_identity,
					m_impl->bake_result->control,
					*m_impl->cleanup_registry,
					*m_impl->p_file_ops);
			m_impl->operation = operation_state(committed.status);
			m_impl->detail = committed.error;
			if (committed.status ==
				AshEngine::VegetationChunkSetCommitStatus::Succeeded)
			{
				m_impl->active_manifest_digest = prepared.manifest_sha256();
				m_impl->last_known_good_manifest_digest =
					prepared.manifest_sha256();
				m_impl->working_set->acknowledge_bake_dirty_evidence(
					m_impl->captured_dirty_generation);
			}
			m_impl->FinalizeAsync();
			return;
		}
		case Impl::AsyncKind::None:
		default:
			m_impl->FinalizeAsync();
			return;
		}
	}

	bool VegetationEditorService::CreateLayer(
		const std::filesystem::path& layer_path, const uint64_t seed)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->CanStartOperation())
		{
			return false;
		}
		if (m_impl->working_set &&
			m_impl->session != VegetationSessionState::Clean)
		{
			if (m_impl->session == VegetationSessionState::Dirty)
			{
				m_impl->operation = VegetationOperationState::DirtyConflict;
				m_impl->detail =
					"Vegetation Layer has unsaved changes; create requires "
					"discard confirmation.";
			}
			return false;
		}
		AshEngine::VegetationFileInspection inspection{};
		if (!try_inspect_path(
				m_impl->p_file_ops,
				m_impl->deps.asset_root,
				layer_path,
				inspection) ||
			!valid_inspection_shape(inspection) ||
			inspection.status !=
				AshEngine::VegetationFileResultStatus::Succeeded ||
			inspection.exists ||
			inspection.canonical_relative_path.extension() !=
				".AshVegetationLayer")
		{
			return false;
		}

		const AshEngine::VegetationId current_id =
			m_impl->working_set && m_impl->working_set->publish_snapshot()
				? m_impl->working_set->publish_snapshot()->layer_id
				: AshEngine::VegetationId{};
		AshEngine::VegetationId id{};
		using RandomWord =
			std::independent_bits_engine<std::random_device, 64, uint64_t>;
		std::optional<RandomWord> random_word{};
		if (!m_impl->deps.create_layer_id)
		{
			random_word.emplace();
		}
		for (size_t attempt = 0; attempt < 32; ++attempt)
		{
			if (m_impl->deps.create_layer_id)
			{
				id = m_impl->deps.create_layer_id();
			}
			else
			{
				for (size_t word_index = 0; word_index < 2; ++word_index)
				{
					const uint64_t word = (*random_word)();
					for (size_t byte_index = 0; byte_index < 8; ++byte_index)
					{
						id[word_index * 8 + byte_index] =
							static_cast<uint8_t>(
								word >> (byte_index * 8));
					}
				}
			}
			if (!is_zero_id(id) && id != current_id)
			{
				break;
			}
			id.fill(0);
		}
		if (is_zero_id(id))
		{
			return false;
		}

		if (m_impl->working_set)
		{
			m_impl->deps.pCommandExecutor->RemoveCommandsForDocument(
				make_document_key(m_impl->source_path));
		}
		auto snapshot = std::make_shared<AshEngine::VegetationLayerSnapshot>();
		snapshot->layer_id = id;
		snapshot->content_generation = 1;
		snapshot->layer_seed = seed;
		m_impl->working_set =
			std::make_shared<AshEngine::VegetationLayerWorkingSet>(snapshot);
		m_impl->source_path = inspection.canonical_relative_path;
		m_impl->source_identity = inspection.canonical_identity;
		m_impl->observed_revision.reset();
		m_impl->persisted_generation = 0;
		m_impl->palette_view =
			std::make_shared<const VegetationPaletteView>();
		m_impl->published_view_generation = snapshot->content_generation;
		m_impl->session = VegetationSessionState::Dirty;
		m_impl->operation = VegetationOperationState::Succeeded;
		m_impl->detail.clear();
		m_impl->active_manifest_digest.fill(0);
		m_impl->last_known_good_manifest_digest.fill(0);
		return true;
	}

	bool VegetationEditorService::OpenLayer(
		const std::filesystem::path& layer_path)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->CanStartOperation())
		{
			return false;
		}
		if (m_impl->working_set &&
			m_impl->session != VegetationSessionState::Clean)
		{
			if (m_impl->session == VegetationSessionState::Dirty)
			{
				m_impl->operation = VegetationOperationState::DirtyConflict;
				m_impl->detail =
					"Vegetation Layer has unsaved changes; open requires "
					"discard confirmation.";
			}
			return false;
		}
		const AshEngine::VegetationLayerReadResult read =
			try_read_layer_snapshot(
				m_impl->deps.asset_root,
				layer_path,
				m_impl->deps.load_budget,
				m_impl->p_file_ops);
		if (read.status != AshEngine::VegetationStorageStatus::Succeeded ||
			!read.snapshot)
		{
			m_impl->operation = operation_state(read.status);
			m_impl->detail = read.error;
			return false;
		}
		std::shared_ptr<const VegetationPaletteView> view{};
		std::string error{};
		if (!m_impl->BuildPaletteView(*read.snapshot, view, error))
		{
			m_impl->operation = VegetationOperationState::Failed;
			m_impl->detail = std::move(error);
			return false;
		}
		const AshEngine::VegetationSha256 manifest_digest =
			m_impl->ManifestDigestForLayer(
				read.canonical_relative_path,
				*read.snapshot);
		if (m_impl->working_set)
		{
			m_impl->deps.pCommandExecutor->RemoveCommandsForDocument(
				make_document_key(m_impl->source_path));
		}
		m_impl->working_set =
			std::make_shared<AshEngine::VegetationLayerWorkingSet>(
				read.snapshot);
		m_impl->source_path = read.canonical_relative_path;
		m_impl->source_identity = read.canonical_identity;
		m_impl->observed_revision = read.revision;
		m_impl->persisted_generation =
			read.snapshot->content_generation;
		m_impl->palette_view = std::move(view);
		m_impl->published_view_generation =
			read.snapshot->content_generation;
		m_impl->active_manifest_digest = manifest_digest;
		m_impl->last_known_good_manifest_digest = manifest_digest;
		m_impl->session = VegetationSessionState::Clean;
		m_impl->operation = VegetationOperationState::Succeeded;
		m_impl->detail.clear();
		return true;
	}

	bool VegetationEditorService::AddPaletteSpecies(
		const std::filesystem::path& species_path)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->working_set ||
			!m_impl->CanStartOperation())
		{
			return false;
		}
		const auto loaded =
			m_impl->deps.pAssetDatabase->load_vegetation_species_by_path(
				species_path, m_impl->deps.load_budget);
		if (loaded.state != AshEngine::AssetLoadState::Loaded ||
			!loaded.asset)
		{
			return false;
		}
		std::vector<uint8_t> canonical{};
		std::string error{};
		if (!AshEngine::encode_vegetation_species(
			*loaded.asset, canonical, &error))
		{
			return false;
		}
		AshEngine::VegetationPaletteEdit edit{};
		edit.mode = AshEngine::VegetationPaletteEditMode::Add;
		edit.replacement.species_id = loaded.asset->species_id;
		edit.replacement.species_sha256 =
			AshEngine::vegetation_sha256(canonical.data(), canonical.size());
		const AshEngine::AssetInfo* asset =
			m_impl->deps.pAssetDatabase->find_asset_by_path(species_path);
		edit.replacement.species_asset_path = asset
			? asset->relative_path.generic_u8string()
			: species_path.generic_u8string();
		return m_impl->ApplyPaletteEdit(edit);
	}

	bool VegetationEditorService::ReplacePaletteSpecies(
		AshEngine::VegetationId target_species_id,
		const std::filesystem::path& species_path)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->working_set ||
			!m_impl->CanStartOperation())
		{
			return false;
		}
		const auto loaded =
			m_impl->deps.pAssetDatabase->load_vegetation_species_by_path(
				species_path, m_impl->deps.load_budget);
		if (loaded.state != AshEngine::AssetLoadState::Loaded ||
			!loaded.asset)
		{
			return false;
		}
		std::vector<uint8_t> canonical{};
		std::string error{};
		if (!AshEngine::encode_vegetation_species(
			*loaded.asset, canonical, &error))
		{
			return false;
		}
		AshEngine::VegetationPaletteEdit edit{};
		edit.mode = AshEngine::VegetationPaletteEditMode::Replace;
		edit.target_species_id = target_species_id;
		edit.replacement.species_id = loaded.asset->species_id;
		edit.replacement.species_sha256 =
			AshEngine::vegetation_sha256(canonical.data(), canonical.size());
		const AshEngine::AssetInfo* asset =
			m_impl->deps.pAssetDatabase->find_asset_by_path(species_path);
		edit.replacement.species_asset_path = asset
			? asset->relative_path.generic_u8string()
			: species_path.generic_u8string();
		return m_impl->ApplyPaletteEdit(edit);
	}

	bool VegetationEditorService::RemovePaletteSpecies(
		AshEngine::VegetationId target_species_id,
		const bool confirmed_clear_weights)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->working_set ||
			!m_impl->CanStartOperation())
		{
			return false;
		}
		AshEngine::VegetationPaletteEdit edit{};
		edit.mode = AshEngine::VegetationPaletteEditMode::Remove;
		edit.target_species_id = target_species_id;
		edit.clear_weights = confirmed_clear_weights;
		return m_impl->ApplyPaletteEdit(edit);
	}

	bool VegetationEditorService::BeginStroke(
		const AshEngine::VegetationBrushStroke& stroke_without_path,
		AshEngine::VegetationSurfaceBinding binding)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->working_set ||
			!m_impl->CanStartOperation() ||
			!m_impl->deps.pSurfaceProvider ||
			!stroke_without_path.path.empty() ||
			m_impl->session == VegetationSessionState::Saving)
		{
			return false;
		}
		const AshEngine::VegetationSurfaceCaptureResult capture =
			AshEngine::capture_vegetation_surface(
				m_impl->deps.pSurfaceProvider, binding);
		if (capture.status != AshEngine::VegetationSurfaceStatus::Ready ||
			!capture.snapshot)
		{
			m_impl->operation = capture.status ==
				AshEngine::VegetationSurfaceStatus::Pending
				? VegetationOperationState::Pending
				: VegetationOperationState::Failed;
			m_impl->detail = capture.detail;
			return false;
		}
		Impl::StrokeContext context{};
		context.stroke = stroke_without_path;
		context.binding = binding;
		context.surface = capture.snapshot;
		if (!try_surface_identity(
			capture.snapshot, context.surface_identity))
		{
			m_impl->operation = VegetationOperationState::Failed;
			m_impl->detail = "Vegetation surface identity query failed.";
			return false;
		}
		context.layer_generation =
			m_impl->working_set->content_generation();
		if (is_zero_id(context.surface_identity.surface_id))
		{
			return false;
		}
		m_impl->active_stroke = std::move(context);
		m_impl->operation = VegetationOperationState::Idle;
		m_impl->detail.clear();
		return true;
	}

	bool VegetationEditorService::AppendStrokePoint(
		const AshEngine::VegetationSurfaceSampleRequest& point)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->active_stroke.has_value() ||
			m_impl->async_kind != Impl::AsyncKind::None ||
			!valid_surface_request(point))
		{
			return false;
		}
		m_impl->active_stroke->stroke.path.push_back(point);
		return true;
	}

	bool VegetationEditorService::EndStroke(const Clock::time_point now)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->active_stroke.has_value() ||
			m_impl->active_stroke->stroke.path.empty() ||
			m_impl->async_kind != Impl::AsyncKind::None)
		{
			return false;
		}
		return m_impl->ScheduleStroke(now);
	}

	bool VegetationEditorService::RequestSave(const Clock::time_point now)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->working_set ||
			m_impl->source_path.empty() ||
			(m_impl->session != VegetationSessionState::Dirty &&
				m_impl->session != VegetationSessionState::Clean))
		{
			return false;
		}
		return m_impl->ScheduleSave(m_impl->source_path, false, now);
	}

	bool VegetationEditorService::RequestSaveCopyAs(
		const std::filesystem::path& destination,
		const Clock::time_point now)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		return m_impl->ScheduleSave(destination, true, now);
	}

	bool VegetationEditorService::RequestReload(const Clock::time_point now)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		return m_impl->ScheduleReload(now, false);
	}

	bool VegetationEditorService::RequestReloadDiscard(
		const bool confirmed, const Clock::time_point now)
	{
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		return confirmed && m_impl->ScheduleReload(now, true);
	}

	bool VegetationEditorService::RequestBake(
		const AshEngine::VegetationSurfaceBinding& binding,
		const Clock::time_point now)
	{
		if (binding.surface_entity_id == 0)
		{
			return false;
		}
		if (!m_impl)
		{
			return false;
		}
		m_impl->SynchronizeWorkingSet();
		if (!m_impl->CanStartOperation())
		{
			return false;
		}
		if (!m_impl->working_set ||
			!m_impl->observed_revision.has_value() ||
			!m_impl->deps.pSurfaceProvider)
		{
			if (!m_impl->deps.pSurfaceProvider)
			{
				m_impl->operation = VegetationOperationState::Failed;
				m_impl->detail =
					"No vegetation surface provider is registered.";
			}
			return false;
		}
		return m_impl->BeginBakeRequest(binding, now);
	}

	VegetationEditorStatusSnapshot
	VegetationEditorService::GetStatusSnapshot() const
	{
		VegetationEditorStatusSnapshot snapshot{};
		if (!m_impl)
		{
			snapshot.palette =
				std::make_shared<const VegetationPaletteView>();
			return snapshot;
		}
		m_impl->SynchronizeWorkingSet();
		snapshot.session = m_impl->session;
		snapshot.operation = m_impl->operation;
		snapshot.source_path = m_impl->source_path;
		snapshot.content_generation = m_impl->working_set
			? m_impl->published_view_generation
			: 0;
		snapshot.persisted_generation = m_impl->persisted_generation;
		snapshot.observed_revision = m_impl->observed_revision;
		snapshot.active_manifest_digest =
			m_impl->active_manifest_digest;
		snapshot.last_known_good_manifest_digest =
			m_impl->last_known_good_manifest_digest;
		snapshot.palette = m_impl->palette_view;
		snapshot.capabilities = m_impl->CapabilitiesSnapshot();
		snapshot.detail = m_impl->detail;
		return snapshot;
	}

	VegetationEditorCapabilities
	VegetationEditorService::GetCapabilities() const
	{
		if (!m_impl || !m_impl->initialized || m_impl->shutting_down)
		{
			return {};
		}
		m_impl->SynchronizeWorkingSet();
		return m_impl->CapabilitiesSnapshot();
	}

	std::shared_ptr<const VegetationPaletteView>
	VegetationEditorService::GetPaletteView() const
	{
		if (m_impl)
		{
			m_impl->SynchronizeWorkingSet();
		}
		return m_impl
			? m_impl->palette_view
			: std::make_shared<const VegetationPaletteView>();
	}

	VegetationOperationState
	VegetationEditorService::GetOperationState() const
	{
		if (m_impl)
		{
			m_impl->SynchronizeWorkingSet();
		}
		return m_impl
			? m_impl->operation
			: VegetationOperationState::Failed;
	}

	uint64_t VegetationEditorService::GetContentGeneration() const
	{
		if (m_impl)
		{
			m_impl->SynchronizeWorkingSet();
		}
		return m_impl && m_impl->working_set
			? m_impl->working_set->content_generation()
			: 0;
	}
}

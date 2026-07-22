#include "Function/Asset/VegetationStorage.h"

#include "Function/Asset/VegetationCodec.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace AshEngine
{
	class VegetationStorageAccess
	{
	public:
		static decltype(auto) Data(VegetationPreparedLayerWrite& prepared) noexcept
		{
			return (prepared.m_data);
		}

		static decltype(auto) Data(const VegetationPreparedLayerWrite& prepared) noexcept
		{
			return (prepared.m_data);
		}
	};

	VegetationPreparedLayerWrite::VegetationPreparedLayerWrite(
		VegetationPreparedLayerWrite&& other) noexcept
		: m_data(std::move(other.m_data))
	{
		other.m_data = Data{};
	}

	namespace
	{
		constexpr uint64_t k_max_write_block_bytes = 1024ull * 1024ull;

		enum class ControlState : uint8_t
		{
			Active = 0,
			Invalid,
			Cancelled,
			TimedOut
		};

		ControlState control_state(const VegetationOperationControl& control)
		{
			if (!control.cancel_requested ||
				control.deadline == std::chrono::steady_clock::time_point{})
			{
				return ControlState::Invalid;
			}
			if (control.cancel_requested->load(std::memory_order_acquire))
			{
				return ControlState::Cancelled;
			}
			if (std::chrono::steady_clock::now() >= control.deadline)
			{
				return ControlState::TimedOut;
			}
			return ControlState::Active;
		}

		VegetationStorageStatus storage_status(const ControlState state)
		{
			switch (state)
			{
			case ControlState::Cancelled:
				return VegetationStorageStatus::Cancelled;
			case ControlState::TimedOut:
				return VegetationStorageStatus::TimedOut;
			case ControlState::Invalid:
			default:
				return VegetationStorageStatus::Failed;
			}
		}

		bool valid_bytes_shape(
			const VegetationFileBytesResult& result,
			const uint64_t max_bytes)
		{
			switch (result.status)
			{
			case VegetationFileResultStatus::Succeeded:
				return static_cast<uint64_t>(result.bytes.size()) <= max_bytes;
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return result.bytes.empty();
			default:
				return false;
			}
		}

		bool has_layer_extension(const std::filesystem::path& path)
		{
			std::wstring extension = path.extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](const wchar_t value)
				{
					return value >= L'A' && value <= L'Z'
						? static_cast<wchar_t>(value - L'A' + L'a')
						: value;
				});
			return extension == L".ashvegetationlayer";
		}

		VegetationFileRevision revision_from_bytes(const std::vector<uint8_t>& bytes)
		{
			VegetationFileRevision revision{};
			revision.file_size = static_cast<uint64_t>(bytes.size());
			revision.sha256 = vegetation_sha256(bytes.data(), bytes.size());
			return revision;
		}

		bool valid_inspection_shape(const VegetationFileInspection& inspection)
		{
			const bool identity_is_cleared =
				!inspection.file_identity.available &&
				inspection.file_identity.volume_serial_number == 0 &&
				inspection.file_identity.file_index == 0;
			switch (inspection.status)
			{
			case VegetationFileResultStatus::Succeeded:
				return !inspection.canonical_relative_path.empty() &&
					!inspection.resolved_absolute_path.empty() &&
					!inspection.canonical_identity.empty() &&
					(!inspection.is_regular_file || inspection.exists) &&
					(inspection.exists ? inspection.file_identity.available :
						identity_is_cleared);
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return inspection.canonical_relative_path.empty() &&
					inspection.resolved_absolute_path.empty() &&
					inspection.canonical_identity.empty() &&
					identity_is_cleared &&
					!inspection.exists && !inspection.is_regular_file;
			default:
				return false;
			}
		}

		bool valid_stage_shape(const VegetationStageFileResult& stage)
		{
			const bool identity_is_cleared =
				!stage.file_identity.available &&
				stage.file_identity.volume_serial_number == 0 &&
				stage.file_identity.file_index == 0;
			switch (stage.status)
			{
			case VegetationFileResultStatus::Succeeded:
				return !stage.owned_stage_file.empty() &&
					stage.file_identity.available && stage.writer != nullptr;
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return stage.owned_stage_file.empty() &&
					identity_is_cleared && stage.writer == nullptr;
			default:
				return false;
			}
		}

		bool same_file_identity(
			const VegetationFileIdentity& lhs,
			const VegetationFileIdentity& rhs) noexcept
		{
			return lhs.available && rhs.available &&
				lhs.volume_serial_number == rhs.volume_serial_number &&
				lhs.file_index == rhs.file_index;
		}

		bool valid_lease_shape(const VegetationFileLeaseResult& lease)
		{
			switch (lease.status)
			{
			case VegetationFileLeaseStatus::Acquired:
				return lease.lease != nullptr;
			case VegetationFileLeaseStatus::Cancelled:
			case VegetationFileLeaseStatus::TimedOut:
			case VegetationFileLeaseStatus::Failed:
				return lease.lease == nullptr;
			default:
				return false;
			}
		}

		bool valid_atomic_replace_shape(const VegetationAtomicReplaceResult& replace)
		{
			switch (replace.status)
			{
			case VegetationAtomicReplaceStatus::Replaced:
			case VegetationAtomicReplaceStatus::TargetPreserved:
				return replace.recovery_path.empty();
			case VegetationAtomicReplaceStatus::RecoveryRequired:
				return !replace.recovery_path.empty() && replace.recovery_path.is_absolute() &&
					replace.recovery_path.lexically_normal() == replace.recovery_path;
			default:
				return false;
			}
		}

		VegetationPreparedLayerWrite prepare_layer(
			const VegetationPreparedLayerWriteKind kind,
			const std::filesystem::path& asset_root,
			const std::filesystem::path& target,
			std::optional<VegetationFileRevision> expected_revision,
			const VegetationLayerSnapshot& snapshot,
			const uint64_t operation_serial,
			VegetationOperationControl control,
			VegetationOwnedStageCleanupRegistry& cleanup_registry,
			IVegetationStageFileOps& file_ops)
		{
			VegetationPreparedLayerWrite result{};
			auto& prepared_data = VegetationStorageAccess::Data(result);
			prepared_data.kind = kind;
			prepared_data.expected_revision = expected_revision;
			prepared_data.operation_serial = operation_serial;

			auto fail = [&result, &prepared_data](
				const VegetationStorageStatus status, std::string error)
			{
				prepared_data.status = status;
				prepared_data.error = std::move(error);
				return std::move(result);
			};

			const ControlState initial_control = control_state(control);
			if (initial_control != ControlState::Active)
			{
				return fail(storage_status(initial_control), "Vegetation Layer preparation expired");
			}
			if (operation_serial == 0 || !has_layer_extension(target))
			{
				return fail(VegetationStorageStatus::InvalidPath,
					"Vegetation Layer target or operation serial is invalid");
			}

			std::vector<uint8_t> encoded{};
			std::string codec_error{};
			if (!encode_vegetation_layer(snapshot, encoded, &codec_error) || encoded.empty())
			{
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer encode failed: " + codec_error);
			}

			const VegetationFileInspection inspection = file_ops.InspectPath(asset_root, target);
			if (!valid_inspection_shape(inspection))
			{
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer path inspection returned an illegal result shape");
			}
			if (inspection.status == VegetationFileResultStatus::InvalidPath)
			{
				return fail(VegetationStorageStatus::InvalidPath, inspection.error);
			}
			if (inspection.status != VegetationFileResultStatus::Succeeded)
			{
				return fail(VegetationStorageStatus::Failed, inspection.error);
			}
			if (inspection.exists && !inspection.is_regular_file)
			{
				return fail(VegetationStorageStatus::InvalidPath,
					"Vegetation Layer target is not a regular file");
			}
			if (kind == VegetationPreparedLayerWriteKind::CopyAsCreateNew ||
				!expected_revision.has_value())
			{
				if (inspection.exists)
				{
					return fail(VegetationStorageStatus::AlreadyExists,
						"Vegetation Layer create-new target already exists");
				}
			}
			else if (!inspection.exists)
			{
				return fail(VegetationStorageStatus::SourceChanged,
					"Vegetation Layer source disappeared before staging");
			}

			std::error_code absolute_error{};
			prepared_data.asset_root =
				std::filesystem::absolute(asset_root, absolute_error).lexically_normal();
			if (absolute_error || prepared_data.asset_root.empty())
			{
				return fail(VegetationStorageStatus::InvalidPath,
					"Vegetation Layer asset root could not be resolved");
			}
			prepared_data.canonical_relative_path = inspection.canonical_relative_path;
			prepared_data.resolved_absolute_path = inspection.resolved_absolute_path;
			prepared_data.canonical_identity = inspection.canonical_identity;

			const VegetationFileResultStatus directory_status = file_ops.EnsureDirectoryTree(
				asset_root, inspection.canonical_relative_path.parent_path());
			if (directory_status == VegetationFileResultStatus::InvalidPath)
			{
				return fail(VegetationStorageStatus::InvalidPath,
					"Vegetation Layer parent path is invalid");
			}
			if (directory_status != VegetationFileResultStatus::Succeeded)
			{
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer parent directory creation failed");
			}

			VegetationStageFileResult stage = file_ops.CreateUniqueSiblingStageFile(
				inspection.resolved_absolute_path, operation_serial);
			if (!valid_stage_shape(stage))
			{
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer stage creation returned an illegal result shape");
			}
			if (stage.status != VegetationFileResultStatus::Succeeded)
			{
				return fail(VegetationStorageStatus::Failed,
					stage.error.empty() ? "Vegetation Layer stage creation failed" : stage.error);
			}
			prepared_data.stage_path = stage.owned_stage_file;
			if (!prepared_data.stage_path.is_absolute() ||
				prepared_data.stage_path.lexically_normal() != prepared_data.stage_path ||
				prepared_data.stage_path == prepared_data.resolved_absolute_path ||
				prepared_data.stage_path.parent_path() !=
					prepared_data.resolved_absolute_path.parent_path() ||
				same_file_identity(inspection.file_identity, stage.file_identity))
			{
				stage.writer.reset();
				prepared_data.stage_path.clear();
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer stage is not a distinct normalized target sibling");
			}

			// Re-inspect after stage creation as well as before it. This closes the
			// create-new gap where an injected provider could form a case-only or
			// hard-link alias only after the initial absent-target inspection.
			const VegetationFileInspection post_stage_inspection =
				file_ops.InspectPath(asset_root, target);
			if (!valid_inspection_shape(post_stage_inspection) ||
				post_stage_inspection.status != VegetationFileResultStatus::Succeeded)
			{
				stage.writer.reset();
				const std::filesystem::path stage_relative_path =
					(inspection.canonical_relative_path.parent_path() /
						prepared_data.stage_path.filename()).lexically_normal();
				const VegetationFileInspection stage_inspection =
					file_ops.InspectPath(asset_root, stage_relative_path);
				const bool verified_new_stage =
					valid_inspection_shape(stage_inspection) &&
					stage_inspection.status == VegetationFileResultStatus::Succeeded &&
					stage_inspection.exists && stage_inspection.is_regular_file &&
					stage_inspection.resolved_absolute_path == prepared_data.stage_path &&
					same_file_identity(stage_inspection.file_identity, stage.file_identity);
				if (verified_new_stage && cleanup_registry.TrackNewRecoveryStageFile(
					prepared_data.stage_path))
				{
					prepared_data.cleanup_registry = &cleanup_registry;
					return fail(VegetationStorageStatus::RecoveryRequired,
						"Vegetation Layer target reinspection failed; the independently verified stage is retained for recovery");
				}
				prepared_data.stage_path.clear();
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer target reinspection failed and no new exact stage ownership could be proven");
			}
			if (post_stage_inspection.exists &&
				same_file_identity(post_stage_inspection.file_identity, stage.file_identity))
			{
				stage.writer.reset();
				prepared_data.stage_path.clear();
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer stage aliases the target after creation");
			}

			const bool target_shape_changed =
				post_stage_inspection.canonical_relative_path !=
					inspection.canonical_relative_path ||
				post_stage_inspection.resolved_absolute_path !=
					inspection.resolved_absolute_path ||
				post_stage_inspection.canonical_identity != inspection.canonical_identity ||
				post_stage_inspection.exists != inspection.exists ||
				post_stage_inspection.is_regular_file != inspection.is_regular_file ||
				(inspection.exists && !same_file_identity(
					inspection.file_identity, post_stage_inspection.file_identity));
			if (target_shape_changed)
			{
				stage.writer.reset();
				if (!cleanup_registry.TrackStageFile(prepared_data.stage_path))
				{
					prepared_data.stage_path.clear();
					return fail(VegetationStorageStatus::Failed,
						"Vegetation Layer target changed and stage ownership registration failed");
				}
				prepared_data.cleanup_registry = &cleanup_registry;
				if (!cleanup_registry.CleanupStageFile(prepared_data.stage_path, file_ops))
				{
					return fail(VegetationStorageStatus::Failed,
						"Vegetation Layer target changed before writing and stage cleanup failed");
				}
				prepared_data.stage_path.clear();
				return fail(inspection.exists
						? VegetationStorageStatus::SourceChanged
						: VegetationStorageStatus::AlreadyExists,
					"Vegetation Layer target changed while creating its stage");
			}
			if (!cleanup_registry.TrackStageFile(prepared_data.stage_path))
			{
				stage.writer.reset();
				prepared_data.stage_path.clear();
				return fail(VegetationStorageStatus::Failed,
					"Vegetation Layer stage ownership registration failed");
			}
			prepared_data.cleanup_registry = &cleanup_registry;

			auto cleanup_failure = [&](const VegetationStorageStatus status, std::string error)
			{
				stage.writer.reset();
				if (!cleanup_registry.CleanupStageFile(prepared_data.stage_path, file_ops))
				{
					error += "; stage cleanup failed";
					return fail(VegetationStorageStatus::Failed, std::move(error));
				}
				prepared_data.stage_path.clear();
				return fail(status, std::move(error));
			};

			uint64_t offset = 0;
			while (offset < encoded.size())
			{
				const ControlState before_write = control_state(control);
				if (before_write != ControlState::Active)
				{
					return cleanup_failure(storage_status(before_write),
						"Vegetation Layer preparation stopped before stage write");
				}
				const uint64_t remaining = static_cast<uint64_t>(encoded.size()) - offset;
				const size_t block_size = static_cast<size_t>(
					std::min<uint64_t>(remaining, k_max_write_block_bytes));
				if (!stage.writer->WriteBlock(offset,
					{ encoded.data() + static_cast<size_t>(offset), block_size }))
				{
					return cleanup_failure(VegetationStorageStatus::Failed,
						"Vegetation Layer stage write failed");
				}
				offset += block_size;
				const ControlState after_write = control_state(control);
				if (after_write != ControlState::Active)
				{
					return cleanup_failure(storage_status(after_write),
						"Vegetation Layer preparation stopped after stage write");
				}
			}
			if (!stage.writer->FlushAndClose())
			{
				return cleanup_failure(VegetationStorageStatus::Failed,
					"Vegetation Layer stage flush failed");
			}
			stage.writer.reset();

			const VegetationFileBytesResult readback = file_ops.ReadAllBytes(
				prepared_data.stage_path, static_cast<uint64_t>(encoded.size()));
			if (!valid_bytes_shape(readback, static_cast<uint64_t>(encoded.size())) ||
				readback.status != VegetationFileResultStatus::Succeeded ||
				readback.bytes != encoded)
			{
				return cleanup_failure(VegetationStorageStatus::Failed,
					"Vegetation Layer stage readback did not match encoded bytes");
			}
			VegetationLayerSnapshot decoded{};
			VegetationLoadBudget validation_budget{};
			validation_budget.max_file_bytes = static_cast<uint64_t>(encoded.size());
			validation_budget.max_payload_bytes = static_cast<uint64_t>(encoded.size());
			validation_budget.max_decoded_bytes = std::numeric_limits<uint64_t>::max();
			validation_budget.max_palette_records = std::numeric_limits<uint32_t>::max();
			validation_budget.max_tile_records = std::numeric_limits<uint32_t>::max();
			validation_budget.max_instance_records = std::numeric_limits<uint32_t>::max();
			if (!decode_vegetation_layer(
				readback.bytes, validation_budget, decoded, &codec_error))
			{
				return cleanup_failure(VegetationStorageStatus::Failed,
					"Vegetation Layer stage strict readback failed: " + codec_error);
			}

			prepared_data.staged_revision = revision_from_bytes(readback.bytes);
			prepared_data.status = VegetationStorageStatus::Prepared;
			prepared_data.error.clear();
			return result;
		}

		VegetationStorageResult commit_layer(
			const VegetationPreparedLayerWriteKind expected_kind,
			const VegetationPreparedLayerWrite& prepared,
			const uint64_t current_operation_serial,
			VegetationOperationControl control,
			VegetationOwnedStageCleanupRegistry& cleanup_registry,
			IVegetationCommitFileOps& file_ops)
		{
			VegetationStorageResult result{};
			const auto& prepared_data = VegetationStorageAccess::Data(prepared);
			auto protect_intact_prepared_stage = [&]()
			{
				if (!cleanup_registry.OwnsStageFile(prepared.stage_path()))
				{
					return false;
				}
				const VegetationFileBytesResult recovery_bytes = file_ops.ReadAllBytes(
					prepared.stage_path(), prepared.staged_revision().file_size);
				if (!valid_bytes_shape(recovery_bytes, prepared.staged_revision().file_size) ||
					recovery_bytes.status != VegetationFileResultStatus::Succeeded ||
					revision_from_bytes(recovery_bytes.bytes) != prepared.staged_revision())
				{
					return false;
				}
				if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
				{
					return true;
				}
				return cleanup_registry.RetainStageFileForRecovery(prepared.stage_path());
			};
			auto finish_with_cleanup = [&](const VegetationStorageStatus status, std::string error)
			{
				result.status = status;
				result.error = std::move(error);
				if (!prepared.stage_path().empty() &&
					cleanup_registry.OwnsStageFile(prepared.stage_path()) &&
					!cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops))
				{
					result.status = VegetationStorageStatus::Failed;
					result.error += "; stage cleanup failed";
				}
				return result;
			};

			if (prepared.status() != VegetationStorageStatus::Prepared ||
				prepared.operation_serial() == 0 ||
				prepared.stage_path().empty() || prepared.canonical_relative_path().empty() ||
				prepared.resolved_absolute_path().empty() || prepared.canonical_identity().empty() ||
				!has_layer_extension(prepared.canonical_relative_path()))
			{
				result.status = VegetationStorageStatus::Failed;
				result.error = "Vegetation Layer prepared write identity is invalid";
				return result;
			}
			if (prepared_data.cleanup_registry != &cleanup_registry ||
				!cleanup_registry.OwnsStageFile(prepared.stage_path()))
			{
				result.status = VegetationStorageStatus::Failed;
				result.error = "Vegetation Layer prepared write cleanup ownership is invalid";
				return result;
			}
			if (prepared.kind() != expected_kind)
			{
				return finish_with_cleanup(VegetationStorageStatus::Failed,
					"Vegetation Layer prepared write kind does not match the commit API");
			}
			if (prepared.stage_path().parent_path() !=
				prepared.resolved_absolute_path().parent_path())
			{
				return finish_with_cleanup(VegetationStorageStatus::Failed,
					"Vegetation Layer stage is not a target sibling");
			}
			if (current_operation_serial != prepared.operation_serial())
			{
				return finish_with_cleanup(VegetationStorageStatus::SourceChanged,
					"Vegetation Layer operation serial changed");
			}
			ControlState state = control_state(control);
			if (state != ControlState::Active)
			{
				return finish_with_cleanup(storage_status(state),
					"Vegetation Layer commit expired before lease");
			}

			VegetationFileLeaseResult lease = file_ops.AcquireNamedLease(
				prepared.canonical_identity(), control);
			if (!valid_lease_shape(lease))
			{
				return finish_with_cleanup(VegetationStorageStatus::Failed,
					"Vegetation Layer lease returned an illegal result shape");
			}
			if (lease.status != VegetationFileLeaseStatus::Acquired)
			{
				const VegetationStorageStatus lease_status =
					lease.status == VegetationFileLeaseStatus::Cancelled
						? VegetationStorageStatus::Cancelled
						: lease.status == VegetationFileLeaseStatus::TimedOut
							? VegetationStorageStatus::TimedOut
							: VegetationStorageStatus::Failed;
				return finish_with_cleanup(lease_status,
					lease.error.empty() ? "Vegetation Layer lease acquisition failed" : lease.error);
			}
			state = control_state(control);
			if (state != ControlState::Active)
			{
				return finish_with_cleanup(storage_status(state),
					"Vegetation Layer commit expired after lease");
			}

			const VegetationFileInspection inspection = file_ops.InspectPath(
				prepared.asset_root(), prepared.canonical_relative_path());
			if (!valid_inspection_shape(inspection) ||
				inspection.status != VegetationFileResultStatus::Succeeded ||
				inspection.canonical_relative_path != prepared.canonical_relative_path() ||
				inspection.resolved_absolute_path != prepared.resolved_absolute_path() ||
				inspection.canonical_identity != prepared.canonical_identity())
			{
				return finish_with_cleanup(VegetationStorageStatus::InvalidPath,
					"Vegetation Layer target identity changed before commit");
			}

			if (prepared.expected_revision().has_value())
			{
				if (!inspection.exists || !inspection.is_regular_file)
				{
					return finish_with_cleanup(VegetationStorageStatus::SourceChanged,
						"Vegetation Layer source disappeared before commit");
				}
				const uint64_t max_source_bytes = prepared.expected_revision()->file_size;
				const VegetationFileBytesResult source = file_ops.ReadAllBytes(
					inspection.resolved_absolute_path, max_source_bytes);
				if (!valid_bytes_shape(source, max_source_bytes))
				{
					return finish_with_cleanup(VegetationStorageStatus::Failed,
						"Vegetation Layer source reread returned an illegal result shape");
				}
				if (source.status == VegetationFileResultStatus::NotFound)
				{
					return finish_with_cleanup(VegetationStorageStatus::SourceChanged,
						"Vegetation Layer source disappeared before commit");
				}
				if (source.status == VegetationFileResultStatus::LimitExceeded)
				{
					return finish_with_cleanup(VegetationStorageStatus::SourceChanged,
						"Vegetation Layer source grew before commit");
				}
				if (source.status != VegetationFileResultStatus::Succeeded)
				{
					return finish_with_cleanup(VegetationStorageStatus::Failed,
						"Vegetation Layer source reread failed");
				}
				if (revision_from_bytes(source.bytes) != *prepared.expected_revision())
				{
					return finish_with_cleanup(VegetationStorageStatus::SourceChanged,
						"Vegetation Layer source revision changed");
				}
			}
			else if (inspection.exists)
			{
				return finish_with_cleanup(VegetationStorageStatus::AlreadyExists,
					"Vegetation Layer create-new target appeared before commit");
			}

			state = control_state(control);
			if (state != ControlState::Active)
			{
				return finish_with_cleanup(storage_status(state),
					"Vegetation Layer commit expired after source validation");
			}

			const VegetationFileBytesResult staged = file_ops.ReadAllBytes(
				prepared.stage_path(), prepared.staged_revision().file_size);
			if (!valid_bytes_shape(staged, prepared.staged_revision().file_size) ||
				staged.status != VegetationFileResultStatus::Succeeded ||
				revision_from_bytes(staged.bytes) != prepared.staged_revision())
			{
				return finish_with_cleanup(VegetationStorageStatus::Failed,
					"Vegetation Layer staged bytes changed before publish");
			}
			state = control_state(control);
			if (state != ControlState::Active)
			{
				return finish_with_cleanup(storage_status(state),
					"Vegetation Layer commit expired before publish");
			}

			if (prepared.expected_revision().has_value())
			{
				const VegetationAtomicReplaceResult replaced = file_ops.AtomicReplace(
					prepared.stage_path(), prepared.resolved_absolute_path(), cleanup_registry);
				if (!valid_atomic_replace_shape(replaced))
				{
					const bool retained = protect_intact_prepared_stage();
					result.status = retained ? VegetationStorageStatus::RecoveryRequired :
						VegetationStorageStatus::Failed;
					if (retained)
					{
						result.recovery_path = prepared.stage_path();
					}
					result.error = retained ?
						"Vegetation Layer atomic replace returned an illegal result shape" :
						"Vegetation Layer atomic replace returned an illegal result shape and no recovery path could be protected";
					return result;
				}
				if (replaced.status == VegetationAtomicReplaceStatus::RecoveryRequired)
				{
					if (!cleanup_registry.OwnsStageFile(replaced.recovery_path) ||
						!cleanup_registry.IsRecoveryStageFile(replaced.recovery_path))
					{
						const bool retained = protect_intact_prepared_stage();
						result.status = retained ? VegetationStorageStatus::RecoveryRequired :
							VegetationStorageStatus::Failed;
						if (retained)
						{
							result.recovery_path = prepared.stage_path();
						}
						result.error = retained ?
							"Vegetation Layer atomic replace recovery ownership is invalid" :
							"Vegetation Layer atomic replace recovery ownership is invalid and no fallback could be protected";
					}
					else
					{
						result.status = VegetationStorageStatus::RecoveryRequired;
						result.recovery_path = replaced.recovery_path;
						result.error = replaced.error;
					}
					return result;
				}
				if (replaced.status != VegetationAtomicReplaceStatus::Replaced)
				{
					return finish_with_cleanup(VegetationStorageStatus::Failed,
						replaced.error.empty() ? "Vegetation Layer atomic replace failed" :
							replaced.error);
				}
			}
			else
			{
				const VegetationCreateNewStatus created = file_ops.CreateNewFromStage(
					prepared.stage_path(), prepared.resolved_absolute_path());
				if (created == VegetationCreateNewStatus::AlreadyExists)
				{
					return finish_with_cleanup(VegetationStorageStatus::AlreadyExists,
						"Vegetation Layer create-new target already exists");
				}
				if (created != VegetationCreateNewStatus::Created)
				{
					return finish_with_cleanup(VegetationStorageStatus::Failed,
						"Vegetation Layer create-new publish failed");
				}
			}

			// Publication consumes the stage. Exact ownership was checked before the
			// publish; forgetting is deliberately idempotent so a concurrent targeted
			// cleanup that observed the already-moved stage cannot turn a successful
			// target mutation into a reported failure.
			if (!cleanup_registry.ForgetConsumedStageFile(prepared.stage_path()))
			{
				(void)cleanup_registry.ReconcileConsumedStageFileAfterPublish(
					prepared.stage_path(), file_ops);
			}
			result.status = VegetationStorageStatus::Succeeded;
			result.resulting_revision = prepared.staged_revision();
			return result;
		}
	}

	VegetationLayerReadResult read_vegetation_layer_snapshot(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& layer_path,
		const VegetationLoadBudget& budget,
		IVegetationStageFileOps& file_ops)
	{
		VegetationLayerReadResult result{};
		if (!has_layer_extension(layer_path))
		{
			result.status = VegetationStorageStatus::InvalidPath;
			result.error = "Vegetation Layer path must use .AshVegetationLayer";
			return result;
		}
		const VegetationFileInspection inspection = file_ops.InspectPath(asset_root, layer_path);
		if (!valid_inspection_shape(inspection))
		{
			result.error = "Vegetation Layer inspection returned an illegal result shape";
			return result;
		}
		if (inspection.status == VegetationFileResultStatus::InvalidPath)
		{
			result.status = VegetationStorageStatus::InvalidPath;
			result.error = inspection.error;
			return result;
		}
		if (inspection.status != VegetationFileResultStatus::Succeeded)
		{
			result.error = inspection.error;
			return result;
		}
		if (!inspection.exists)
		{
			result.status = VegetationStorageStatus::NotFound;
			result.error = "Vegetation Layer was not found";
			return result;
		}
		if (!inspection.is_regular_file)
		{
			result.status = VegetationStorageStatus::InvalidPath;
			result.error = "Vegetation Layer path is not a regular file";
			return result;
		}

		const VegetationFileBytesResult read = file_ops.ReadAllBytes(
			inspection.resolved_absolute_path, budget.max_file_bytes);
		if (!valid_bytes_shape(read, budget.max_file_bytes))
		{
			result.error = "Vegetation Layer read returned an illegal result shape";
			return result;
		}
		if (read.status == VegetationFileResultStatus::NotFound)
		{
			result.status = VegetationStorageStatus::NotFound;
			result.error = read.error;
			return result;
		}
		if (read.status != VegetationFileResultStatus::Succeeded)
		{
			result.error = read.error;
			return result;
		}

		VegetationLayerSnapshot snapshot{};
		std::string codec_error{};
		if (!decode_vegetation_layer(read.bytes, budget, snapshot, &codec_error))
		{
			result.error = "Vegetation Layer decode failed: " + codec_error;
			return result;
		}
		result.status = VegetationStorageStatus::Succeeded;
		result.snapshot = std::make_shared<const VegetationLayerSnapshot>(std::move(snapshot));
		result.revision = revision_from_bytes(read.bytes);
		result.canonical_relative_path = inspection.canonical_relative_path;
		result.resolved_absolute_path = inspection.resolved_absolute_path;
		result.canonical_identity = inspection.canonical_identity;
		return result;
	}

	VegetationPreparedLayerWrite prepare_vegetation_layer_write(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& target,
		std::optional<VegetationFileRevision> expected_revision,
		const VegetationLayerSnapshot& snapshot,
		const uint64_t operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationStageFileOps& file_ops)
	{
		return prepare_layer(VegetationPreparedLayerWriteKind::CheckedSave,
			asset_root, target, expected_revision, snapshot, operation_serial,
			std::move(control), cleanup_registry, file_ops);
	}

	VegetationStorageResult commit_vegetation_layer_write(
		const VegetationPreparedLayerWrite& prepared,
		const uint64_t current_operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationCommitFileOps& file_ops)
	{
		return commit_layer(VegetationPreparedLayerWriteKind::CheckedSave,
			prepared, current_operation_serial, std::move(control), cleanup_registry, file_ops);
	}

	VegetationPreparedLayerWrite prepare_vegetation_layer_copy_as(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& destination,
		const VegetationLayerSnapshot& snapshot,
		const uint64_t operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationStageFileOps& file_ops)
	{
		return prepare_layer(VegetationPreparedLayerWriteKind::CopyAsCreateNew,
			asset_root, destination, std::nullopt, snapshot, operation_serial,
			std::move(control), cleanup_registry, file_ops);
	}

	VegetationStorageResult commit_vegetation_layer_copy_as(
		const VegetationPreparedLayerWrite& prepared,
		const uint64_t current_operation_serial,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationCommitFileOps& file_ops)
	{
		return commit_layer(VegetationPreparedLayerWriteKind::CopyAsCreateNew,
			prepared, current_operation_serial, std::move(control), cleanup_registry, file_ops);
	}
}

#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Asset/VegetationBaker.h"
#include "Function/Asset/VegetationChunkSet.h"
#include "Services/VegetationEditorService.h"
#include "Vegetation/VegetationTestSupport.h"

#include "doctest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using namespace std::chrono_literals;
	using Clock = std::chrono::steady_clock;

	const std::filesystem::path k_layer_path =
		"vegetation/phase2-exit.AshVegetationLayer";
	const std::filesystem::path k_other_layer_path =
		"vegetation/phase2-exit-other.AshVegetationLayer";
	const std::filesystem::path k_primary_species_path =
		"vegetation/Phase2ManualSpecies.AshVegetation";
	const std::filesystem::path k_replacement_species_path =
		"vegetation/Phase2ManualSpeciesReplacement.AshVegetation";
	const std::filesystem::path k_secondary_species_path =
		"vegetation/Phase2SecondarySpecies.AshVegetation";
	const std::filesystem::path k_filtered_species_path =
		"vegetation/Phase2FilteredSpecies.AshVegetation";
	const std::filesystem::path k_dormant_species_path =
		"vegetation/Phase2DormantSpecies.AshVegetation";

	struct SpeciesFixture
	{
		std::filesystem::path path{};
		std::vector<uint8_t> bytes{};
		std::shared_ptr<const AshEngine::VegetationSpecies> species{};
		AshEngine::VegetationPaletteEntry palette{};
	};

	SpeciesFixture MakeSpeciesFixture(
		const std::filesystem::path& path,
		const std::vector<uint8_t>& source)
	{
		AshEngine::VegetationSpecies decoded{};
		std::string error{};
		if (!AshEngine::decode_vegetation_species(
				source,
				VegetationTest::GenerousLoadBudget(),
				decoded,
				&error))
		{
			throw std::runtime_error(
				"Phase 2 exit Species did not decode: " + error);
		}
		std::vector<uint8_t> canonical{};
		if (!AshEngine::encode_vegetation_species(
				decoded, canonical, &error))
		{
			throw std::runtime_error(
				"Phase 2 exit Species did not encode: " + error);
		}
		SpeciesFixture fixture{};
		fixture.path = path;
		fixture.bytes = canonical;
		fixture.species =
			std::make_shared<const AshEngine::VegetationSpecies>(
				std::move(decoded));
		fixture.palette.species_id = fixture.species->species_id;
		fixture.palette.species_sha256 =
			AshEngine::vegetation_sha256(
				fixture.bytes.data(), fixture.bytes.size());
		fixture.palette.species_asset_path =
			path.generic_u8string();
		return fixture;
	}

	std::vector<uint8_t> SecondarySpeciesSource()
	{
		return VegetationTest::ReplaceJsonToken(
			VegetationTest::CanonicalGrassSpeciesJson(),
			"00112233445566778899aabbccddeeff",
			"10112233445566778899aabbccddeeff");
	}

	std::vector<uint8_t> FilteredSpeciesSource()
	{
		return VegetationTest::ReplaceJsonToken(
			VegetationTest::CanonicalGrassSpeciesJson(),
			"\"material_slot_min\":[0,0,0,0,0,0,0,0]",
			"\"material_slot_min\":[0,255,0,0,0,0,0,0]");
	}

	std::vector<uint8_t> DormantSpeciesSource()
	{
		return VegetationTest::ReplaceJsonToken(
			FilteredSpeciesSource(),
			"00112233445566778899aabbccddeeff",
			"20112233445566778899aabbccddeeff");
	}

	struct ExitAssets
	{
		SpeciesFixture primary = MakeSpeciesFixture(
			k_primary_species_path,
			VegetationTest::CanonicalGrassSpeciesJson());
		SpeciesFixture replacement = MakeSpeciesFixture(
			k_replacement_species_path,
			VegetationTest::ReadFixtureBytes(
				"project/src/tests/fixtures/vegetation/"
				"Phase2ManualSpeciesReplacement.AshVegetation"));
		SpeciesFixture secondary = MakeSpeciesFixture(
			k_secondary_species_path,
			SecondarySpeciesSource());
		SpeciesFixture filtered = MakeSpeciesFixture(
			k_filtered_species_path,
			FilteredSpeciesSource());
		SpeciesFixture dormant = MakeSpeciesFixture(
			k_dormant_species_path,
			DormantSpeciesSource());
	};

	AshEngine::VegetationLayerPlane MakePlane(
		const AshEngine::VegetationLayerPlaneKind kind,
		const uint8_t value,
		const AshEngine::VegetationId& species_id = {})
	{
		AshEngine::VegetationLayerPlane plane{};
		plane.kind = kind;
		plane.species_id = species_id;
		plane.values.fill(0);
		plane.values[0] = value;
		return plane;
	}

	AshEngine::VegetationLayerTile MakeTile(
		const int64_t tile_x,
		const int64_t tile_z,
		const uint8_t density,
		const std::vector<std::pair<AshEngine::VegetationId, uint8_t>>&
			weights)
	{
		AshEngine::VegetationLayerTile tile{};
		tile.tile_x = tile_x;
		tile.tile_z = tile_z;
		tile.planes.push_back(MakePlane(
			AshEngine::VegetationLayerPlaneKind::Density, density));
		for (const auto& weight : weights)
		{
			tile.planes.push_back(MakePlane(
				AshEngine::VegetationLayerPlaneKind::SpeciesWeight,
				weight.second,
				weight.first));
		}
		std::sort(
			tile.planes.begin() + 1,
			tile.planes.end(),
			[](const AshEngine::VegetationLayerPlane& lhs,
				const AshEngine::VegetationLayerPlane& rhs)
			{
				return lhs.species_id < rhs.species_id;
			});
		return tile;
	}

	AshEngine::VegetationLayerSnapshot MakeLayer(
		std::vector<AshEngine::VegetationPaletteEntry> palette,
		std::vector<AshEngine::VegetationLayerTile> tiles,
		const uint64_t generation = 1,
		const uint64_t seed = 0x0123456789abcdefull)
	{
		std::sort(
			palette.begin(),
			palette.end(),
			[](const AshEngine::VegetationPaletteEntry& lhs,
				const AshEngine::VegetationPaletteEntry& rhs)
			{
				return lhs.species_id < rhs.species_id;
			});
		std::sort(
			tiles.begin(),
			tiles.end(),
			[](const AshEngine::VegetationLayerTile& lhs,
				const AshEngine::VegetationLayerTile& rhs)
			{
				return lhs.tile_z != rhs.tile_z
					? lhs.tile_z < rhs.tile_z
					: lhs.tile_x < rhs.tile_x;
			});
		AshEngine::VegetationLayerSnapshot layer{};
		layer.layer_id = VegetationTest::SequentialId(201);
		layer.content_generation = generation;
		layer.layer_seed = seed;
		layer.palette = std::move(palette);
		layer.tiles = std::move(tiles);
		return layer;
	}

	std::vector<uint8_t> EncodeLayer(
		const AshEngine::VegetationLayerSnapshot& layer)
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(layer, bytes, &error))
		{
			throw std::runtime_error(
				"Phase 2 exit Layer did not encode: " + error);
		}
		return bytes;
	}

	class RecordingCommandExecutor final :
		public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(
			std::unique_ptr<AshEditor::EditorCommand> command) override
		{
			if (!command)
			{
				return false;
			}
			AshEditor::EditorContext context{};
			if (!command->Execute(context))
			{
				return false;
			}
			m_commands.push_back(std::move(command));
			return true;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> command) override
		{
			if (!command)
			{
				return AshEditor::EditorCommandRecordResult::RollbackFailed;
			}
			m_commands.push_back(std::move(command));
			return AshEditor::EditorCommandRecordResult::Recorded;
		}

		std::size_t RemoveCommandsForDocument(
			const AshEditor::EditorCommandDocumentKey& key) override
		{
			const size_t before = m_commands.size();
			m_commands.erase(
				std::remove_if(
					m_commands.begin(),
					m_commands.end(),
					[&key](const std::unique_ptr<AshEditor::EditorCommand>& command)
					{
						const auto document = command->GetDocumentKey();
						return document.has_value() && *document == key;
					}),
				m_commands.end());
			return before - m_commands.size();
		}

		bool Undo()
		{
			if (m_commands.empty())
			{
				return false;
			}
			AshEditor::EditorContext context{};
			return m_commands.back()->Undo(context);
		}

		bool Redo()
		{
			if (m_commands.empty())
			{
				return false;
			}
			AshEditor::EditorContext context{};
			return m_commands.back()->Execute(context);
		}

		size_t RecordedCount() const
		{
			return m_commands.size();
		}

	private:
		std::vector<std::unique_ptr<AshEditor::EditorCommand>> m_commands{};
	};

	class CancellationBarrier
	{
	public:
		void Bind(
			std::shared_ptr<const std::atomic_bool> cancel_requested)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_cancel_requested = std::move(cancel_requested);
		}

		void EnterAndWait()
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_entered = true;
			m_condition.notify_all();
			m_condition.wait(lock, [this]
			{
				return m_cancel_requested &&
					m_cancel_requested->load(std::memory_order_acquire);
			});
			m_acknowledged = true;
			m_condition.notify_all();
		}

		bool WaitUntilEntered()
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			return m_condition.wait_for(lock, 5s, [this]
			{
				return m_entered;
			});
		}

		void NotifyCancellation()
		{
			m_condition.notify_all();
		}

		bool Acknowledged() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_acknowledged;
		}

	private:
		mutable std::mutex m_mutex{};
		std::condition_variable m_condition{};
		std::shared_ptr<const std::atomic_bool> m_cancel_requested{};
		bool m_entered = false;
		bool m_acknowledged = false;
	};

	enum class ExecutorLifecycleEvent : uint8_t
	{
		Cancel = 0,
		Observe,
		Join
	};

	class CancellationNotifyingExecutor final :
		public AshEditor::IVegetationEditorTaskExecutor
	{
	public:
		explicit CancellationNotifyingExecutor(CancellationBarrier& barrier)
			: m_barrier(barrier)
		{
		}

		uint64_t Submit(
			AshEditor::VegetationEditorTaskSubmission submission) override
		{
			auto work = std::move(submission.work);
			submission.work =
				[this, work = std::move(work)](
					AshEngine::VegetationOperationControl control) mutable
				{
					m_barrier.Bind(control.cancel_requested);
					work(std::move(control));
				};
			return m_inner.Submit(std::move(submission));
		}

		void RequestCancel(const uint64_t task_id) override
		{
			m_events.push_back(ExecutorLifecycleEvent::Cancel);
			m_inner.RequestCancel(task_id);
			m_barrier.NotifyCancellation();
		}

		bool IsComplete(const uint64_t task_id) const override
		{
			return m_inner.IsComplete(task_id);
		}

		std::exception_ptr GetException(const uint64_t task_id) const override
		{
			m_events.push_back(ExecutorLifecycleEvent::Observe);
			return m_inner.GetException(task_id);
		}

		void Join(const uint64_t task_id) override
		{
			m_events.push_back(ExecutorLifecycleEvent::Join);
			m_inner.Join(task_id);
		}

		void CancelAndJoinAll() override
		{
			m_inner.CancelAndJoinAll();
		}

		bool IsIdle() const override
		{
			return m_inner.IsIdle();
		}

		const std::vector<ExecutorLifecycleEvent>& Events() const
		{
			return m_events;
		}

	private:
		CancellationBarrier& m_barrier;
		AshEditor::VegetationEditorTaskExecutor m_inner{};
		mutable std::vector<ExecutorLifecycleEvent> m_events{};
	};

	enum class ExitFileFault : uint8_t
	{
		None = 0,
		ActiveReadThrow,
		ObjectPublish,
		ManifestPublish,
		PointerCommit
	};

	class CancellingWriter final :
		public AshEngine::IVegetationStageFileWriter
	{
	public:
		CancellingWriter(
			std::unique_ptr<AshEngine::IVegetationStageFileWriter> inner,
			std::shared_ptr<std::atomic_bool> cancellation)
			: m_inner(std::move(inner))
			, m_cancellation(std::move(cancellation))
		{
		}

		bool WriteBlock(
			const uint64_t offset,
			const AshEngine::VegetationByteSpan bytes) override
		{
			const bool result =
				m_inner && m_inner->WriteBlock(offset, bytes);
			if (result && m_cancellation)
			{
				m_cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		bool FlushAndClose() override
		{
			return m_inner && m_inner->FlushAndClose();
		}

	private:
		std::unique_ptr<AshEngine::IVegetationStageFileWriter> m_inner{};
		std::shared_ptr<std::atomic_bool> m_cancellation{};
	};

	class BlockingWriter final :
		public AshEngine::IVegetationStageFileWriter
	{
	public:
		BlockingWriter(
			std::unique_ptr<AshEngine::IVegetationStageFileWriter> inner,
			CancellationBarrier& barrier)
			: m_inner(std::move(inner))
			, m_barrier(barrier)
		{
		}

		bool WriteBlock(
			const uint64_t offset,
			const AshEngine::VegetationByteSpan bytes) override
		{
			m_barrier.EnterAndWait();
			return m_inner && m_inner->WriteBlock(offset, bytes);
		}

		bool FlushAndClose() override
		{
			return m_inner && m_inner->FlushAndClose();
		}

	private:
		std::unique_ptr<AshEngine::IVegetationStageFileWriter> m_inner{};
		CancellationBarrier& m_barrier;
	};

	class ExitFileOps final : public AshEngine::IVegetationFileOps
	{
	public:
		explicit ExitFileOps(AshEngine::IVegetationFileOps& inner)
			: m_inner(inner)
		{
		}

		AshEngine::VegetationFileInspection InspectPath(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path) override
		{
			return m_inner.InspectPath(asset_root, path);
		}

		AshEngine::VegetationFileBytesResult ReadAllBytes(
			const std::filesystem::path& path,
			const uint64_t max_bytes) override
		{
			if (m_fault == ExitFileFault::ActiveReadThrow &&
				path.filename() == "active.asva")
			{
				throw std::runtime_error(
					"Injected active pointer read exception.");
			}
			return m_inner.ReadAllBytes(path, max_bytes);
		}

		AshEngine::VegetationFileResultStatus EnsureDirectoryTree(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& relative_directory) override
		{
			return m_inner.EnsureDirectoryTree(
				asset_root, relative_directory);
		}

		AshEngine::VegetationStageFileResult CreateUniqueSiblingStageFile(
			const std::filesystem::path& target,
			const uint64_t operation_serial) override
		{
			m_stage_serials.push_back(operation_serial);
			return Wrap(m_inner.CreateUniqueSiblingStageFile(
				target, operation_serial));
		}

		AshEngine::VegetationStageTreeResult CreateUniqueStageTree(
			const std::filesystem::path& store_root,
			const uint64_t operation_serial) override
		{
			m_tree_serials.push_back(operation_serial);
			return m_inner.CreateUniqueStageTree(
				store_root, operation_serial);
		}

		AshEngine::VegetationStageFileResult CreateOwnedStageFile(
			const std::filesystem::path& owned_stage_root,
			const std::filesystem::path& relative_path) override
		{
			return Wrap(m_inner.CreateOwnedStageFile(
				owned_stage_root, relative_path));
		}

		bool RemoveOwnedStageFile(
			const std::filesystem::path& stage_file,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			return m_inner.RemoveOwnedStageFile(
				stage_file, expected_identity);
		}

		bool RemoveOwnedStageTree(
			const std::filesystem::path& stage_root,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			return m_inner.RemoveOwnedStageTree(
				stage_root, expected_identity);
		}

		AshEngine::VegetationCreateNewStatus PublishImmutableFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& target) override
		{
			const std::filesystem::path parent =
				target.parent_path().filename();
			if ((m_fault == ExitFileFault::ObjectPublish &&
					parent == "objects") ||
				(m_fault == ExitFileFault::ManifestPublish &&
					parent == "manifests"))
			{
				return AshEngine::VegetationCreateNewStatus::Failed;
			}
			return m_inner.PublishImmutableFromStage(stage, target);
		}

		AshEngine::VegetationFileLeaseResult AcquireNamedLease(
			const std::string_view canonical_identity,
			const AshEngine::VegetationOperationControl& control) override
		{
			return m_inner.AcquireNamedLease(canonical_identity, control);
		}

		AshEngine::VegetationAtomicReplaceResult AtomicReplace(
			const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry) override
		{
			if (m_fault == ExitFileFault::PointerCommit &&
				target.filename() == "active.asva")
			{
				return {
					AshEngine::VegetationAtomicReplaceStatus::TargetPreserved,
					{},
					"Injected active pointer replacement failure."
				};
			}
			return m_inner.AtomicReplace(stage, target, registry);
		}

		AshEngine::VegetationCreateNewStatus CreateNewFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& target) override
		{
			if (m_fault == ExitFileFault::PointerCommit &&
				target.filename() == "active.asva")
			{
				return AshEngine::VegetationCreateNewStatus::Failed;
			}
			return m_inner.CreateNewFromStage(stage, target);
		}

		void SetFault(const ExitFileFault fault)
		{
			m_fault = fault;
		}

		void CancelAfterWrite(
			std::shared_ptr<std::atomic_bool> cancellation)
		{
			m_cancel_after_write = std::move(cancellation);
		}

		void BlockDuringWrite(CancellationBarrier& barrier)
		{
			m_write_barrier = &barrier;
		}

		const std::vector<uint64_t>& StageSerials() const
		{
			return m_stage_serials;
		}

		const std::vector<uint64_t>& TreeSerials() const
		{
			return m_tree_serials;
		}

	private:
		AshEngine::VegetationStageFileResult Wrap(
			AshEngine::VegetationStageFileResult result)
		{
			if (result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded &&
				result.writer && m_write_barrier)
			{
				result.writer = std::make_unique<BlockingWriter>(
					std::move(result.writer), *m_write_barrier);
			}
			else if (result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded &&
				result.writer && m_cancel_after_write)
			{
				result.writer = std::make_unique<CancellingWriter>(
					std::move(result.writer), m_cancel_after_write);
			}
			return result;
		}

		AshEngine::IVegetationFileOps& m_inner;
		ExitFileFault m_fault = ExitFileFault::None;
		CancellationBarrier* m_write_barrier = nullptr;
		std::shared_ptr<std::atomic_bool> m_cancel_after_write{};
		std::vector<uint64_t> m_stage_serials{};
		std::vector<uint64_t> m_tree_serials{};
	};

	struct ExitFixture
	{
		ExitFixture(
			const std::string& label,
			const AshEngine::VegetationLayerSnapshot& layer,
			const AshEngine::IVegetationSurfaceProvider* provider,
			AshEngine::IVegetationFileOps* file_ops = nullptr)
			: root(label)
		{
			root.Write(assets.primary.path, assets.primary.bytes);
			root.Write(assets.replacement.path, assets.replacement.bytes);
			root.Write(assets.secondary.path, assets.secondary.bytes);
			root.Write(assets.filtered.path, assets.filtered.bytes);
			root.Write(assets.dormant.path, assets.dormant.bytes);
			root.Write(k_layer_path, EncodeLayer(layer));
			database = AshEngine::AssetDatabase::create(root.Path());
			deps.pAssetDatabase = &database;
			deps.asset_root = root.Path();
			deps.pCommandExecutor = &commands;
			deps.pSurfaceProvider = provider;
			deps.pTaskExecutor = &executor;
			deps.load_budget = VegetationTest::GenerousLoadBudget();
			deps.chunk_set_load_budget =
				AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget();
			deps.create_layer_id = []
			{
				return VegetationTest::SequentialId(231);
			};
			deps.pFileOps = file_ops;
		}

		ExitAssets assets{};
		VegetationTest::ScopedAssetRoot root;
		AshEngine::AssetDatabase database{};
		VegetationTest::ManualVegetationEditorTaskExecutor executor{};
		RecordingCommandExecutor commands{};
		AshEditor::VegetationEditorServiceDeps deps{};
	};

	bool HasOperationStages(const std::filesystem::path& root)
	{
		std::error_code error{};
		for (std::filesystem::recursive_directory_iterator iterator(
				root,
				std::filesystem::directory_options::skip_permission_denied,
				error),
			end;
			iterator != end && !error;
			iterator.increment(error))
		{
			const std::wstring name =
				iterator->path().filename().wstring();
			if (name.rfind(L".ashveg-layer-stage-", 0) == 0 ||
				name.rfind(L".ashveg-stage-tree-", 0) == 0)
			{
				return true;
			}
		}
		return false;
	}

	std::filesystem::path ActivePath(
		const VegetationTest::ScopedAssetRoot& root)
	{
		return root.Path() /
			(k_layer_path.generic_u8string() +
				".AshVegetationChunks/active.asva");
	}

	struct BakeArtifacts
	{
		bool published = false;
		AshEngine::VegetationSha256 manifest_digest{};
		std::vector<uint8_t> active_pointer_bytes{};
		std::vector<uint8_t> manifest_bytes{};
		std::vector<std::vector<uint8_t>> chunk_bytes{};
		AshEngine::VegetationChunkSetManifest manifest{};
	};

	std::vector<uint8_t> ReadArtifactBytes(
		const std::filesystem::path& path,
		const char* const artifact)
	{
		const AshEngine::VegetationFileBytesResult result =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				path, 64ull * 1024ull * 1024ull);
		if (result.status !=
			AshEngine::VegetationFileResultStatus::Succeeded)
		{
			throw std::runtime_error(
				std::string("Failed to read Phase 2 exit ") + artifact +
				" at '" + path.generic_u8string() + "': " + result.error);
		}
		return result.bytes;
	}

	BakeArtifacts ReadBakeArtifacts(
		const VegetationTest::ScopedAssetRoot& root)
	{
		BakeArtifacts artifacts{};
		const std::filesystem::path active_path = ActivePath(root);
		if (!std::filesystem::is_regular_file(active_path))
		{
			return artifacts;
		}
		artifacts.active_pointer_bytes =
			ReadArtifactBytes(active_path, "active pointer");
		AshEngine::VegetationChunkSetActivePointer pointer{};
		std::string error{};
		if (!AshEngine::decode_vegetation_chunk_set_active_pointer(
				artifacts.active_pointer_bytes, pointer, &error))
		{
			throw std::runtime_error(
				"Phase 2 exit active pointer did not decode: " + error);
		}
		artifacts.manifest_digest = pointer.manifest_sha256;
		const std::filesystem::path store =
			root.Path() /
			(k_layer_path.generic_u8string() +
				".AshVegetationChunks");
		const std::filesystem::path manifest_path =
			store / "manifests" /
			(VegetationTest::ToHex(pointer.manifest_sha256) + ".asvm");
		artifacts.manifest_bytes =
			ReadArtifactBytes(manifest_path, "manifest");
		if (!AshEngine::decode_vegetation_chunk_set_manifest(
				artifacts.manifest_bytes,
				262144u,
				artifacts.manifest,
				&error))
		{
			throw std::runtime_error(
				"Phase 2 exit manifest did not decode: " + error);
		}
		for (const AshEngine::VegetationChunkSetManifestEntry& entry :
			artifacts.manifest.entries)
		{
			artifacts.chunk_bytes.push_back(
				ReadArtifactBytes(
					store / "objects" /
					(VegetationTest::ToHex(entry.object_sha256) +
						".AshVegetationChunk"),
					"chunk object"));
		}
		artifacts.published = true;
		return artifacts;
	}

	void CompleteQueuedOperation(
		AshEditor::VegetationEditorService& service,
		VegetationTest::ManualVegetationEditorTaskExecutor& executor,
		const Clock::time_point now)
	{
		REQUIRE(executor.PendingCount() == 1);
		REQUIRE(executor.RunNext());
		CHECK(executor.IsIdle());
		service.Tick(now);
		CHECK(executor.TaskRecordCount() == 0);
		CHECK(executor.ScheduledOrderCount() == 0);
	}

	void CompleteSave(
		AshEditor::VegetationEditorService& service,
		VegetationTest::ManualVegetationEditorTaskExecutor& executor,
		const Clock::time_point now)
	{
		REQUIRE(service.RequestSave(now));
		CompleteQueuedOperation(service, executor, now);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
	}

	void CompleteCleanReload(
		AshEditor::VegetationEditorService& service,
		VegetationTest::ManualVegetationEditorTaskExecutor& executor,
		const Clock::time_point now)
	{
		REQUIRE(service.RequestReload(now));
		CompleteQueuedOperation(service, executor, now);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
	}

	void ExecuteStroke(
		AshEditor::VegetationEditorService& service,
		VegetationTest::ManualVegetationEditorTaskExecutor& executor,
		VegetationTest::DeterministicSurfaceProvider& provider,
		const AshEngine::VegetationSurfaceBinding binding,
		const AshEngine::VegetationBrushMode mode,
		const uint8_t strength,
		const AshEngine::VegetationSurfaceSampleRequest& point,
		const Clock::time_point now)
	{
		provider.SetMode(VegetationTest::DeterministicSurfaceMode::Ready);
		const auto palette = service.GetPaletteView();
		REQUIRE(palette);
		REQUIRE_FALSE(palette->empty());
		AshEngine::VegetationBrushStroke stroke{};
		stroke.mode = mode;
		stroke.selected_species = palette->front().species_id;
		stroke.radius_mm = 1000;
		stroke.strength = strength;
		stroke.falloff = 0;
		stroke.spacing_mm = 500;
		REQUIRE(service.BeginStroke(stroke, binding));
		REQUIRE(service.AppendStrokePoint(point));
		REQUIRE(service.EndStroke(now));
		CompleteQueuedOperation(service, executor, now);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
	}

	BakeArtifacts CompleteBake(
		AshEditor::VegetationEditorService& service,
		VegetationTest::ManualVegetationEditorTaskExecutor& executor,
		VegetationTest::DeterministicSurfaceProvider& provider,
		const AshEngine::VegetationSurfaceBinding binding,
		const Clock::time_point now,
		const VegetationTest::ScopedAssetRoot& root)
	{
		REQUIRE(binding.surface_entity_id != 0);
		REQUIRE(service.RequestBake(binding, now));
		CompleteQueuedOperation(service, executor, now);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		BakeArtifacts artifacts = ReadBakeArtifacts(root);
		REQUIRE(artifacts.published);
		return artifacts;
	}

	AshEngine::VegetationLayerSnapshot SingleSpeciesLayer(
		const ExitAssets& assets,
		const uint8_t density = 255,
		const int64_t tile_x = 0,
		const uint64_t generation = 1,
		const uint64_t seed = 0x0123456789abcdefull)
	{
		return MakeLayer(
			{ assets.primary.palette },
			{ MakeTile(
				tile_x,
				0,
				density,
				{ { assets.primary.palette.species_id, 255 } }) },
			generation,
			seed);
	}

	bool SeedProducesInstance(
		const AshEngine::VegetationLayerSnapshot& layer,
		const AshEngine::VegetationSpecies& species,
		const uint64_t seed)
	{
		for (uint16_t candidate = 0;
			candidate < species.placement.candidates_per_cell;
			++candidate)
		{
			AshEngine::VegetationCounterHashKey key{};
			key.layer_id = layer.layer_id;
			key.chunk = { 0, 0 };
			key.cell_x = 0;
			key.cell_z = 0;
			key.species_id = species.species_id;
			key.layer_seed = seed;
			key.candidate_ordinal = candidate;
			const AshEngine::VegetationCounterHashResult hash =
				AshEngine::make_vegetation_counter_hash(key, 1);
			const uint16_t acceptance =
				static_cast<uint16_t>(hash.random[0] >> 48u);
			if (acceptance <
				AshEngine::vegetation_candidate_accept_limit(1))
			{
				return true;
			}
		}
		return false;
	}

	std::pair<uint64_t, uint64_t> FindAbsentAndPresentSeeds(
		const AshEngine::VegetationLayerSnapshot& layer,
		const AshEngine::VegetationSpecies& species)
	{
		std::optional<uint64_t> absent{};
		std::optional<uint64_t> present{};
		for (uint64_t seed = 1;
			seed < 1000000 && (!absent.has_value() || !present.has_value());
			++seed)
		{
			if (SeedProducesInstance(layer, species, seed))
			{
				present = present.value_or(seed);
			}
			else
			{
				absent = absent.value_or(seed);
			}
		}
		if (!absent.has_value() || !present.has_value())
		{
			throw std::runtime_error(
				"Phase 2 exit could not find deterministic seed transitions.");
		}
		return { *absent, *present };
	}

	bool BatchContainsChunk(
		const VegetationTest::DeterministicSurfaceSnapshot& snapshot,
		const AshEngine::VegetationChunkCoord coord)
	{
		for (const auto& batch : snapshot.Batches())
		{
			if (std::any_of(
					batch.begin(),
					batch.end(),
					[coord](
						const AshEngine::VegetationSurfaceSampleRequest& request)
					{
						return request.chunk.x == coord.x &&
							request.chunk.z == coord.z;
					}))
			{
				return true;
			}
		}
		return false;
	}

	void CopyChunkStore(
		const VegetationTest::ScopedAssetRoot& root,
		const std::filesystem::path& source_layer,
		const std::filesystem::path& destination_layer)
	{
		const std::filesystem::path source =
			root.Path() /
			(source_layer.generic_u8string() + ".AshVegetationChunks");
		const std::filesystem::path destination =
			root.Path() /
			(destination_layer.generic_u8string() + ".AshVegetationChunks");
		const auto extended_length_path =
			[](const std::filesystem::path& path)
			{
				const std::wstring absolute =
					std::filesystem::absolute(path).wstring();
				return std::filesystem::path(L"\\\\?\\" + absolute);
			};
		const std::filesystem::path extended_source =
			extended_length_path(source);
		const std::filesystem::path extended_destination =
			extended_length_path(destination);
		std::error_code error{};
		std::filesystem::create_directories(
			extended_destination.parent_path(), error);
		if (error)
		{
			throw std::runtime_error(
				"Phase 2 exit could not create copied store parent: " +
				error.message());
		}
		std::filesystem::copy(
			extended_source,
			extended_destination,
			std::filesystem::copy_options::recursive,
			error);
		if (error)
		{
			throw std::runtime_error(
				"Phase 2 exit could not copy active chunk store: " +
				error.message());
		}
	}
}

TEST_CASE("Vegetation Phase 2 authoring and deterministic bake exit contract")
{
	SUBCASE("full authoring save reload deterministic bake and last known good chain")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer = MakeLayer(
			{ assets.primary.palette, assets.secondary.palette },
			{ MakeTile(
				0,
				0,
				254,
				{
					{ assets.primary.palette.species_id, 255 },
					{ assets.secondary.palette.species_id, 255 }
				}) });
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(181, 1, 2, 3));
		ExitFixture fixture("phase2-exit-full-chain", layer, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));

		REQUIRE(service.ReplacePaletteSpecies(
			assets.primary.palette.species_id,
			k_replacement_species_path));
		REQUIRE(service.RemovePaletteSpecies(
			assets.secondary.palette.species_id, true));
		REQUIRE(fixture.commands.Undo());
		REQUIRE(fixture.commands.Redo());

		const AshEngine::VegetationSurfaceBinding binding{ 42 };
		const Clock::time_point t0 = Clock::now();
		ExecuteStroke(
			service,
			fixture.executor,
			provider,
			binding,
			AshEngine::VegetationBrushMode::Paint,
			8,
			VegetationTest::SurfaceRequest(0.5, 0.5),
			t0);
		ExecuteStroke(
			service,
			fixture.executor,
			provider,
			binding,
			AshEngine::VegetationBrushMode::Erase,
			8,
			VegetationTest::SurfaceRequest(0.5, 0.5),
			t0 + 1ms);
		CHECK(fixture.commands.RecordedCount() == 4);
		REQUIRE(fixture.commands.Undo());
		REQUIRE(fixture.commands.Redo());

		const AshEditor::VegetationEditorStatusSnapshot dirty_before_save =
			service.GetStatusSnapshot();
		REQUIRE(dirty_before_save.session ==
			AshEditor::VegetationSessionState::Dirty);
		REQUIRE(service.RequestSave(t0 + 2ms));
		const AshEditor::VegetationEditorStatusSnapshot before_save_task =
			service.GetStatusSnapshot();
		CHECK(before_save_task.session ==
			AshEditor::VegetationSessionState::Saving);
		CHECK(before_save_task.persisted_generation ==
			dirty_before_save.persisted_generation);
		CHECK(before_save_task.observed_revision ==
			dirty_before_save.observed_revision);
		REQUIRE(fixture.executor.RunNext());
		const AshEditor::VegetationEditorStatusSnapshot worker_completed =
			service.GetStatusSnapshot();
		CHECK(worker_completed.session ==
			AshEditor::VegetationSessionState::Saving);
		CHECK(worker_completed.persisted_generation ==
			before_save_task.persisted_generation);
		CHECK(worker_completed.observed_revision ==
			before_save_task.observed_revision);
		service.Tick(t0 + 2ms);
		CHECK(fixture.executor.TaskRecordCount() == 0);
		const AshEditor::VegetationEditorStatusSnapshot saved =
			service.GetStatusSnapshot();
		CHECK(saved.session == AshEditor::VegetationSessionState::Clean);
		CHECK(saved.operation ==
			AshEditor::VegetationOperationState::Succeeded);
		CHECK(saved.source_path == k_layer_path);
		CHECK(saved.persisted_generation == saved.content_generation);
		CHECK(saved.observed_revision.has_value());
		CHECK(saved.observed_revision !=
			before_save_task.observed_revision);
		CHECK(saved.active_manifest_digest ==
			AshEngine::VegetationSha256{});
		CHECK(saved.last_known_good_manifest_digest ==
			AshEngine::VegetationSha256{});
		CHECK(saved.capabilities.can_bake);
		REQUIRE(saved.palette);
		REQUIRE(saved.palette->size() == 1);
		CompleteCleanReload(service, fixture.executor, t0 + 3ms);
		const AshEditor::VegetationEditorStatusSnapshot reloaded =
			service.GetStatusSnapshot();
		REQUIRE(reloaded.palette);
		CHECK(reloaded.palette->size() == 1);
		CHECK(reloaded.palette->front().species_id ==
			assets.primary.palette.species_id);

		const BakeArtifacts first = CompleteBake(
			service,
			fixture.executor,
			provider,
			binding,
			t0 + 4ms,
			fixture.root);
		REQUIRE(std::filesystem::remove(ActivePath(fixture.root)));
		const BakeArtifacts repeated = CompleteBake(
			service,
			fixture.executor,
			provider,
			binding,
			t0 + 5ms,
			fixture.root);
		CHECK(first.chunk_bytes == repeated.chunk_bytes);
		CHECK(first.manifest_bytes == repeated.manifest_bytes);
		CHECK(first.active_pointer_bytes ==
			repeated.active_pointer_bytes);

		provider.SetMode(
			VegetationTest::DeterministicSurfaceMode::Failed);
		CHECK_FALSE(service.RequestBake(binding, t0 + 6ms));
		const AshEditor::VegetationEditorStatusSnapshot failed =
			service.GetStatusSnapshot();
		CHECK(failed.operation ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(failed.last_known_good_manifest_digest ==
			first.manifest_digest);
		CHECK(failed.active_manifest_digest ==
			repeated.manifest_digest);
		CHECK(failed.palette == reloaded.palette);
		CHECK_FALSE(failed.detail.empty());

		service.Shutdown();
		CHECK(fixture.executor.IsIdle());
		CHECK(fixture.executor.TaskRecordCount() == 0);
		CHECK_FALSE(HasOperationStages(fixture.root.Path()));
	}

	SUBCASE("public stroke encounter order canonicalizes saved and baked bytes")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot palette_only =
			MakeLayer({ assets.primary.palette }, {});
		const AshEngine::VegetationSurfaceIdentity surface_identity =
			VegetationTest::SurfaceIdentity(194, 1, 2, 3);
		VegetationTest::DeterministicSurfaceProvider provider_a(
			surface_identity);
		VegetationTest::DeterministicSurfaceProvider provider_b(
			surface_identity);
		ExitFixture fixture_a(
			"phase2-exit-stroke-order-a", palette_only, &provider_a);
		ExitFixture fixture_b(
			"phase2-exit-stroke-order-b", palette_only, &provider_b);
		AshEditor::VegetationEditorService service_a(
			std::move(fixture_a.deps));
		AshEditor::VegetationEditorService service_b(
			std::move(fixture_b.deps));
		REQUIRE(service_a.Initialize());
		REQUIRE(service_b.Initialize());
		REQUIRE(service_a.OpenLayer(k_layer_path));
		REQUIRE(service_b.OpenLayer(k_layer_path));

		const AshEngine::VegetationSurfaceBinding binding{ 42 };
		const auto chunk_zero = VegetationTest::SurfaceRequest(
			AshEngine::VegetationChunkCoord{ 0, 0 },
			glm::dvec2(128.0, 128.0));
		const auto chunk_one = VegetationTest::SurfaceRequest(
			AshEngine::VegetationChunkCoord{ 1, 0 },
			glm::dvec2(128.0, 128.0));
		const Clock::time_point t0 = Clock::now();
		ExecuteStroke(
			service_a,
			fixture_a.executor,
			provider_a,
			binding,
			AshEngine::VegetationBrushMode::Paint,
			255,
			chunk_zero,
			t0);
		ExecuteStroke(
			service_a,
			fixture_a.executor,
			provider_a,
			binding,
			AshEngine::VegetationBrushMode::Paint,
			255,
			chunk_one,
			t0 + 1ms);
		ExecuteStroke(
			service_b,
			fixture_b.executor,
			provider_b,
			binding,
			AshEngine::VegetationBrushMode::Paint,
			255,
			chunk_one,
			t0);
		ExecuteStroke(
			service_b,
			fixture_b.executor,
			provider_b,
			binding,
			AshEngine::VegetationBrushMode::Paint,
			255,
			chunk_zero,
			t0 + 1ms);

		const auto& batches_a = provider_a.Snapshot()->Batches();
		const auto& batches_b = provider_b.Snapshot()->Batches();
		REQUIRE(batches_a.size() == 2);
		REQUIRE(batches_b.size() == 2);
		REQUIRE(batches_a[0].size() == 1);
		REQUIRE(batches_a[1].size() == 1);
		REQUIRE(batches_b[0].size() == 1);
		REQUIRE(batches_b[1].size() == 1);
		const std::array<int64_t, 2> order_a{
			batches_a[0][0].chunk.x,
			batches_a[1][0].chunk.x
		};
		const std::array<int64_t, 2> order_b{
			batches_b[0][0].chunk.x,
			batches_b[1][0].chunk.x
		};
		CHECK(order_a == std::array<int64_t, 2>{ 0, 1 });
		CHECK(order_b == std::array<int64_t, 2>{ 1, 0 });
		CHECK(order_a != order_b);

		CompleteSave(service_a, fixture_a.executor, t0 + 2ms);
		CompleteSave(service_b, fixture_b.executor, t0 + 2ms);
		const std::vector<uint8_t> layer_bytes_a = ReadArtifactBytes(
			fixture_a.root.Path() / k_layer_path, "canonical Layer A");
		const std::vector<uint8_t> layer_bytes_b = ReadArtifactBytes(
			fixture_b.root.Path() / k_layer_path, "canonical Layer B");
		CHECK(layer_bytes_a == layer_bytes_b);

		provider_a.Snapshot()->ClearObservations();
		provider_b.Snapshot()->ClearObservations();
		const BakeArtifacts baked_a = CompleteBake(
			service_a,
			fixture_a.executor,
			provider_a,
			binding,
			t0 + 3ms,
			fixture_a.root);
		const BakeArtifacts baked_b = CompleteBake(
			service_b,
			fixture_b.executor,
			provider_b,
			binding,
			t0 + 3ms,
			fixture_b.root);
		REQUIRE_FALSE(baked_a.chunk_bytes.empty());
		CHECK(baked_a.chunk_bytes == baked_b.chunk_bytes);
		CHECK(baked_a.manifest_bytes == baked_b.manifest_bytes);
		CHECK(baked_a.active_pointer_bytes ==
			baked_b.active_pointer_bytes);
	}

	SUBCASE("Pending retry and malformed batch use explicit time and binding")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets);
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(182, 1, 2, 3));
		provider.PushCaptureMode(
			VegetationTest::DeterministicSurfaceMode::Pending);
		ExitFixture fixture("phase2-exit-pending", layer, &provider);
		AshEditor::VegetationEditorServiceDeps restart_deps = fixture.deps;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));

		const AshEngine::VegetationSurfaceBinding binding{ 90210 };
		const Clock::time_point t0 = Clock::now();
		REQUIRE(service.RequestBake(binding, t0));
		const AshEditor::VegetationEditorStatusSnapshot pending =
			service.GetStatusSnapshot();
		CHECK(pending.operation ==
			AshEditor::VegetationOperationState::Pending);
		CHECK_FALSE(pending.capabilities.can_bake);
		CHECK(fixture.executor.PendingCount() == 0);
		service.Tick(t0 + 49ms);
		CHECK(provider.CaptureCount() == 1);
		service.Tick(t0 + 50ms);
		CHECK(provider.CaptureCount() == 2);
		REQUIRE(provider.Bindings().size() == 2);
		CHECK(provider.Bindings()[0].surface_entity_id ==
			binding.surface_entity_id);
		CHECK(provider.Bindings()[1].surface_entity_id ==
			binding.surface_entity_id);
		CompleteQueuedOperation(service, fixture.executor, t0 + 50ms);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		const BakeArtifacts last_known_good =
			ReadBakeArtifacts(fixture.root);
		REQUIRE(last_known_good.published);

		REQUIRE(service.RequestReload(t0 + 51ms));
		CompleteQueuedOperation(service, fixture.executor, t0 + 51ms);
		provider.Snapshot()->ClearObservations();
		provider.SetIdentity(
			VegetationTest::SurfaceIdentity(182, 2, 2, 3));
		provider.SetMode(
			VegetationTest::DeterministicSurfaceMode::MalformedBatch);
		REQUIRE(service.RequestBake(binding, t0 + 52ms));
		CompleteQueuedOperation(service, fixture.executor, t0 + 52ms);
		const AshEditor::VegetationEditorStatusSnapshot malformed =
			service.GetStatusSnapshot();
		CHECK(malformed.operation ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(provider.Snapshot()->SampleCallCount() > 0);
		CHECK(malformed.last_known_good_manifest_digest !=
			AshEngine::VegetationSha256{});
		CHECK(ReadBakeArtifacts(fixture.root).active_pointer_bytes ==
			last_known_good.active_pointer_bytes);
		CHECK_FALSE(HasOperationStages(fixture.root.Path()));
		service.Shutdown();

		AshEditor::VegetationEditorService restarted(
			std::move(restart_deps));
		REQUIRE(restarted.Initialize());
		REQUIRE(restarted.OpenLayer(k_layer_path));
		const AshEditor::VegetationEditorStatusSnapshot restart_snapshot =
			restarted.GetStatusSnapshot();
		CHECK(restart_snapshot.active_manifest_digest ==
			last_known_good.manifest_digest);
		CHECK(restart_snapshot.last_known_good_manifest_digest ==
			last_known_good.manifest_digest);
	}

	SUBCASE("source changed and dirty reload preserve the coherent publication")
	{
		ExitAssets assets{};
		AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets);
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(183, 1, 2, 3));
		ExitFixture fixture("phase2-exit-reload-races", layer, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));
		const AshEditor::VegetationEditorStatusSnapshot opened =
			service.GetStatusSnapshot();

		const Clock::time_point t0 = Clock::now();
		REQUIRE(service.RequestReload(t0));
		REQUIRE(fixture.executor.RunNext());
		layer.content_generation = 2;
		layer.layer_seed ^= 0x55aa55aa55aa55aaull;
		fixture.root.Write(k_layer_path, EncodeLayer(layer));
		service.Tick(t0);
		const AshEditor::VegetationEditorStatusSnapshot source_changed =
			service.GetStatusSnapshot();
		CHECK(source_changed.operation ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(source_changed.content_generation ==
			opened.content_generation);
		CHECK(source_changed.palette == opened.palette);

		CompleteCleanReload(service, fixture.executor, t0 + 1ms);
		REQUIRE(service.AddPaletteSpecies(k_secondary_species_path));
		const AshEditor::VegetationEditorStatusSnapshot dirty =
			service.GetStatusSnapshot();
		CHECK(dirty.session == AshEditor::VegetationSessionState::Dirty);
		CHECK_FALSE(service.RequestReload(t0 + 2ms));
		const AshEditor::VegetationEditorStatusSnapshot conflict =
			service.GetStatusSnapshot();
		CHECK(conflict.operation ==
			AshEditor::VegetationOperationState::DirtyConflict);
		CHECK(conflict.content_generation == dirty.content_generation);
		CHECK(conflict.palette == dirty.palette);
		CHECK(fixture.executor.PendingCount() == 0);
	}

	SUBCASE("palette rebuild failure retains one complete publication")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets);
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(192, 1, 2, 3));
		ExitFixture fixture(
			"phase2-exit-publication-coherence", layer, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));
		REQUIRE(service.AddPaletteSpecies(k_secondary_species_path));
		REQUIRE(fixture.commands.Undo());
		const AshEditor::VegetationEditorStatusSnapshot after_undo =
			service.GetStatusSnapshot();
		REQUIRE(after_undo.palette);
		REQUIRE(after_undo.palette->size() == 1);

		REQUIRE(std::filesystem::remove(
			fixture.root.Path() / k_secondary_species_path));
		REQUIRE(fixture.database.refresh());
		REQUIRE(fixture.commands.Redo());
		const AshEditor::VegetationEditorStatusSnapshot failed =
			service.GetStatusSnapshot();
		CHECK(failed.operation ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(failed.content_generation ==
			after_undo.content_generation);
		CHECK(failed.palette == after_undo.palette);
		CHECK_FALSE(failed.capabilities.can_create);
		CHECK_FALSE(failed.capabilities.can_load);
		CHECK_FALSE(failed.capabilities.can_save);
		CHECK_FALSE(failed.capabilities.can_save_copy_as);
		CHECK_FALSE(failed.capabilities.can_reload);
		CHECK_FALSE(failed.capabilities.can_edit_palette);
		CHECK_FALSE(failed.capabilities.can_paint);
		CHECK_FALSE(failed.capabilities.can_erase);
		CHECK_FALSE(failed.capabilities.can_bake);
		CHECK_FALSE(failed.detail.empty());
	}

	SUBCASE("operation serial remains structural across rejected submission and conflicts")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets);
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(184, 1, 2, 3));
		ExitFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		ExitFixture fixture(
			"phase2-exit-serial-contract",
			layer,
			&provider,
			&file_ops);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));
		const Clock::time_point t0 = Clock::now();

		fixture.executor.RejectNextSubmission();
		CHECK_FALSE(service.RequestSave(t0));
		CompleteSave(service, fixture.executor, t0 + 1ms);
		REQUIRE(file_ops.StageSerials().size() == 1);
		CHECK(file_ops.StageSerials().front() == 1);

		fixture.executor.RejectNextSubmission();
		CHECK_FALSE(service.RequestBake({ 42 }, t0 + 2ms));
		const BakeArtifacts first = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0 + 3ms,
			fixture.root);
		CHECK(first.published);
		REQUIRE(file_ops.TreeSerials().size() == 1);
		CHECK(file_ops.TreeSerials().front() == 2);

		REQUIRE(service.RequestBake({ 42 }, t0 + 4ms));
		CHECK_FALSE(service.RequestSave(t0 + 4ms));
		CHECK_FALSE(service.RequestReload(t0 + 4ms));
		CHECK_FALSE(service.RequestBake({ 42 }, t0 + 4ms));
		CHECK(fixture.executor.PendingCount() == 1);
		CompleteQueuedOperation(service, fixture.executor, t0 + 4ms);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		REQUIRE(file_ops.TreeSerials().size() == 2);
		CHECK(file_ops.TreeSerials().back() == 3);
		CHECK_FALSE(HasOperationStages(fixture.root.Path()));
	}

	SUBCASE("session replacement and restart rehydrate only matching active manifest")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets);
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(193, 1, 2, 3));
		ExitFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		ExitFixture fixture(
			"phase2-exit-manifest-session",
			layer,
			&provider,
			&file_ops);
		AshEditor::VegetationEditorServiceDeps restart_deps = fixture.deps;
		AshEditor::VegetationEditorServiceDeps limited_restart_deps =
			fixture.deps;
		limited_restart_deps.chunk_set_load_budget.
			max_total_inspected_bytes = 47;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));
		const Clock::time_point t0 = Clock::now();
		const BakeArtifacts baseline = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0,
			fixture.root);

		CopyChunkStore(fixture.root, k_layer_path, k_other_layer_path);
		AshEngine::VegetationLayerSnapshot other = layer;
		other.layer_id[0] ^= 0x5au;
		fixture.root.Write(k_other_layer_path, EncodeLayer(other));
		REQUIRE(fixture.database.refresh());
		REQUIRE(service.OpenLayer(k_other_layer_path));
		const AshEditor::VegetationEditorStatusSnapshot other_opened =
			service.GetStatusSnapshot();
		CHECK(other_opened.source_path == k_other_layer_path);
		CHECK(other_opened.active_manifest_digest ==
			AshEngine::VegetationSha256{});
		CHECK(other_opened.last_known_good_manifest_digest ==
			AshEngine::VegetationSha256{});

		REQUIRE(service.OpenLayer(k_layer_path));
		const AshEditor::VegetationEditorStatusSnapshot reopened =
			service.GetStatusSnapshot();
		CHECK(reopened.active_manifest_digest ==
			baseline.manifest_digest);
		CHECK(reopened.last_known_good_manifest_digest ==
			baseline.manifest_digest);
		file_ops.SetFault(ExitFileFault::ActiveReadThrow);
		bool reopened_without_throw = false;
		CHECK_NOTHROW(
			reopened_without_throw = service.OpenLayer(k_layer_path));
		REQUIRE(reopened_without_throw);
		const AshEditor::VegetationEditorStatusSnapshot failed_closed =
			service.GetStatusSnapshot();
		CHECK(failed_closed.active_manifest_digest ==
			AshEngine::VegetationSha256{});
		CHECK(failed_closed.last_known_good_manifest_digest ==
			AshEngine::VegetationSha256{});
		file_ops.SetFault(ExitFileFault::None);
		service.Shutdown();

		AshEditor::VegetationEditorService limited_restarted(
			std::move(limited_restart_deps));
		REQUIRE(limited_restarted.Initialize());
		REQUIRE(limited_restarted.OpenLayer(k_layer_path));
		const AshEditor::VegetationEditorStatusSnapshot limited =
			limited_restarted.GetStatusSnapshot();
		CHECK(limited.active_manifest_digest ==
			AshEngine::VegetationSha256{});
		CHECK(limited.last_known_good_manifest_digest ==
			AshEngine::VegetationSha256{});
		limited_restarted.Shutdown();

		AshEditor::VegetationEditorService restarted(
			std::move(restart_deps));
		REQUIRE(restarted.Initialize());
		REQUIRE(restarted.OpenLayer(k_layer_path));
		const AshEditor::VegetationEditorStatusSnapshot fresh =
			restarted.GetStatusSnapshot();
		CHECK(fresh.active_manifest_digest ==
			baseline.manifest_digest);
		CHECK(fresh.last_known_good_manifest_digest ==
			baseline.manifest_digest);
	}

	SUBCASE("object manifest and pointer faults retain last known good")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets);
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(185, 1, 2, 3));
		ExitFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		ExitFixture fixture(
			"phase2-exit-publication-faults",
			layer,
			&provider,
			&file_ops);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));
		const Clock::time_point t0 = Clock::now();
		const BakeArtifacts baseline = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0,
			fixture.root);
		REQUIRE_FALSE(baseline.manifest.entries.empty());

		const std::array faults{
			ExitFileFault::ObjectPublish,
			ExitFileFault::ManifestPublish,
			ExitFileFault::PointerCommit
		};
		for (size_t index = 0; index < faults.size(); ++index)
		{
			file_ops.SetFault(faults[index]);
			provider.SetIdentity(
				VegetationTest::SurfaceIdentity(
					185, 2 + index, 2, 3));
			REQUIRE(service.RequestBake(
				{ 42 }, t0 + std::chrono::milliseconds(index + 1)));
			CompleteQueuedOperation(
				service,
				fixture.executor,
				t0 + std::chrono::milliseconds(index + 1));
			const AshEditor::VegetationEditorStatusSnapshot failed =
				service.GetStatusSnapshot();
			CHECK(failed.operation ==
				AshEditor::VegetationOperationState::Failed);
			CHECK(failed.active_manifest_digest ==
				baseline.manifest_digest);
			CHECK(failed.last_known_good_manifest_digest ==
				baseline.manifest_digest);
			CHECK(ReadBakeArtifacts(fixture.root).active_pointer_bytes ==
				baseline.active_pointer_bytes);
			CHECK_FALSE(HasOperationStages(fixture.root.Path()));
		}
	}

	SUBCASE("full dirty is the exact manifest and authoring density union")
	{
		ExitAssets assets{};
		AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets, 255, 0, 1);
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(186, 1, 2, 3));
		ExitFixture fixture(
			"phase2-exit-full-dirty-union", layer, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));
		const Clock::time_point t0 = Clock::now();
		const BakeArtifacts manifest_only = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0,
			fixture.root);
		REQUIRE(manifest_only.manifest.entries.size() == 1);
		CHECK(manifest_only.manifest.entries.front().coord.x == 0);

		layer = SingleSpeciesLayer(assets, 255, 8, 2);
		fixture.root.Write(k_layer_path, EncodeLayer(layer));
		CompleteCleanReload(service, fixture.executor, t0 + 1ms);
		provider.Snapshot()->ClearObservations();
		const BakeArtifacts authoring_only = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0 + 2ms,
			fixture.root);
		REQUIRE(authoring_only.manifest.entries.size() == 1);
		CHECK(authoring_only.manifest.entries.front().coord.x == 1);
		CHECK(authoring_only.manifest.entries.front().coord.z == 0);
		CHECK(BatchContainsChunk(
			*provider.Snapshot(), { 1, 0 }));
		CHECK(std::none_of(
			authoring_only.manifest.entries.begin(),
			authoring_only.manifest.entries.end(),
			[](const AshEngine::VegetationChunkSetManifestEntry& entry)
			{
				return entry.coord.x == 0 && entry.coord.z == 0;
			}));
	}

	SUBCASE("seed and surface revisions both cross absent and present")
	{
		ExitAssets assets{};
		AshEngine::VegetationLayerSnapshot low_density =
			SingleSpeciesLayer(assets, 1);
		const auto seeds = FindAbsentAndPresentSeeds(
			low_density, *assets.primary.species);
		low_density.layer_seed = seeds.first;
		VegetationTest::DeterministicSurfaceProvider seed_provider(
			VegetationTest::SurfaceIdentity(187, 1, 2, 3));
		ExitFixture seed_fixture(
			"phase2-exit-seed-transitions",
			low_density,
			&seed_provider);
		AshEditor::VegetationEditorService seed_service(
			std::move(seed_fixture.deps));
		REQUIRE(seed_service.Initialize());
		REQUIRE(seed_service.OpenLayer(k_layer_path));
		const Clock::time_point t0 = Clock::now();
		const BakeArtifacts seed_absent = CompleteBake(
			seed_service,
			seed_fixture.executor,
			seed_provider,
			{ 42 },
			t0,
			seed_fixture.root);
		CHECK(seed_absent.manifest.entries.empty());

		low_density.content_generation = 2;
		low_density.layer_seed = seeds.second;
		seed_fixture.root.Write(k_layer_path, EncodeLayer(low_density));
		CompleteCleanReload(
			seed_service, seed_fixture.executor, t0 + 1ms);
		const BakeArtifacts seed_present = CompleteBake(
			seed_service,
			seed_fixture.executor,
			seed_provider,
			{ 42 },
			t0 + 2ms,
			seed_fixture.root);
		REQUIRE(seed_present.manifest.entries.size() == 1);

		low_density.content_generation = 3;
		low_density.layer_seed = seeds.first;
		seed_fixture.root.Write(k_layer_path, EncodeLayer(low_density));
		CompleteCleanReload(
			seed_service, seed_fixture.executor, t0 + 3ms);
		const BakeArtifacts seed_absent_again = CompleteBake(
			seed_service,
			seed_fixture.executor,
			seed_provider,
			{ 42 },
			t0 + 4ms,
			seed_fixture.root);
		CHECK(seed_absent_again.manifest.entries.empty());
		CHECK(seed_present.manifest_digest !=
			seed_absent.manifest_digest);
		CHECK(seed_absent_again.manifest_digest !=
			seed_present.manifest_digest);

		AshEngine::VegetationLayerSnapshot surface_layer =
			SingleSpeciesLayer(assets);
		VegetationTest::DeterministicSurfaceProvider surface_provider(
			VegetationTest::SurfaceIdentity(188, 1, 2, 3));
		surface_provider.SetMode(
			VegetationTest::DeterministicSurfaceMode::Outside);
		ExitFixture surface_fixture(
			"phase2-exit-surface-transitions",
			surface_layer,
			&surface_provider);
		AshEditor::VegetationEditorService surface_service(
			std::move(surface_fixture.deps));
		REQUIRE(surface_service.Initialize());
		REQUIRE(surface_service.OpenLayer(k_layer_path));
		const BakeArtifacts surface_absent = CompleteBake(
			surface_service,
			surface_fixture.executor,
			surface_provider,
			{ 77 },
			t0 + 5ms,
			surface_fixture.root);
		CHECK(surface_absent.manifest.entries.empty());

		surface_provider.SetIdentity(
			VegetationTest::SurfaceIdentity(188, 2, 2, 3));
		surface_provider.SetMode(
			VegetationTest::DeterministicSurfaceMode::Ready);
		const BakeArtifacts surface_present = CompleteBake(
			surface_service,
			surface_fixture.executor,
			surface_provider,
			{ 77 },
			t0 + 6ms,
			surface_fixture.root);
		REQUIRE(surface_present.manifest.entries.size() == 1);

		surface_provider.SetIdentity(
			VegetationTest::SurfaceIdentity(188, 3, 2, 3));
		surface_provider.SetMode(
			VegetationTest::DeterministicSurfaceMode::Outside);
		const BakeArtifacts surface_absent_again = CompleteBake(
			surface_service,
			surface_fixture.executor,
			surface_provider,
			{ 77 },
			t0 + 7ms,
			surface_fixture.root);
		CHECK(surface_absent_again.manifest.entries.empty());
		CHECK(surface_present.manifest_digest !=
			surface_absent.manifest_digest);
		CHECK(surface_absent_again.manifest_digest !=
			surface_present.manifest_digest);
	}

	SUBCASE("shutdown during sample and write leaves no stage or pointer")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer =
			SingleSpeciesLayer(assets);
		const Clock::time_point t0 = Clock::now();
		const std::vector<ExecutorLifecycleEvent> expected_lifecycle{
			ExecutorLifecycleEvent::Cancel,
			ExecutorLifecycleEvent::Observe,
			ExecutorLifecycleEvent::Join
		};

		{
			CancellationBarrier barrier{};
			CancellationNotifyingExecutor executor(barrier);
			VegetationTest::DeterministicSurfaceProvider provider(
				VegetationTest::SurfaceIdentity(189, 1, 2, 3));
			provider.Snapshot()->SetOnSample(
				[&barrier](
					const AshEngine::VegetationOperationControl&)
				{
					barrier.EnterAndWait();
				});
			ExitFixture fixture(
				"phase2-exit-shutdown-sample", layer, &provider);
			fixture.deps.pTaskExecutor = &executor;
			AshEditor::VegetationEditorService service(
				std::move(fixture.deps));
			REQUIRE(service.Initialize());
			REQUIRE(service.OpenLayer(k_layer_path));
			REQUIRE(service.RequestBake({ 42 }, t0));
			REQUIRE(barrier.WaitUntilEntered());
			service.Shutdown();
			CHECK(barrier.Acknowledged());
			CHECK(executor.Events() == expected_lifecycle);
			CHECK(executor.IsIdle());
			CHECK(provider.Snapshot()->SampleCallCount() == 1);
			CHECK_FALSE(std::filesystem::exists(
				ActivePath(fixture.root)));
			CHECK_FALSE(HasOperationStages(fixture.root.Path()));
		}

		{
			CancellationBarrier barrier{};
			CancellationNotifyingExecutor executor(barrier);
			VegetationTest::DeterministicSurfaceProvider provider(
				VegetationTest::SurfaceIdentity(190, 1, 2, 3));
			ExitFileOps file_ops(
				AshEngine::get_default_vegetation_file_ops());
			file_ops.BlockDuringWrite(barrier);
			ExitFixture fixture(
				"phase2-exit-shutdown-write",
				layer,
				&provider,
				&file_ops);
			fixture.deps.pTaskExecutor = &executor;
			AshEditor::VegetationEditorService service(
				std::move(fixture.deps));
			REQUIRE(service.Initialize());
			REQUIRE(service.OpenLayer(k_layer_path));
			REQUIRE(service.RequestBake({ 42 }, t0 + 1ms));
			REQUIRE(barrier.WaitUntilEntered());
			service.Shutdown();
			CHECK(barrier.Acknowledged());
			CHECK(executor.Events() == expected_lifecycle);
			CHECK(executor.IsIdle());
			CHECK_FALSE(std::filesystem::exists(
				ActivePath(fixture.root)));
			CHECK_FALSE(HasOperationStages(fixture.root.Path()));
		}
	}

	SUBCASE("Remove clear evidence survives failure until successful pointer commit")
	{
		ExitAssets assets{};
		const AshEngine::VegetationLayerSnapshot layer = MakeLayer(
			{
				assets.filtered.palette,
				assets.secondary.palette,
				assets.dormant.palette
			},
			{
				MakeTile(
					0,
					0,
					255,
					{
						{ assets.filtered.palette.species_id, 255 },
						{ assets.secondary.palette.species_id, 255 }
					}),
				MakeTile(
					8,
					0,
					255,
					{ { assets.dormant.palette.species_id, 255 } })
			});
		VegetationTest::DeterministicSurfaceProvider provider(
			VegetationTest::SurfaceIdentity(191, 1, 2, 3));
		ExitFixture fixture(
			"phase2-exit-remove-evidence", layer, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer(k_layer_path));
		const Clock::time_point t0 = Clock::now();
		const BakeArtifacts baseline = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0,
			fixture.root);
		REQUIRE(baseline.manifest.entries.size() == 1);
		const AshEngine::VegetationSha256 baseline_input =
			baseline.manifest.entries.front().input_sha256;

		const auto resolver =
			fixture.database.capture_vegetation_resolver_snapshot();
		REQUIRE(resolver);
		const auto active_before =
			AshEngine::read_active_vegetation_chunk_set(
				fixture.root.Path(),
				k_layer_path,
				*resolver,
				AshEditor::VegetationEditorService::
					DefaultChunkSetLoadBudget(),
				VegetationTest::ActiveControl(1s));
		REQUIRE(active_before.status ==
			AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
		REQUIRE(active_before.snapshot);
		REQUIRE(active_before.snapshot->entries.size() == 1);
		CHECK(std::find(
				active_before.snapshot->entries.front().
					referenced_species_ids.begin(),
				active_before.snapshot->entries.front().
					referenced_species_ids.end(),
				assets.filtered.palette.species_id) ==
			active_before.snapshot->entries.front().
				referenced_species_ids.end());
		CHECK(std::find(
				active_before.snapshot->entries.front().
					referenced_species_ids.begin(),
				active_before.snapshot->entries.front().
					referenced_species_ids.end(),
				assets.secondary.palette.species_id) !=
			active_before.snapshot->entries.front().
				referenced_species_ids.end());

		REQUIRE(service.RemovePaletteSpecies(
			assets.filtered.palette.species_id, true));
		CompleteSave(service, fixture.executor, t0 + 1ms);
		provider.SetMode(
			VegetationTest::DeterministicSurfaceMode::MalformedBatch);
		REQUIRE(service.RequestBake({ 42 }, t0 + 2ms));
		CompleteQueuedOperation(service, fixture.executor, t0 + 2ms);
		const AshEditor::VegetationEditorStatusSnapshot failed =
			service.GetStatusSnapshot();
		CHECK(failed.operation ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(failed.last_known_good_manifest_digest ==
			baseline.manifest_digest);
		CHECK(ReadBakeArtifacts(fixture.root).manifest_digest ==
			baseline.manifest_digest);

		provider.SetMode(
			VegetationTest::DeterministicSurfaceMode::Ready);
		provider.Snapshot()->ClearObservations();
		const BakeArtifacts committed = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0 + 3ms,
			fixture.root);
		REQUIRE(committed.manifest.entries.size() == 1);
		CHECK(committed.manifest.entries.front().coord.x == 0);
		CHECK(committed.manifest.entries.front().coord.z == 0);
		CHECK(committed.manifest.entries.front().input_sha256 !=
			baseline_input);
		CHECK(provider.Snapshot()->SampleCallCount() > 0);
		CHECK(BatchContainsChunk(*provider.Snapshot(), { 0, 0 }));
		CHECK_FALSE(BatchContainsChunk(*provider.Snapshot(), { 1, 0 }));

		provider.Snapshot()->ClearObservations();
		const BakeArtifacts no_op = CompleteBake(
			service,
			fixture.executor,
			provider,
			{ 42 },
			t0 + 4ms,
			fixture.root);
		CHECK(provider.Snapshot()->SampleCallCount() == 0);
		CHECK(no_op.active_pointer_bytes ==
			committed.active_pointer_bytes);
		CHECK(service.GetStatusSnapshot().
			last_known_good_manifest_digest ==
			committed.manifest_digest);
		CHECK_FALSE(HasOperationStages(fixture.root.Path()));
	}
}

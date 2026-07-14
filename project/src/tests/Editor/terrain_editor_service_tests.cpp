#include "App/SceneWorkflowCoordinator.h"
#include "Core/TerrainEditorSessionCore.h"
#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/IEditorCommandExecutor.h"
#include "Base/hthreading.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainContainer.h"
#include "Function/Asset/TerrainContainerFormat.h"
#include "Function/Asset/TerrainSpatialData.h"
#include "Services/TerrainEditorService.h"
#include "Services/EditorSettingsService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include "Terrain/TerrainCommitLeaseTestUtils.h"
#include "Terrain/TerrainTestUtils.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif

#include "doctest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	std::string ReadTerrainEditorText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	AshEngine::TerrainLayerId MakeEditorStrokeLayerId()
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = 47u;
		return id;
	}

	AshEngine::TerrainLayerId MakeEditorLayerId(const uint8_t value)
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = value;
		return id;
	}

	AshEngine::TerrainAssetSnapshot MakeEditorStrokeSnapshot(
		AshEngine::TerrainHeightBlendMode blendMode = AshEngine::TerrainHeightBlendMode::Additive)
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
		std::string error{};
		if (!AshEngine::create_flat_terrain_snapshot(
				83u,
				TerrainTests::MakeSmallLayout(),
				{ 0.0f, 1024.0f },
				0.0f,
				flat,
				&error))
		{
			throw std::runtime_error(error);
		}
		AshEngine::TerrainAssetSnapshot snapshot = *flat;
		AshEngine::TerrainEditLayer layer{};
		layer.id = MakeEditorStrokeLayerId();
		layer.name = "Sculpt";
		layer.height_blend_mode = blendMode;
		snapshot.edit_layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ std::move(layer) });
		return snapshot;
	}

	AshEngine::TerrainAssetSnapshot MakeEditorLayerSelectionSnapshot(
		const bool lockThird = false)
	{
		AshEngine::TerrainAssetSnapshot snapshot = MakeEditorStrokeSnapshot();
		auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(
			*snapshot.edit_layers);
		AshEngine::TerrainEditLayer second{};
		second.id = MakeEditorLayerId(48u);
		second.name = "Second";
		layers->push_back(std::move(second));
		AshEngine::TerrainEditLayer third{};
		third.id = MakeEditorLayerId(49u);
		third.name = "Third";
		third.locked = lockThird;
		layers->push_back(std::move(third));
		snapshot.edit_layers = std::move(layers);
		return snapshot;
	}

	bool EqualHeightBlocks(
		const std::vector<AshEngine::TerrainSparseHeightBlock>& lhs,
		const std::vector<AshEngine::TerrainSparseHeightBlock>& rhs)
	{
		if (lhs.size() != rhs.size())
		{
			return false;
		}
		for (size_t index = 0u; index < lhs.size(); ++index)
		{
			const auto& left = lhs[index];
			const auto& right = rhs[index];
			if (!(left.owner == right.owner) ||
				left.changed_rect.min_x != right.changed_rect.min_x ||
				left.changed_rect.min_z != right.changed_rect.min_z ||
				left.changed_rect.max_x_exclusive != right.changed_rect.max_x_exclusive ||
				left.changed_rect.max_z_exclusive != right.changed_rect.max_z_exclusive ||
				left.values != right.values || left.coverage != right.coverage)
			{
				return false;
			}
		}
		return true;
	}

	class RecordingTerrainCommandExecutor final : public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(std::unique_ptr<AshEditor::EditorCommand>) override
		{
			++execute_count;
			return false;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> upCommand) override
		{
			if (!upCommand)
			{
				return AshEditor::EditorCommandRecordResult::RollbackFailed;
			}
			++record_count;
			commands.push_back(std::move(upCommand));
			return AshEditor::EditorCommandRecordResult::Recorded;
		}

		bool UndoLatest(AshEditor::EditorContext& refContext)
		{
			return !commands.empty() && commands.back()->Undo(refContext);
		}

		bool RedoLatest(AshEditor::EditorContext& refContext)
		{
			return !commands.empty() && commands.back()->Execute(refContext);
		}

		bool RemoveCommandsForTerrainAsset(const uint64_t assetId) noexcept override
		{
			if (fail_remove)
			{
				return false;
			}
			removed_terrain_asset_ids.push_back(assetId);
			return true;
		}

		uint32_t execute_count = 0;
		uint32_t record_count = 0;
		bool fail_remove = false;
		std::vector<std::unique_ptr<AshEditor::EditorCommand>> commands{};
		std::vector<uint64_t> removed_terrain_asset_ids{};
	};

	class RejectingTerrainCommandExecutor final : public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(std::unique_ptr<AshEditor::EditorCommand>) override
		{
			return false;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> upCommand) override
		{
			++record_count;
			rollback_succeeded = upCommand && p_context && upCommand->Undo(*p_context);
			return rollback_succeeded
				? AshEditor::EditorCommandRecordResult::RolledBack
				: AshEditor::EditorCommandRecordResult::RollbackFailed;
		}

		AshEditor::EditorContext* p_context = nullptr;
		uint32_t record_count = 0;
		bool rollback_succeeded = false;
	};

	class FailedRollbackTerrainCommandExecutor final : public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(std::unique_ptr<AshEditor::EditorCommand>) override
		{
			return false;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> upCommand) override
		{
			++record_count;
			command_received = upCommand != nullptr;
			return AshEditor::EditorCommandRecordResult::RollbackFailed;
		}

		uint32_t record_count = 0;
		bool command_received = false;
	};

	class MalformedRollbackTerrainCommandExecutor final : public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(std::unique_ptr<AshEditor::EditorCommand>) override
		{
			return false;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> upCommand) override
		{
			++record_count;
			command_received = upCommand != nullptr;
			return AshEditor::EditorCommandRecordResult::RolledBack;
		}

		uint32_t record_count = 0;
		bool command_received = false;
	};

	class WrongSelectionRollbackTerrainCommandExecutor final : public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(std::unique_ptr<AshEditor::EditorCommand>) override
		{
			return false;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> upCommand) override
		{
			rollback_succeeded = upCommand && p_context && upCommand->Undo(*p_context);
			if (rollback_succeeded && p_context->pTerrainEditorService)
			{
				AshEditor::TerrainEditorIntent select{};
				select.kind = AshEditor::TerrainEditorIntent::Kind::SelectLayer;
				select.layer_id = wrong_selection;
				selection_changed = p_context->pTerrainEditorService->SubmitIntent(select);
			}
			return rollback_succeeded
				? AshEditor::EditorCommandRecordResult::RolledBack
				: AshEditor::EditorCommandRecordResult::RollbackFailed;
		}

		AshEditor::EditorContext* p_context = nullptr;
		AshEngine::TerrainLayerId wrong_selection{};
		bool rollback_succeeded = false;
		bool selection_changed = false;
	};

	class ThrowingTerrainCommandExecutor final : public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(std::unique_ptr<AshEditor::EditorCommand>) override
		{
			return false;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> upCommand) override
		{
			++record_count;
			command_received = upCommand != nullptr;
			throw std::runtime_error("injected history failure");
		}

		uint32_t record_count = 0;
		bool command_received = false;
	};

	AshEditor::TerrainEditorIntent MakeBeginStrokeIntent()
	{
		AshEditor::TerrainEditorIntent intent{};
		intent.kind = AshEditor::TerrainEditorIntent::Kind::BeginStroke;
		intent.asset_id = 83u;
		intent.layer_id = MakeEditorStrokeLayerId();
		intent.brush.tool = AshEngine::TerrainBrushTool::Raise;
		intent.brush.radius_meters = 1.0f;
		intent.brush.strength = 1.0f;
		intent.brush.falloff = 1.0f;
		intent.brush.stroke_spacing_meters = 1.0f;
		intent.brush.layer_id = MakeEditorStrokeLayerId();
		intent.brush_metric.world_meters_per_terrain_meter = { 2.0f, 0.5f };
		return intent;
	}

	bool SubmitConfiguredBeginStroke(
		AshEditor::TerrainEditorService& refService,
		AshEditor::TerrainEditorIntent begin = MakeBeginStrokeIntent())
	{
		AshEditor::TerrainEditorIntent configure{};
		configure.kind = AshEditor::TerrainEditorIntent::Kind::ConfigureAuthoring;
		configure.mode = AshEditor::TerrainEditorMode::Sculpt;
		configure.brush = begin.brush;
		if (!refService.SubmitIntent(configure))
		{
			return false;
		}
		begin.brush = refService.GetAuthoringConfig().brush;
		if (begin.asset_id == 83u)
		{
			if (const AshEngine::TerrainWorkingSet* workingSet = refService.GetWorkingSet())
			{
				begin.asset_id = workingSet->asset_id;
			}
		}
		return refService.SubmitIntent(begin);
	}

	AshEditor::TerrainEditorIntent MakeStrokeSampleIntent(float x, float z)
	{
		AshEditor::TerrainEditorIntent intent{};
		intent.kind = AshEditor::TerrainEditorIntent::Kind::AddStrokeSample;
		intent.stroke_sample.terrain_local_xz = { x, z };
		intent.stroke_sample.pressure = 1.0f;
		return intent;
	}

	AshEditor::TerrainEditorIntent MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind kind)
	{
		AshEditor::TerrainEditorIntent intent{};
		intent.kind = kind;
		return intent;
	}

	AshEditor::TerrainEditorIntent MakeLayerActionIntent(
		const AshEditor::TerrainLayerActionKind kind,
		const AshEngine::TerrainLayerId layer_id = {})
	{
		AshEditor::TerrainEditorIntent intent{};
		intent.kind = AshEditor::TerrainEditorIntent::Kind::LayerAction;
		intent.layer_action.kind = kind;
		intent.layer_action.layer_id = layer_id;
		return intent;
	}

	AshEditor::TerrainEditorIntent MakeSelectLayerIntent(
		const AshEngine::TerrainLayerId layerId)
	{
		AshEditor::TerrainEditorIntent intent{};
		intent.kind = AshEditor::TerrainEditorIntent::Kind::SelectLayer;
		intent.layer_id = layerId;
		return intent;
	}

	struct TerrainEditorThreadingScope
	{
		~TerrainEditorThreadingScope()
		{
			AshEngine::shutdown_threading();
		}
	};

	struct TerrainEditorWorkerBlocker
	{
		std::promise<void> release_promise{};
		std::shared_future<void> release_future{};
		AshEngine::ThreadCommandFuture blocker_future{};
		bool released = false;

		TerrainEditorWorkerBlocker()
		{
			release_future = release_promise.get_future().share();
			std::promise<void> startedPromise{};
			auto startedFuture = startedPromise.get_future();
			blocker_future = AshEngine::dispatch_background_task(
				"TerrainEditorServiceTests::WorkerBlocker",
				[started = std::move(startedPromise), release = release_future]() mutable
				{
					started.set_value();
					release.wait();
				});
			startedFuture.wait();
		}

		~TerrainEditorWorkerBlocker()
		{
			Release();
			if (blocker_future.valid())
			{
				blocker_future.wait();
			}
		}

		void Release()
		{
			if (!released)
			{
				released = true;
				release_promise.set_value();
			}
		}
	};

	struct TerrainEditorFileJobHookBlocker
	{
		struct State
		{
			std::promise<void> entered_promise{};
			std::future<void> entered_future{};
			std::promise<void> release_promise{};
			std::shared_future<void> release_future{};
			std::atomic<uint32_t> entry_count{ 0u };
			std::atomic<bool> released{ false };

			State()
				: entered_future(entered_promise.get_future()),
				release_future(release_promise.get_future().share())
			{
			}
		};

		std::shared_ptr<State> state = std::make_shared<State>();
		AshEditor::TerrainFileOperationKind kind =
			AshEditor::TerrainFileOperationKind::None;
		AshEditor::TerrainEditorService::FileJobTestPoint point =
			AshEditor::TerrainEditorService::FileJobTestPoint::BeforeCodec;

		TerrainEditorFileJobHookBlocker(
			const AshEditor::TerrainFileOperationKind expectedKind,
			const AshEditor::TerrainEditorService::FileJobTestPoint expectedPoint)
			: kind(expectedKind), point(expectedPoint)
		{
		}

		~TerrainEditorFileJobHookBlocker()
		{
			Release();
		}

		auto Callback() const -> AshEditor::TerrainEditorService::FileJobTestHook
		{
			const auto capturedState = state;
			const auto expectedKind = kind;
			const auto expectedPoint = point;
			return [capturedState, expectedKind, expectedPoint](
				const AshEditor::TerrainFileOperationKind actualKind,
				const AshEditor::TerrainEditorService::FileJobTestPoint actualPoint)
			{
				if (actualKind != expectedKind || actualPoint != expectedPoint)
				{
					return;
				}
				if (capturedState->entry_count.fetch_add(1u, std::memory_order_acq_rel) == 0u)
				{
					capturedState->entered_promise.set_value();
				}
				capturedState->release_future.wait();
			};
		}

		void WaitUntilEntered()
		{
			state->entered_future.wait();
		}

		void Release()
		{
			if (!state->released.exchange(true, std::memory_order_acq_rel))
			{
				state->release_promise.set_value();
			}
		}
	};

	std::filesystem::path TerrainEditorFilePath(const char* name)
	{
		const std::filesystem::path directory =
			std::filesystem::temp_directory_path() / "AshEngineTerrainEditorSaveTests";
		std::filesystem::create_directories(directory);
		return directory / name;
	}

	std::filesystem::path TerrainEditorAssetRoot(const char* name)
	{
		const std::filesystem::path root = TerrainEditorFilePath(name);
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);
		return root;
	}

	void WriteTerrainEditorR16(
		const std::filesystem::path& path,
		const std::vector<uint16_t>& values,
		const AshEngine::TerrainByteOrder byteOrder = AshEngine::TerrainByteOrder::LittleEndian)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		for (const uint16_t value : values)
		{
			std::array<uint8_t, 2> bytes{
				static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8u) };
			if (byteOrder == AshEngine::TerrainByteOrder::BigEndian)
			{
				std::swap(bytes[0], bytes[1]);
			}
			REQUIRE(output.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size())));
		}
	}

	std::vector<float> ReadTerrainEditorR32F(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		REQUIRE(input.is_open());
		const std::streamsize byteCount = input.tellg();
		REQUIRE(byteCount >= 0);
		REQUIRE(static_cast<size_t>(byteCount) % sizeof(float) == 0u);
		std::vector<float> values(static_cast<size_t>(byteCount) / sizeof(float));
		input.seekg(0);
		REQUIRE(input.read(
			reinterpret_cast<char*>(values.data()),
			byteCount));
		return values;
	}

	bool HasTerrainEditorFileJobTemporary(const std::filesystem::path& root)
	{
		for (const std::filesystem::directory_entry& entry :
			std::filesystem::recursive_directory_iterator(root))
		{
			const std::string name = entry.path().filename().string();
			if (name.find(".create.") != std::string::npos ||
				name.find(".import.") != std::string::npos ||
				name.find(".export.") != std::string::npos ||
				name.find(".save-copy.") != std::string::npos ||
				(name.size() >= 4u && name.compare(name.size() - 4u, 4u, ".tmp") == 0))
			{
				return true;
			}
		}
		return false;
	}

	void SaveTerrainEditorSnapshot(
		const std::filesystem::path& path,
		const AshEngine::TerrainAssetSnapshot& snapshot)
	{
		std::filesystem::remove(path);
		std::string error{};
		REQUIRE(AshEngine::save_terrain_container_incremental(
			path, snapshot, {}, nullptr, &error) == AshEngine::TerrainContainerResult::Success);
		REQUIRE_MESSAGE(error.empty(), error);
	}

	void CorruptNewestTerrainIndexDescriptor(const std::filesystem::path& path)
	{
		using AshEngine::TerrainContainerFormat::FileHeaderDisk;
		using AshEngine::TerrainContainerFormat::IndexDescriptorDisk;
		FileHeaderDisk header{};
		{
			std::ifstream input(path, std::ios::binary);
			REQUIRE(input.read(reinterpret_cast<char*>(&header), sizeof(header)));
		}
		const size_t newestSlot =
			header.index_descriptors[1].generation_le >
			header.index_descriptors[0].generation_le ? 1u : 0u;
		const uint64_t crcOffset = offsetof(FileHeaderDisk, index_descriptors) +
			newestSlot * sizeof(IndexDescriptorDisk) +
			offsetof(IndexDescriptorDisk, index_crc32_le);
		std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
		REQUIRE(file.is_open());
		file.seekg(static_cast<std::streamoff>(crcOffset));
		char value = 0;
		REQUIRE(file.read(&value, 1));
		value ^= 0x5a;
		file.seekp(static_cast<std::streamoff>(crcOffset));
		REQUIRE(file.write(&value, 1));
	}

	void BindTerrainEditorSnapshotToAssetDatabase(
		AshEngine::AssetDatabase& assets,
		const std::filesystem::path& path,
		AshEngine::TerrainAssetSnapshot& snapshot)
	{
		const std::filesystem::path relative = path.lexically_relative(assets.get_root_path());
		const AshEngine::AssetInfo* info = assets.find_asset_by_path(relative);
		REQUIRE(info != nullptr);
		REQUIRE(info->type == AshEngine::AssetType::Terrain);
		snapshot.asset_id = info->id;
	}

	bool WaitForTerrainFileOperation(
		AshEditor::TerrainEditorService& service,
		const std::chrono::milliseconds timeout = std::chrono::seconds(2))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			service.Update();
			const AshEditor::TerrainFileOperationStatus status =
				service.GetFileOperationState().status;
			if (status == AshEditor::TerrainFileOperationStatus::Succeeded ||
				status == AshEditor::TerrainFileOperationStatus::Failed)
			{
				return status == AshEditor::TerrainFileOperationStatus::Succeeded;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	bool WaitForTerrainFileOperationStatus(
		AshEditor::TerrainEditorService& service,
		const AshEditor::TerrainFileOperationStatus expected,
		const std::chrono::milliseconds timeout = std::chrono::seconds(2))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			service.Update();
			if (service.GetFileOperationState().status == expected)
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	void WriteTerrainEditorText(
		const std::filesystem::path& path,
		const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		output.write(text.data(), static_cast<std::streamsize>(text.size()));
		REQUIRE(output.good());
	}

	AshEngine::TerrainAssetSnapshot MakeExternalTerrainGeneration(
		const AshEngine::TerrainAssetSnapshot& source,
		const uint64_t generation,
		const char* layerName)
	{
		AshEngine::TerrainAssetSnapshot result = source;
		result.content_generation = generation;
		auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(
			*source.edit_layers);
		if (!layers->empty())
		{
			layers->front().name = layerName ? layerName : "External";
		}
		result.edit_layers = std::move(layers);
		result.components.clear();
		result.components.reserve(source.components.size());
		for (const auto& component : source.components)
		{
			auto replacement = std::make_shared<AshEngine::TerrainComponentSnapshot>(*component);
			replacement->content_generation = generation;
			result.components.push_back(std::move(replacement));
		}
		return result;
	}

	AshEngine::TerrainAssetSnapshot AppendChangedTerrainGeneration(
		const std::filesystem::path& path,
		const AshEngine::TerrainAssetSnapshot& previous,
		const uint64_t generation,
		const char* layerName)
	{
		AshEngine::TerrainAssetSnapshot next = MakeExternalTerrainGeneration(
			previous, generation, layerName);
		next.source_path = path;
		auto changedComponent = std::make_shared<AshEngine::TerrainComponentSnapshot>(
			*previous.components.front());
		changedComponent->content_generation = generation;
		changedComponent->heights.front() += 1.0f;
		std::string error{};
		REQUIRE(AshEngine::build_terrain_component_spatial_data(
			*changedComponent,
			changedComponent->sample_width,
			changedComponent->sample_height,
			&error));
		next.components = previous.components;
		next.components.front() = changedComponent;
		const std::vector<AshEngine::TerrainDirtyComponentPayload> dirtyComponents{
			{ changedComponent->coord, generation, changedComponent }
		};
		const AshEngine::TerrainContainerResult saveResult =
			AshEngine::save_terrain_container_incremental(
				path, next, dirtyComponents, nullptr, &error);
		REQUIRE_MESSAGE(saveResult == AshEngine::TerrainContainerResult::Success, error);
		return next;
	}

	bool WaitForTerrainExternalState(
		AshEditor::TerrainEditorService& service,
		const AshEditor::TerrainExternalChangeStatus expected,
		const std::chrono::milliseconds timeout = std::chrono::seconds(2))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			service.Update();
			if (service.GetExternalChangeState().status == expected)
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}
}

TEST_CASE("Terrain editor session core starts without mutable asset state")
{
	AshEditor::TerrainEditorSessionCore core{};
	CHECK(core.GetAssetId() == 0u);
	CHECK(core.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Outside);
	CHECK_FALSE(core.HasActiveStroke());
}

TEST_CASE("Terrain Inspector routes component changes through an EditorCommand")
{
	const std::string registry = ReadTerrainEditorText(
		"project/src/editor/Panels/Inspector/InspectorComponentEditorRegistry.cpp");
	const std::string editor = ReadTerrainEditorText(
		"project/src/editor/Panels/Inspector/TerrainComponentEditor.cpp");
	const std::string commands = ReadTerrainEditorText(
		"project/src/editor/Core/EntityCommands.cpp");
	const std::string snapshots = ReadTerrainEditorText(
		"project/src/editor/Core/SceneSnapshotComponentUtils.cpp");

	CHECK(registry.find("TerrainComponentEditor") != std::string::npos);
	CHECK(editor.find("CommitTerrainDraft") != std::string::npos);
	CHECK(commands.find("SetTerrainComponentCommand::Execute") != std::string::npos);
	CHECK(commands.find("SetTerrainComponentCommand::Undo") != std::string::npos);
	CHECK(editor.find(".set_terrain_component") == std::string::npos);
	CHECK(snapshots.find("MakeTerrainComponentSnapshot") != std::string::npos);
	CHECK(snapshots.find("material_layer_overrides") != std::string::npos);
}

TEST_CASE("Terrain editor core accepts one selected asset and immutable intents")
{
	AshEditor::TerrainEditorSessionCore core{};
	AshEditor::TerrainEditorIntent select{};
	select.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	select.asset_id = 17u;

	CHECK(core.Reduce(select));
	CHECK(core.GetAssetId() == 17u);
	CHECK(select.asset_id == 17u);
}

TEST_CASE("Terrain editor session core owns one validated working set")
{
	AshEditor::TerrainEditorSessionCore core{};
	CHECK_FALSE(core.Open({}));

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	REQUIRE(AshEngine::create_flat_terrain_snapshot(
		19u,
		TerrainTests::MakeSmallLayout(),
		{ 0.0f, 1024.0f },
		0.0f,
		snapshot,
		&error));
	AshEngine::TerrainWorkingSet workingSet{};
	REQUIRE(AshEngine::make_terrain_working_set(*snapshot, workingSet, &error));
	REQUIRE(core.Open(std::move(workingSet)));

	REQUIRE(core.GetWorkingSet() != nullptr);
	CHECK(core.GetWorkingSet()->asset_id == 19u);
	CHECK(core.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	CHECK_FALSE(core.IsDirty());
}

TEST_CASE("Terrain editor service owns async loading without backend dependencies")
{
	const std::string service = ReadTerrainEditorText(
		"project/src/editor/Services/TerrainEditorService.cpp");
	const std::string serviceHeader = ReadTerrainEditorText(
		"project/src/editor/Services/TerrainEditorService.h");
	const std::string application = ReadTerrainEditorText(
		"project/src/editor/App/EditorApplicationImpl.cpp");

	CHECK(serviceHeader.find("TerrainEditorSessionCore _core") != std::string::npos);
	CHECK(service.find("load_terrain_candidate_by_id_async") != std::string::npos);
	CHECK(service.find("Graphics/") == std::string::npos);
	CHECK(service.find("Vulkan") == std::string::npos);
	CHECK(service.find("DirectX12") == std::string::npos);
	const size_t terrainUpdate = application.find("_upTerrainEditorService->Update()");
	const size_t panelUpdate = application.find("_upPanelManager->Update()");
	REQUIRE(terrainUpdate != std::string::npos);
	REQUIRE(panelUpdate != std::string::npos);
	CHECK(terrainUpdate < panelUpdate);
}

TEST_CASE("Terrain editor forwards one raw path to one Engine brush transaction")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(1.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(3.0f, 4.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	CHECK(commands.execute_count == 0u);
	CHECK(commands.record_count == 1u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 1u);
	CHECK_FALSE(service.GetWorkingSet()->dirty_components.empty());
	CHECK(service.HasPendingComposition());

	const std::string source = ReadTerrainEditorText(
		"project/src/editor/Services/TerrainEditorService.cpp");
	const std::string coreSource = ReadTerrainEditorText(
		"project/src/editor/Core/TerrainEditorSessionCore.cpp");
	CHECK(source.find("resample_terrain_stroke") == std::string::npos);
	CHECK(coreSource.find("resample_terrain_stroke") == std::string::npos);
	CHECK(source.find("apply_terrain_brush_stroke(") == std::string::npos);
	const size_t brushCall = coreSource.find("apply_terrain_brush_stroke(");
	REQUIRE(brushCall != std::string::npos);
	CHECK(coreSource.find("apply_terrain_brush_stroke(", brushCall + 1u) == std::string::npos);
}

TEST_CASE("Terrain editor preserves raw sample values and the frozen non-uniform metric")
{
	const AshEngine::TerrainAssetSnapshot snapshot = MakeEditorStrokeSnapshot();
	AshEngine::TerrainWorkingSet expectedWorkingSet{};
	std::string error{};
	REQUIRE(AshEngine::make_terrain_working_set(snapshot, expectedWorkingSet, &error));
	AshEditor::TerrainEditorIntent begin = MakeBeginStrokeIntent();
	std::vector<AshEngine::TerrainStrokeSample> rawSamples{
		{ { 1.0f, 2.0f }, 0.25f },
		{ { 3.0f, 4.0f }, 1.0f }
	};
	std::vector<AshEngine::TerrainEditPatch> expectedPatches{};
	std::vector<AshEngine::TerrainComponentCoord> expectedDirty{};
	REQUIRE(AshEngine::apply_terrain_brush_stroke(
		expectedWorkingSet,
		begin.brush,
		begin.brush_metric,
		rawSamples,
		expectedPatches,
		expectedDirty,
		&error));

	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(snapshot));
	REQUIRE(SubmitConfiguredBeginStroke(service, begin));
	begin.brush.radius_meters = 128.0f;
	begin.brush_metric.world_meters_per_terrain_meter = { 1.0f, 1.0f };
	AshEditor::TerrainEditorIntent first = MakeStrokeSampleIntent(1.0f, 2.0f);
	first.stroke_sample.pressure = 0.25f;
	REQUIRE(service.SubmitIntent(first));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(3.0f, 4.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetWorkingSet()->content_generation == expectedWorkingSet.content_generation);
	CHECK(service.GetWorkingSet()->dirty_components == expectedDirty);
	CHECK(EqualHeightBlocks(
		service.GetWorkingSet()->edit_layers.front().height_blocks,
		expectedWorkingSet.edit_layers.front().height_blocks));
}

TEST_CASE("Terrain editor cancel and empty stroke create no mutation or history")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	CHECK(commands.record_count == 0u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration);

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
	CHECK(commands.record_count == 0u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration);
	CHECK_FALSE(service.GetPreviewState().stroke_active);
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetWorkingSet()->dirty_components.empty());
	CHECK(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));

	AshEditor::TerrainEditorIntent invalidMetric = MakeBeginStrokeIntent();
	invalidMetric.brush_metric.world_meters_per_terrain_meter.x = 0.0f;
	CHECK_FALSE(SubmitConfiguredBeginStroke(service, invalidMetric));
	invalidMetric = MakeBeginStrokeIntent();
	invalidMetric.brush_metric.world_meters_per_terrain_meter.y = -1.0f;
	CHECK_FALSE(SubmitConfiguredBeginStroke(service, invalidMetric));
	invalidMetric = MakeBeginStrokeIntent();
	invalidMetric.brush_metric.world_meters_per_terrain_meter.x =
		std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(SubmitConfiguredBeginStroke(service, invalidMetric));

	AshEditor::TerrainEditorIntent wrongAsset = MakeBeginStrokeIntent();
	wrongAsset.asset_id = 84u;
	CHECK_FALSE(SubmitConfiguredBeginStroke(service, wrongAsset));
	CHECK_FALSE(service.GetPreviewState().stroke_active);
}

TEST_CASE("Terrain editor reserves a generation for history rollback")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	AshEngine::TerrainAssetSnapshot snapshot = MakeEditorStrokeSnapshot();
	snapshot.content_generation = std::numeric_limits<uint64_t>::max() - 1u;
	REQUIRE(service.OpenSnapshotForAuthoring(snapshot));

	CHECK_FALSE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetWorkingSet()->content_generation == snapshot.content_generation);
	CHECK(commands.record_count == 0u);
}

TEST_CASE("Terrain editor publishes generations using only the latest complete dirty set")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(1.0f, 1.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(3.0f, 3.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 2u);
	CHECK(commands.record_count == 2u);

	service.Update();
	REQUIRE(service.GetPublishedSnapshot() != nullptr);
	CHECK(service.GetPublishedSnapshot()->content_generation == initialGeneration + 2u);
	CHECK(service.GetWorkingSet()->dirty_components.empty());
	CHECK_FALSE(service.HasPendingComposition());
}

TEST_CASE("Terrain editor save persists cumulative published component generations")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("cumulative-save-root");
	const std::filesystem::path path = root / "cumulative-save.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(1.0f, 1.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	service.Update();
	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(7.0f, 7.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	service.Update();
	const std::shared_ptr<const AshEngine::TerrainAssetSnapshot> expected =
		service.GetPublishedSnapshot();
	REQUIRE(expected);
	CHECK(expected->content_generation == initial.content_generation + 2u);
	CHECK(std::any_of(
		expected->components.begin(), expected->components.end(),
		[&initial](const auto& component)
		{
			return component->content_generation == initial.content_generation + 1u;
		}));

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	const uint64_t operationSerial = service.GetFileOperationState().operation_serial;
	REQUIRE(operationSerial != 0u);
	REQUIRE(WaitForTerrainFileOperation(service));
	CHECK(service.GetFileOperationState().operation_serial == operationSerial);
	CHECK_FALSE(service.HasDirtyAssets());

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->components.size() == expected->components.size());
	for (size_t index = 0u; index < loaded->components.size(); ++index)
	{
		CHECK(loaded->components[index]->content_generation ==
			expected->components[index]->content_generation);
		CHECK(loaded->components[index]->heights == expected->components[index]->heights);
	}
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor save completion leaves edits made after capture dirty")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("save-generation-capture-root");
	const std::filesystem::path path = root / "save-generation-capture.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Captured Save";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	const uint64_t capturedGeneration = service.GetFileOperationState().content_generation;

	rename.layer_action.name = "Later Edit";
	REQUIRE(service.SubmitIntent(rename));
	REQUIRE(WaitForTerrainFileOperation(service));
	service.Update();
	CHECK(service.HasDirtyAssets());
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation > capturedGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Later Edit");

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->edit_layers);
	CHECK(loaded->edit_layers->front().name == "Captured Save");
	CHECK(commands.record_count == 2u);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor save waits for the requested composition before dispatch")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("save-awaits-publication-root");
	const std::filesystem::path path = root / "save-awaits-publication.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Awaited Publication";
	REQUIRE(service.SubmitIntent(rename));
	REQUIRE(service.HasPendingComposition());
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::AwaitingPublication);
	CHECK(service.HasBlockingOperation());

	rename.layer_action.name = "Must Be Rejected";
	CHECK_FALSE(service.SubmitIntent(rename));
	CHECK(service.GetLastError().find("paused") != std::string::npos);
	REQUIRE(WaitForTerrainFileOperation(service));
	CHECK_FALSE(service.HasPendingComposition());
	CHECK_FALSE(service.HasDirtyAssets());

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->edit_layers);
	CHECK(loaded->edit_layers->front().name == "Awaited Publication");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor async save fails quickly without a worker and preserves the file")
{
	AshEngine::shutdown_threading();
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("save-no-worker-root");
	const std::filesystem::path path = root / "save-no-worker.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Must Not Reach Disk";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	CHECK_FALSE(WaitForTerrainFileOperation(service, std::chrono::milliseconds(250)));
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Failed);
	CHECK(service.GetFileOperationState().error.find("worker") != std::string::npos);
	CHECK(service.HasDirtyAssets());

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(path, loaded, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loaded);
	REQUIRE(loaded->edit_layers);
	CHECK(loaded->edit_layers->front().name == "Sculpt");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor failed save preserves dirty state and command history")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("failed-save-root");
	const std::filesystem::path parent = root / "terrain";
	std::filesystem::create_directories(parent);
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = parent / "failed-save.AshTerrain";
	SaveTerrainEditorSnapshot(initial.source_path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, initial.source_path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	std::filesystem::remove_all(parent);
	{
		std::ofstream blocker(parent, std::ios::binary);
		REQUIRE(blocker.is_open());
		blocker.put('x');
	}
	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Unsaved After Failure";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();

	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	CHECK(service.GetLastError().find("parent") != std::string::npos);
	CHECK(service.HasDirtyAssets());
	CHECK(commands.record_count == 1u);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Unsaved After Failure");
	CHECK_FALSE(service.GetLastError().empty());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor SaveAs writes a copy without rebinding or clearing dirty state")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("save-as-copy-root");
	const std::filesystem::path sourcePath = root / "save-as-source.AshTerrain";
	const std::filesystem::path copyPath = root / "save-as-copy.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Saved Copy";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();

	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = copyPath;
	const bool submitted = service.SubmitIntent(saveAs);
	REQUIRE_MESSAGE(submitted, service.GetLastError());
	REQUIRE(WaitForTerrainFileOperation(service));
	CHECK(service.HasDirtyAssets());
	CHECK(service.GetWorkingSet()->source_path == sourcePath);

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> copy{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(sourcePath, source, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(AshEngine::load_terrain_container(copyPath, copy, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(source);
	REQUIRE(source->edit_layers);
	REQUIRE(copy);
	REQUIRE(copy->edit_layers);
	CHECK(source->edit_layers->front().name == "Sculpt");
	CHECK(copy->edit_layers->front().name == "Saved Copy");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor SaveAs atomically refuses a destination that appears after submit")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("save-as-race-root");
	const std::filesystem::path sourcePath = root / "source.AshTerrain";
	const std::filesystem::path copyPath = root / "appeared.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = copyPath;
	REQUIRE(service.SubmitIntent(saveAs));
	const std::string appearedBytes = "external writer owns this destination";
	{
		std::ofstream appeared(copyPath, std::ios::binary | std::ios::trunc);
		REQUIRE(appeared.is_open());
		appeared.write(appearedBytes.data(), static_cast<std::streamsize>(appearedBytes.size()));
		REQUIRE(appeared.good());
	}
	blocker.Release();

	CHECK_FALSE(WaitForTerrainFileOperation(service));
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Failed);
	INFO(service.GetFileOperationState().error);
	CHECK(service.GetFileOperationState().error.find("appeared") != std::string::npos);
	CHECK(ReadTerrainEditorText(copyPath) == appearedBytes);
	for (const auto& entry : std::filesystem::directory_iterator(root))
	{
		CHECK(entry.path().filename().string().find(".save-copy.") == std::string::npos);
	}
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor shutdown waits for an active file worker without losing its result")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("shutdown-waits-for-file-worker-root");
	const std::filesystem::path sourcePath = root / "source.AshTerrain";
	const std::filesystem::path copyPath = root / "copy.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = copyPath;
	REQUIRE(service.SubmitIntent(saveAs));
	REQUIRE(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Running);

	std::promise<void> shutdownStartedPromise{};
	auto shutdownStarted = shutdownStartedPromise.get_future();
	auto shutdown = std::async(
		std::launch::async,
		[&service, started = std::move(shutdownStartedPromise)]() mutable
		{
			started.set_value();
			service.Shutdown();
		});
	REQUIRE(shutdownStarted.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
	CHECK(shutdown.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
	CHECK_FALSE(std::filesystem::exists(copyPath));

	blocker.Release();
	REQUIRE(shutdown.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	shutdown.get();
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> copy{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(copyPath, copy, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(copy != nullptr);
	CHECK(copy->content_generation == initial.content_generation);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor SaveAs rejects paths outside the configured asset root")
{
	const std::filesystem::path root = TerrainEditorFilePath("save-as-root");
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "terrain");
	const std::filesystem::path sourcePath = root / "terrain" / "source.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = "../escaped.AshTerrain";
	CHECK_FALSE(service.SubmitIntent(saveAs));
	CHECK(service.GetLastError().find("inside") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(root.parent_path() / "escaped.AshTerrain"));

	saveAs.asset_path = "terrain/not-terrain.bin";
	CHECK_FALSE(service.SubmitIntent(saveAs));
	CHECK(service.GetLastError().find(".AshTerrain") != std::string::npos);

	const std::filesystem::path outside = root.parent_path() / "save-as-outside";
	std::filesystem::remove_all(outside);
	std::filesystem::create_directories(outside);
	const std::filesystem::path escapeLink = root / "terrain" / "escape-link";
	std::error_code linkError{};
	std::filesystem::create_directory_symlink(outside, escapeLink, linkError);
	if (!linkError)
	{
		saveAs.asset_path = escapeLink / "escaped.AshTerrain";
		CHECK_FALSE(service.SubmitIntent(saveAs));
		CHECK(service.GetLastError().find("inside") != std::string::npos);
		CHECK_FALSE(std::filesystem::exists(outside / "escaped.AshTerrain"));
	}
	std::filesystem::remove_all(outside);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file operations require a configured asset root")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = TerrainEditorFilePath("unrooted-source.AshTerrain");
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = TerrainEditorFilePath("unrooted-copy.AshTerrain");
	CHECK_FALSE(service.SubmitIntent(saveAs));
	CHECK(service.GetLastError().find("asset root") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(saveAs.asset_path));
}

TEST_CASE("Terrain editor classifies unresolved Scene references fail closed")
{
	const std::filesystem::path root = TerrainEditorAssetRoot("scene-reference-classification-root");
	const std::filesystem::path sourcePath = root / "active.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	CHECK(service.ClassifyCurrentAssetReferences({}) ==
		AshEditor::TerrainAssetReferenceMatch::Different);
	CHECK(service.ClassifyCurrentAssetReferences({ sourcePath }) ==
		AshEditor::TerrainAssetReferenceMatch::Current);
	CHECK(service.ClassifyCurrentAssetReferences({ "active.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Current);
	CHECK(service.ClassifyCurrentAssetReferences({ "folder/../active.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Current);
	CHECK(service.ClassifyCurrentAssetReferences({ "other.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Different);
	CHECK(service.ClassifyCurrentAssetReferences({ "../outside.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Unsafe);
	CHECK(service.ClassifyCurrentAssetReferences(
		{ "../outside.AshTerrain", "active.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Current);
	CHECK(service.ClassifyCurrentAssetReferences(
		{ "active.AshTerrain", "../outside.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Current);
	CHECK(service.ClassifyCurrentAssetReferences(
		{ "../outside.AshTerrain", "other.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Unsafe);

	AshEngine::TerrainAssetSnapshot outside = MakeEditorStrokeSnapshot();
	outside.source_path = root.parent_path() / "outside-source.AshTerrain";
	AshEditor::TerrainEditorService outsideService{};
	REQUIRE(outsideService.Initialize(assets, commands));
	REQUIRE(outsideService.OpenSnapshotForAuthoring(outside));
	CHECK(outsideService.ClassifyCurrentAssetReferences({ "active.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Unsafe);

	AshEditor::TerrainEditorService unrooted{};
	REQUIRE(unrooted.Initialize(commands));
	REQUIRE(unrooted.OpenSnapshotForAuthoring(initial));
	CHECK(unrooted.ClassifyCurrentAssetReferences({ "active.AshTerrain" }) ==
		AshEditor::TerrainAssetReferenceMatch::Unsafe);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor Save and Optimize cannot target another Terrain asset")
{
	const std::filesystem::path root = TerrainEditorFilePath("bound-source-root");
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "terrain");
	const std::filesystem::path sourcePath = root / "terrain" / "source.AshTerrain";
	const std::filesystem::path otherPath = root / "terrain" / "other.AshTerrain";
	AshEngine::TerrainAssetSnapshot source = MakeEditorStrokeSnapshot();
	source.source_path = sourcePath;
	AshEngine::TerrainAssetSnapshot other = MakeEditorStrokeSnapshot();
	other.asset_id = 84u;
	other.source_path = otherPath;
	SaveTerrainEditorSnapshot(sourcePath, source);
	SaveTerrainEditorSnapshot(otherPath, other);
	const std::string otherBefore = ReadTerrainEditorText(otherPath);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, source);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(source));

	AshEditor::TerrainEditorIntent optimize = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Optimize);
	optimize.asset_path = "terrain/other.AshTerrain";
	CHECK_FALSE(service.SubmitIntent(optimize));
	CHECK(service.GetLastError().find("current Terrain") != std::string::npos);
	CHECK(ReadTerrainEditorText(otherPath) == otherBefore);

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Wrong Target";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	AshEditor::TerrainEditorIntent save = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save);
	save.asset_path = "terrain/other.AshTerrain";
	CHECK_FALSE(service.SubmitIntent(save));
	CHECK(service.GetLastError().find("current Terrain") != std::string::npos);
	CHECK(service.HasDirtyAssets());
	CHECK(ReadTerrainEditorText(otherPath) == otherBefore);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor Optimize rejects dirty state and runs only on a clean source")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("optimize-clean-source-root");
	const std::filesystem::path path = root / "optimize-clean-source.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	AshEditor::TerrainEditorIntent optimize = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Optimize);
	REQUIRE(service.SubmitIntent(optimize));
	REQUIRE(WaitForTerrainFileOperation(service));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Dirty";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	CHECK_FALSE(service.SubmitIntent(optimize));
	CHECK(service.GetLastError().find("dirty") != std::string::npos);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file operation blocks asset replacement after history quarantine")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("file-operation-quarantine-root");
	const std::filesystem::path path = root / "active.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	FailedRollbackTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	TerrainEditorWorkerBlocker blocker{};
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Optimize)));
	REQUIRE(service.HasFileOperationInProgress());

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Quarantined while optimize runs";
	CHECK_FALSE(service.SubmitIntent(rename));
	REQUIRE(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	AshEngine::TerrainAssetSnapshot replacement = MakeEditorStrokeSnapshot();
	replacement.asset_id = initial.asset_id + 2u;
	replacement.source_path = root / "replacement.AshTerrain";
	CHECK_FALSE(service.OpenSnapshotForAuthoring(replacement));
	CHECK(service.GetSelectedAssetId() == initial.asset_id);

	AshEditor::TerrainEditorIntent select = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SelectAsset);
	select.asset_id = initial.asset_id + 1u;
	CHECK_FALSE(service.SubmitIntent(select));
	CHECK(service.GetLastError().find("file operation") != std::string::npos);
	CHECK(service.GetSelectedAssetId() == initial.asset_id);
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetWorkingSet()->asset_id == initial.asset_id);

	blocker.Release();
	REQUIRE(WaitForTerrainFileOperation(service));
	CHECK(service.GetSelectedAssetId() == initial.asset_id);
	select.asset_id = 0u;
	REQUIRE(service.SubmitIntent(select));
	CHECK(service.GetWorkingSet() == nullptr);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor undo invalidates pending composition and publishes the undo generation")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	REQUIRE(commands.UndoLatest(context));
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 2u);
	CHECK(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());

	service.Update();
	REQUIRE(service.GetPublishedSnapshot() != nullptr);
	CHECK(service.GetPublishedSnapshot()->content_generation == initialGeneration + 2u);
	CHECK(service.GetWorkingSet()->dirty_components.empty());
}

TEST_CASE("Terrain editor blocks command replay while a stroke is active")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	const uint64_t editedGeneration = service.GetWorkingSet()->content_generation;
	REQUIRE(SubmitConfiguredBeginStroke(service));

	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	CHECK_FALSE(commands.UndoLatest(context));
	CHECK(service.GetWorkingSet()->content_generation == editedGeneration);
	CHECK(service.GetPreviewState().stroke_active);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
}

TEST_CASE("Terrain editor records one already-executed layer command and tracks stable selection")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	const AshEngine::TerrainLayerId original_id = MakeEditorStrokeLayerId();
	REQUIRE(service.GetSelectedLayerId() == original_id);
	const uint64_t initial_generation = service.GetWorkingSet()->content_generation;

	AshEditor::TerrainEditorIntent add = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Add);
	add.layer_action.name = "Detail";
	add.layer_action.blend_mode = AshEngine::TerrainHeightBlendMode::Alpha;
	add.layer_action.destination_index = 1u;
	REQUIRE(service.SubmitIntent(add));
	CHECK(commands.execute_count == 0u);
	CHECK(commands.record_count == 1u);
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetWorkingSet()->content_generation == initial_generation + 1u);
	REQUIRE(service.GetWorkingSet()->edit_layers.size() == 2u);
	const AshEngine::TerrainLayerId added_id = service.GetSelectedLayerId();
	CHECK(added_id.is_valid());
	CHECK(added_id != original_id);
	CHECK(service.GetWorkingSet()->edit_layers[1].id == added_id);
	CHECK(service.HasPendingComposition());

	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	REQUIRE(commands.UndoLatest(context));
	REQUIRE(service.GetWorkingSet()->edit_layers.size() == 1u);
	CHECK(service.GetSelectedLayerId() == original_id);
	CHECK(service.GetWorkingSet()->content_generation == initial_generation + 2u);
	REQUIRE(commands.RedoLatest(context));
	REQUIRE(service.GetWorkingSet()->edit_layers.size() == 2u);
	CHECK(service.GetSelectedLayerId() == added_id);
	CHECK(service.GetWorkingSet()->content_generation == initial_generation + 3u);

	service.Update();
	REQUIRE(service.GetPublishedSnapshot() != nullptr);
	CHECK(service.GetPublishedSnapshot()->content_generation == initial_generation + 3u);
}

TEST_CASE("Terrain editor selects layers by stable id and strokes only the selected layer")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorLayerSelectionSnapshot(true)));
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;
	const AshEngine::TerrainLayerId firstId = MakeEditorStrokeLayerId();
	const AshEngine::TerrainLayerId secondId = MakeEditorLayerId(48u);
	const AshEngine::TerrainLayerId lockedId = MakeEditorLayerId(49u);
	REQUIRE(service.GetSelectedLayerId() == firstId);

	AshEditor::TerrainEditorIntent intentMismatch = MakeBeginStrokeIntent();
	intentMismatch.layer_id = secondId;
	CHECK_FALSE(service.SubmitIntent(intentMismatch));
	AshEditor::TerrainEditorIntent brushMismatch = MakeBeginStrokeIntent();
	brushMismatch.brush.layer_id = secondId;
	CHECK_FALSE(service.SubmitIntent(brushMismatch));

	AshEditor::TerrainEditorIntent beginSecond = MakeBeginStrokeIntent();
	beginSecond.layer_id = secondId;
	beginSecond.brush.layer_id = secondId;
	CHECK_FALSE(SubmitConfiguredBeginStroke(service, beginSecond));
	if (service.GetPreviewState().stroke_active)
	{
		REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
	}

	REQUIRE(service.SubmitIntent(MakeSelectLayerIntent(lockedId)));
	CHECK(service.GetSelectedLayerId() == lockedId);
	CHECK(service.GetPreviewState().layer_locked);
	AshEditor::TerrainEditorIntent beginLocked = MakeBeginStrokeIntent();
	beginLocked.layer_id = lockedId;
	beginLocked.brush.layer_id = lockedId;
	CHECK_FALSE(service.SubmitIntent(beginLocked));

	REQUIRE(service.SubmitIntent(MakeSelectLayerIntent(secondId)));
	CHECK(service.GetSelectedLayerId() == secondId);
	CHECK_FALSE(service.GetPreviewState().layer_locked);
	REQUIRE(SubmitConfiguredBeginStroke(service, beginSecond));
	CHECK_FALSE(service.SubmitIntent(MakeSelectLayerIntent(firstId)));
	CHECK(service.GetSelectedLayerId() == secondId);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
	CHECK_FALSE(service.SubmitIntent(MakeSelectLayerIntent(MakeEditorLayerId(99u))));
	CHECK(service.GetSelectedLayerId() == secondId);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration);
	CHECK(commands.record_count == 0u);
	CHECK_FALSE(service.HasPendingComposition());
}

TEST_CASE("Terrain editor layer history restores the exact selection before a non-adjacent insertion")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorLayerSelectionSnapshot()));
	const AshEngine::TerrainLayerId selectedBefore = MakeEditorLayerId(49u);
	REQUIRE(service.SubmitIntent(MakeSelectLayerIntent(selectedBefore)));
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	AshEditor::TerrainEditorIntent add = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Add);
	add.layer_action.name = "Inserted";
	add.layer_action.destination_index = 0u;
	REQUIRE(service.SubmitIntent(add));
	const AshEngine::TerrainLayerId selectedAfter = service.GetSelectedLayerId();
	REQUIRE(selectedAfter.is_valid());
	CHECK(selectedAfter != selectedBefore);
	CHECK(service.GetWorkingSet()->edit_layers.front().id == selectedAfter);

	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	REQUIRE(commands.UndoLatest(context));
	CHECK(service.GetSelectedLayerId() == selectedBefore);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 2u);
	REQUIRE(commands.RedoLatest(context));
	CHECK(service.GetSelectedLayerId() == selectedAfter);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 3u);
}

TEST_CASE("Terrain editor duplicate history restores selection independent of insertion index")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorLayerSelectionSnapshot()));
	const AshEngine::TerrainLayerId selectedBefore = MakeEditorLayerId(48u);
	REQUIRE(service.SubmitIntent(MakeSelectLayerIntent(selectedBefore)));
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	AshEditor::TerrainEditorIntent duplicate = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Duplicate,
		MakeEditorStrokeLayerId());
	duplicate.layer_action.name = "First copy";
	duplicate.layer_action.destination_index = 2u;
	REQUIRE(service.SubmitIntent(duplicate));
	const AshEngine::TerrainLayerId selectedAfter = service.GetSelectedLayerId();
	REQUIRE(selectedAfter.is_valid());
	CHECK(service.GetWorkingSet()->edit_layers[2].id == selectedAfter);

	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	REQUIRE(commands.UndoLatest(context));
	CHECK(service.GetSelectedLayerId() == selectedBefore);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 2u);
	REQUIRE(commands.RedoLatest(context));
	CHECK(service.GetSelectedLayerId() == selectedAfter);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 3u);
}

TEST_CASE("Terrain editor treats an idempotent layer action as success without recording history")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	const uint64_t initial_generation = service.GetWorkingSet()->content_generation;
	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename,
		MakeEditorStrokeLayerId());
	rename.layer_action.name = "Sculpt";
	REQUIRE(service.SubmitIntent(rename));
	CHECK(commands.execute_count == 0u);
	CHECK(commands.record_count == 0u);
	CHECK(service.GetWorkingSet()->content_generation == initial_generation);
	CHECK(service.GetSelectedLayerId() == MakeEditorStrokeLayerId());
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetLastError().empty());
}

TEST_CASE("Terrain editor lock state blocks strokes and layer actions cannot interrupt an active stroke")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	AshEngine::TerrainAssetSnapshot snapshot = MakeEditorStrokeSnapshot();
	auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(
		*snapshot.edit_layers);
	layers->front().locked = true;
	snapshot.edit_layers = std::move(layers);
	REQUIRE(service.OpenSnapshotForAuthoring(snapshot));
	CHECK(service.GetSelectedLayerId() == MakeEditorStrokeLayerId());
	CHECK(service.GetPreviewState().layer_locked);
	CHECK_FALSE(SubmitConfiguredBeginStroke(service));
	CHECK(commands.record_count == 0u);

	AshEditor::TerrainEditorIntent unlock = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::SetLocked,
		MakeEditorStrokeLayerId());
	unlock.layer_action.flag_value = false;
	REQUIRE(service.SubmitIntent(unlock));
	CHECK_FALSE(service.GetPreviewState().layer_locked);
	REQUIRE(SubmitConfiguredBeginStroke(service));
	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename,
		MakeEditorStrokeLayerId());
	rename.layer_action.name = "Blocked rename";
	CHECK_FALSE(service.SubmitIntent(rename));
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Sculpt");
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
}

TEST_CASE("Terrain editor layer history rejection rolls back or quarantines unverifiable mutation")
{
	RejectingTerrainCommandExecutor rejecting{};
	AshEditor::TerrainEditorService rolled_back{};
	REQUIRE(rolled_back.Initialize(rejecting));
	REQUIRE(rolled_back.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &rolled_back;
	rejecting.p_context = &context;
	const uint64_t initial_generation = rolled_back.GetWorkingSet()->content_generation;
	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename,
		MakeEditorStrokeLayerId());
	rename.layer_action.name = "Rejected";
	CHECK_FALSE(rolled_back.SubmitIntent(rename));
	CHECK(rejecting.rollback_succeeded);
	CHECK(rolled_back.GetWorkingSet()->edit_layers.front().name == "Sculpt");
	CHECK(rolled_back.GetWorkingSet()->content_generation == initial_generation + 2u);
	CHECK(rolled_back.HasPendingComposition());
	CHECK(rolled_back.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);

	MalformedRollbackTerrainCommandExecutor malformed{};
	AshEditor::TerrainEditorService quarantined{};
	REQUIRE(quarantined.Initialize(malformed));
	REQUIRE(quarantined.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	CHECK_FALSE(quarantined.SubmitIntent(rename));
	CHECK(quarantined.GetWorkingSet()->edit_layers.front().name == "Rejected");
	CHECK_FALSE(quarantined.HasPendingComposition());
	CHECK(quarantined.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	CHECK(quarantined.HasBlockingOperation());
	CHECK_FALSE(quarantined.SubmitIntent(rename));
}

TEST_CASE("Terrain editor rejected layer history restores selection and lock preview")
{
	RejectingTerrainCommandExecutor rejecting{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(rejecting));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorLayerSelectionSnapshot(true)));
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	rejecting.p_context = &context;
	const AshEngine::TerrainLayerId selectedBefore = MakeEditorLayerId(49u);
	REQUIRE(service.SubmitIntent(MakeSelectLayerIntent(selectedBefore)));
	REQUIRE(service.GetPreviewState().layer_locked);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	AshEditor::TerrainEditorIntent add = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Add);
	add.layer_action.name = "Rejected insertion";
	add.layer_action.destination_index = 0u;
	CHECK_FALSE(service.SubmitIntent(add));
	CHECK(rejecting.rollback_succeeded);
	CHECK(service.GetWorkingSet()->edit_layers.size() == 3u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 2u);
	CHECK(service.GetSelectedLayerId() == selectedBefore);
	CHECK(service.GetPreviewState().layer_locked);
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	CHECK(service.HasPendingComposition());
}

TEST_CASE("Terrain editor quarantines a layer rollback that restores data but not selection")
{
	WrongSelectionRollbackTerrainCommandExecutor executor{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(executor));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorLayerSelectionSnapshot(true)));
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	executor.p_context = &context;
	executor.wrong_selection = MakeEditorStrokeLayerId();
	REQUIRE(service.SubmitIntent(MakeSelectLayerIntent(MakeEditorLayerId(49u))));
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	AshEditor::TerrainEditorIntent add = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Add);
	add.layer_action.name = "Rejected insertion";
	add.layer_action.destination_index = 0u;
	CHECK_FALSE(service.SubmitIntent(add));
	CHECK(executor.rollback_succeeded);
	CHECK(executor.selection_changed);
	CHECK(service.GetWorkingSet()->edit_layers.size() == 3u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 2u);
	CHECK(service.GetSelectedLayerId() == executor.wrong_selection);
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	CHECK_FALSE(service.SubmitIntent(MakeSelectLayerIntent(MakeEditorLayerId(48u))));
}

TEST_CASE("Terrain editor quarantines a malformed rollback claim")
{
	MalformedRollbackTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	const auto initialPublishedSnapshot = service.GetPublishedSnapshot();

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	CHECK(commands.record_count == 1u);
	CHECK(commands.command_received);
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	service.Update();
	CHECK(service.GetPublishedSnapshot() == initialPublishedSnapshot);
}

TEST_CASE("Terrain editor quarantines a history recording exception")
{
	ThrowingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	const auto initialPublishedSnapshot = service.GetPublishedSnapshot();

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	CHECK(commands.record_count == 1u);
	CHECK(commands.command_received);
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	service.Update();
	CHECK(service.GetPublishedSnapshot() == initialPublishedSnapshot);
}

TEST_CASE("Terrain editor rejects incompatible layers and non-ready stroke state")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot(
		AshEngine::TerrainHeightBlendMode::Alpha)));
	CHECK_FALSE(SubmitConfiguredBeginStroke(service));

	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	AshEditor::TerrainEditorIntent smooth = MakeBeginStrokeIntent();
	smooth.brush.tool = AshEngine::TerrainBrushTool::Smooth;
	CHECK_FALSE(SubmitConfiguredBeginStroke(service, smooth));

	AshEditor::TerrainEditorSessionCore core{};
	AshEngine::TerrainWorkingSet workingSet{};
	std::string error{};
	REQUIRE(AshEngine::make_terrain_working_set(
		MakeEditorStrokeSnapshot(),
		workingSet,
		&error));
	REQUIRE(core.Open(std::move(workingSet)));
	core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Pending);
	CHECK_FALSE(core.BeginStroke(1u));
	core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Outside);
	CHECK_FALSE(core.BeginStroke(1u));
	core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
	CHECK_FALSE(core.BeginStroke(1u));
}

TEST_CASE("Terrain viewport preview stays independent from authoring session readiness")
{
	AshEditor::TerrainEditorSessionCore core{};
	AshEngine::TerrainWorkingSet workingSet{};
	std::string error{};
	REQUIRE(AshEngine::make_terrain_working_set(
		MakeEditorStrokeSnapshot(),
		workingSet,
		&error));
	REQUIRE(core.Open(std::move(workingSet)));

	AshEditor::TerrainViewportPreviewState viewport{};
	viewport.query_status = AshEngine::TerrainQueryStatus::Ready;
	viewport.center_ws = { 12.0f, 3.0f, -5.0f };
	viewport.normal_ws = { 0.0f, 2.0f, 0.0f };
	viewport.radius_meters = 24.0f;
	viewport.terrain_entity_id = 91u;
	viewport.has_world_position = true;
	REQUIRE(core.SetViewportPreview(viewport));

	const AshEditor::TerrainEditorPreviewState& ready = core.GetPreviewState();
	CHECK(ready.query_status == AshEngine::TerrainQueryStatus::Ready);
	CHECK(ready.viewport.query_status == AshEngine::TerrainQueryStatus::Ready);
	CHECK(ready.viewport.has_world_position);
	CHECK(ready.viewport.center_ws.x == doctest::Approx(12.0f));
	CHECK(ready.viewport.normal_ws.y == doctest::Approx(1.0f));
	CHECK(ready.viewport.radius_meters == doctest::Approx(24.0f));

	viewport.query_status = AshEngine::TerrainQueryStatus::Pending;
	viewport.center_ws = { -100.0f, -200.0f, -300.0f };
	viewport.normal_ws = { 1.0f, 0.0f, 0.0f };
	viewport.radius_meters = 12.0f;
	viewport.terrain_entity_id = 999u;
	REQUIRE(core.SetViewportPreview(viewport));
	CHECK(core.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	CHECK(core.GetPreviewState().viewport.query_status == AshEngine::TerrainQueryStatus::Pending);
	CHECK(core.GetPreviewState().viewport.center_ws.x == doctest::Approx(12.0f));
	CHECK(core.GetPreviewState().viewport.normal_ws.y == doctest::Approx(1.0f));
	CHECK(core.GetPreviewState().viewport.terrain_entity_id == 91u);
	CHECK(core.GetPreviewState().viewport.radius_meters == doctest::Approx(12.0f));

	viewport.query_status = AshEngine::TerrainQueryStatus::Ready;
	viewport.normal_ws = {};
	CHECK_FALSE(core.SetViewportPreview(viewport));
	CHECK(core.GetPreviewState().viewport.query_status == AshEngine::TerrainQueryStatus::Pending);
	viewport.normal_ws = { 0.0f, 1.0f, 0.0f };
	viewport.terrain_entity_id = 0u;
	CHECK_FALSE(core.SetViewportPreview(viewport));
	CHECK(core.GetPreviewState().viewport.query_status == AshEngine::TerrainQueryStatus::Pending);
	viewport.terrain_entity_id = 91u;
	viewport.center_ws.x = std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(core.SetViewportPreview(viewport));
	CHECK(core.GetPreviewState().viewport.query_status == AshEngine::TerrainQueryStatus::Pending);
	viewport.query_status = AshEngine::TerrainQueryStatus::Outside;
	REQUIRE(core.SetViewportPreview(viewport));
	CHECK(core.GetPreviewState().viewport.query_status == AshEngine::TerrainQueryStatus::Outside);
	CHECK(core.GetPreviewState().viewport.terrain_entity_id == 0u);
	CHECK_FALSE(core.GetPreviewState().viewport.has_world_position);

	core.SetPreviewQueryStatus(AshEngine::TerrainQueryStatus::Failed);
	CHECK(core.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	CHECK(core.GetPreviewState().viewport.query_status == AshEngine::TerrainQueryStatus::Outside);
	CHECK_FALSE(core.GetPreviewState().viewport.has_world_position);
}

TEST_CASE("Terrain editor service validates viewport preview ownership and owns brush radius")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));

	AshEditor::TerrainViewportPreviewState viewport{};
	viewport.query_status = AshEngine::TerrainQueryStatus::Ready;
	viewport.center_ws = { 1.0f, 2.0f, 3.0f };
	viewport.normal_ws = { 0.0f, 1.0f, 0.0f };
	viewport.radius_meters = 999.0f;
	viewport.terrain_entity_id = 17u;
	viewport.has_world_position = true;
	CHECK_FALSE(service.SetViewportPreview(83u, viewport));
	CHECK_FALSE(service.GetPreviewState().viewport.has_world_position);

	AshEditor::TerrainEditorIntent configure{};
	configure.kind = AshEditor::TerrainEditorIntent::Kind::ConfigureAuthoring;
	configure.mode = AshEditor::TerrainEditorMode::Sculpt;
	configure.brush = MakeBeginStrokeIntent().brush;
	REQUIRE(service.SubmitIntent(configure));
	CHECK_FALSE(service.SetViewportPreview(999u, viewport));
	CHECK_FALSE(service.GetPreviewState().viewport.has_world_position);
	REQUIRE(service.SetViewportPreview(83u, viewport));
	CHECK(service.GetPreviewState().viewport.has_world_position);
	CHECK(service.GetPreviewState().viewport.terrain_entity_id == 17u);
	CHECK(service.GetPreviewState().viewport.radius_meters == doctest::Approx(
		service.GetAuthoringConfig().brush.radius_meters));

	configure.brush.radius_meters = 4.5f;
	REQUIRE(service.SubmitIntent(configure));
	CHECK(service.GetPreviewState().viewport.radius_meters == doctest::Approx(4.5f));
	configure.mode = AshEditor::TerrainEditorMode::Manage;
	REQUIRE(service.SubmitIntent(configure));
	CHECK(service.GetPreviewState().viewport.query_status == AshEngine::TerrainQueryStatus::Outside);
	CHECK(service.GetPreviewState().viewport.terrain_entity_id == 0u);
	CHECK_FALSE(service.GetPreviewState().viewport.has_world_position);
}

TEST_CASE("Terrain editor invalid raw samples fail without mutation or history")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	AshEditor::TerrainEditorIntent invalidSample = MakeStrokeSampleIntent(2.0f, 2.0f);
	invalidSample.stroke_sample.pressure = std::numeric_limits<float>::quiet_NaN();
	REQUIRE(service.SubmitIntent(invalidSample));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	CHECK(commands.record_count == 0u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
	CHECK_FALSE(service.HasPendingComposition());
	CHECK_FALSE(service.GetPreviewState().stroke_active);
}

TEST_CASE("Terrain editor publication failure preserves the edited working set")
{
	AshEngine::AssetDatabase invalidAssets{};
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(invalidAssets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;
	const auto initialComponents = service.GetWorkingSet()->components;
	REQUIRE(service.GetPublishedSnapshot() != nullptr);
	const auto initialPublishedSnapshot = service.GetPublishedSnapshot();

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	REQUIRE_FALSE(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
	const auto editedBlocks = service.GetWorkingSet()->edit_layers.front().height_blocks;
	const auto expectedDirty = service.GetWorkingSet()->dirty_components;
	const uint64_t editedGeneration = service.GetWorkingSet()->content_generation;

	service.Update();

	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 1u);
	CHECK(service.GetWorkingSet()->content_generation == editedGeneration);
	CHECK(service.GetWorkingSet()->dirty_components == expectedDirty);
	CHECK(service.GetWorkingSet()->components == initialComponents);
	CHECK(EqualHeightBlocks(
		service.GetWorkingSet()->edit_layers.front().height_blocks,
		editedBlocks));
	CHECK(service.GetPublishedSnapshot() == initialPublishedSnapshot);
	CHECK(service.GetPublishedSnapshot()->content_generation == initialGeneration);
	CHECK_FALSE(service.HasPendingComposition());
	CHECK_FALSE(service.GetLastError().empty());
}

TEST_CASE("Terrain editor history rejection publishes the rollback generation")
{
	RejectingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	commands.p_context = &context;
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	CHECK(commands.record_count == 1u);
	CHECK(commands.rollback_succeeded);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 2u);
	CHECK(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
	CHECK(service.HasPendingComposition());

	service.Update();
	REQUIRE(service.GetPublishedSnapshot() != nullptr);
	CHECK(service.GetPublishedSnapshot()->content_generation == initialGeneration + 2u);
	CHECK(service.GetWorkingSet()->dirty_components.empty());
}

TEST_CASE("Terrain editor redo supersedes pending undo publication")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	const auto editedBlocks = service.GetWorkingSet()->edit_layers.front().height_blocks;
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	REQUIRE(commands.UndoLatest(context));
	REQUIRE(commands.RedoLatest(context));

	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 3u);
	CHECK(EqualHeightBlocks(
		service.GetWorkingSet()->edit_layers.front().height_blocks,
		editedBlocks));
	CHECK(service.HasPendingComposition());

	service.Update();
	REQUIRE(service.GetPublishedSnapshot() != nullptr);
	CHECK(service.GetPublishedSnapshot()->content_generation == initialGeneration + 3u);
	CHECK(service.GetWorkingSet()->dirty_components.empty());
}

TEST_CASE("Terrain editor stale sequence intents do not mutate the active stroke")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;
	REQUIRE(SubmitConfiguredBeginStroke(service));

	AshEditor::TerrainEditorIntent staleSample = MakeStrokeSampleIntent(2.0f, 2.0f);
	staleSample.sequence = 999u;
	CHECK_FALSE(service.SubmitIntent(staleSample));
	CHECK(service.GetPreviewState().stroke_active);
	AshEditor::TerrainEditorIntent staleEnd = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke);
	staleEnd.sequence = 999u;
	CHECK_FALSE(service.SubmitIntent(staleEnd));
	CHECK(service.GetPreviewState().stroke_active);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));

	CHECK(service.GetWorkingSet()->content_generation == initialGeneration);
	CHECK(service.GetWorkingSet()->dirty_components.empty());
	CHECK(commands.record_count == 0u);
}

TEST_CASE("Terrain editor quarantines a mutation when history rollback fails")
{
	FailedRollbackTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;
	const auto initialPublishedSnapshot = service.GetPublishedSnapshot();

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	CHECK(commands.record_count == 1u);
	CHECK(commands.command_received);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 1u);
	CHECK_FALSE(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	CHECK_FALSE(SubmitConfiguredBeginStroke(service));

	service.Update();
	CHECK(service.GetPublishedSnapshot() == initialPublishedSnapshot);
	CHECK_FALSE(service.GetWorkingSet()->dirty_components.empty());

	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
}

TEST_CASE("Terrain external modification clean reloads at an Editor update boundary")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-clean-reload-root");
	const std::filesystem::path path = root / "clean.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 4u, "External Clean");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	CHECK(service.GetWorkingSet()->content_generation == initial.content_generation);
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "External Clean");
	CHECK_FALSE(service.HasDirtyAssets());
	REQUIRE(commands.removed_terrain_asset_ids.size() == 1u);
	CHECK(commands.removed_terrain_asset_ids.front() == initial.asset_id);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain external file polling queues a clean reload without a frame-count rule")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-auto-reload-root");
	const std::filesystem::path path = root / "auto.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 2u, "Auto Reloaded");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline &&
		service.GetWorkingSet()->content_generation != external.content_generation)
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Auto Reloaded");
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	REQUIRE(commands.removed_terrain_asset_ids.size() == 1u);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain source disappearance enters a recoverable read-only failure")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-missing-source-root");
	const std::filesystem::path path = root / "missing.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	const auto publishedBeforeFailure = service.GetPublishedSnapshot();

	REQUIRE(std::filesystem::remove(path));
	const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < failureDeadline &&
		service.GetExternalChangeState().status != AshEditor::TerrainExternalChangeStatus::Failed)
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	const AshEditor::TerrainExternalChangeState& failed = service.GetExternalChangeState();
	CHECK(failed.status == AshEditor::TerrainExternalChangeStatus::Failed);
	CHECK(failed.read_only);
	CHECK(failed.can_repair);
	CHECK(failed.can_save_as);
	CHECK(failed.diagnostic == "Terrain source asset is missing on disk.");
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == initial.content_generation);
	CHECK(service.GetPublishedSnapshot() == publishedBeforeFailure);
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);

	AshEngine::TerrainAssetSnapshot repaired = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 1u, "Repaired Source");
	repaired.source_path = path;
	SaveTerrainEditorSnapshot(path, repaired);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Repair)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == repaired.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Repaired Source");
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain reload refuses to start without a stable source write time")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("reload-source-time-root");
	const std::filesystem::path path = root / "source-time.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	const auto published = service.GetPublishedSnapshot();

	REQUIRE(std::filesystem::remove(path));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	CHECK(service.GetWorkingSet()->content_generation == initial.content_generation);
	CHECK(service.GetPublishedSnapshot() == published);
	CHECK(service.GetLastError().find("write time") != std::string::npos);

	AshEngine::TerrainAssetSnapshot restored = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 1u, "Stable Retry");
	restored.source_path = path;
	SaveTerrainEditorSnapshot(path, restored);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	CHECK(service.GetWorkingSet()->content_generation == restored.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Stable Retry");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain source polling debounces one transient missing-file observation")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("transient-missing-source-root");
	const std::filesystem::path path = root / "transient.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	const auto published = service.GetPublishedSnapshot();

	REQUIRE(std::filesystem::remove(path));
	const auto onePollDeadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(650);
	while (std::chrono::steady_clock::now() < onePollDeadline)
	{
		service.Update();
		CHECK(service.GetExternalChangeState().status !=
			AshEditor::TerrainExternalChangeStatus::Failed);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(service.GetPublishedSnapshot() == published);

	AshEngine::TerrainAssetSnapshot restored = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 1u, "Atomic Replace Restored");
	restored.source_path = path;
	SaveTerrainEditorSnapshot(path, restored);
	const auto reloadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < reloadDeadline &&
		service.GetWorkingSet()->content_generation != restored.content_generation)
	{
		service.Update();
		CHECK(service.GetExternalChangeState().status !=
			AshEditor::TerrainExternalChangeStatus::Failed);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(service.GetWorkingSet()->content_generation == restored.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Atomic Replace Restored");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain reload observes a second disk write that arrives after candidate loading")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-reload-race-root");
	const std::filesystem::path path = root / "race.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEngine::TerrainAssetSnapshot candidateA = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 1u, "Candidate A");
	candidateA.source_path = path;
	SaveTerrainEditorSnapshot(path, candidateA);
	const auto candidateATime = std::filesystem::file_time_type::clock::now() -
		std::chrono::seconds(4);
	std::filesystem::last_write_time(path, candidateATime);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));

	std::promise<void> candidateFinishedPromise{};
	auto candidateFinished = candidateFinishedPromise.get_future();
	auto candidateBarrier = AshEngine::dispatch_background_task(
		"TerrainEditorServiceTests::CandidateBarrier",
		[finished = std::move(candidateFinishedPromise)]() mutable
		{
			finished.set_value();
		});
	REQUIRE(candidateFinished.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	REQUIRE(candidateBarrier.valid());
	candidateBarrier.wait();

	AshEngine::TerrainAssetSnapshot candidateB = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 2u, "Candidate B");
	candidateB.source_path = path;
	SaveTerrainEditorSnapshot(path, candidateB);
	const auto candidateBTime = std::filesystem::file_time_type::clock::now();
	std::filesystem::last_write_time(path, candidateBTime);

	const auto applyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < applyDeadline &&
		(service.GetWorkingSet()->content_generation != candidateB.content_generation ||
		 service.GetExternalChangeState().status != AshEditor::TerrainExternalChangeStatus::None))
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(service.GetWorkingSet()->content_generation == candidateB.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Candidate B");
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	CHECK(commands.removed_terrain_asset_ids.size() == 2u);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain external modification keeps dirty local bytes until an explicit choice")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-dirty-conflict-root");
	const std::filesystem::path path = root / "dirty.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Unsaved Local";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	const uint64_t localGeneration = service.GetWorkingSet()->content_generation;
	const auto localPublished = service.GetPublishedSnapshot();

	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, localGeneration + 6u, "External Dirty");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));
	CHECK(service.HasDirtyAssets());
	CHECK(service.GetWorkingSet()->content_generation == localGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Unsaved Local");
	CHECK(service.GetPublishedSnapshot() == localPublished);
	CHECK(service.GetExternalChangeState().local_generation == localGeneration);
	CHECK(service.GetExternalChangeState().disk_generation == external.content_generation);
	CHECK(commands.removed_terrain_asset_ids.empty());
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> globallyPublished{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, globallyPublished));
	REQUIRE(globallyPublished);
	REQUIRE(globallyPublished->edit_layers);
	CHECK(globallyPublished->content_generation == localGeneration);
	CHECK(globallyPublished->edit_layers->front().name == "Unsaved Local");

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::KeepLocal)));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation + 1u);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Unsaved Local");
	CHECK(service.HasPendingComposition());
	service.Update();
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.HasDirtyAssets());
	CHECK(commands.removed_terrain_asset_ids.empty());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain Keep Local observes a second disk write made while conflict UI is open")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("conflict-second-write-root");
	const std::filesystem::path path = root / "second-write.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Local Survives A And B";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	AshEngine::TerrainAssetSnapshot candidateA = MakeExternalTerrainGeneration(
		initial, service.GetWorkingSet()->content_generation + 4u, "Candidate A");
	candidateA.source_path = path;
	SaveTerrainEditorSnapshot(path, candidateA);
	const auto candidateATime = std::filesystem::file_time_type::clock::now() -
		std::chrono::seconds(4);
	std::filesystem::last_write_time(path, candidateATime);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));

	AshEngine::TerrainAssetSnapshot candidateB = MakeExternalTerrainGeneration(
		initial, candidateA.content_generation + 1u, "Candidate B");
	candidateB.source_path = path;
	SaveTerrainEditorSnapshot(path, candidateB);
	std::filesystem::last_write_time(
		path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(4));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::KeepLocal)));
	const uint64_t keptLocalGeneration = service.GetWorkingSet()->content_generation;
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Local Survives A And B");

	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict,
		std::chrono::seconds(3)));
	CHECK(service.GetExternalChangeState().disk_generation == candidateB.content_generation);
	CHECK(service.GetWorkingSet()->content_generation == keptLocalGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Local Survives A And B");
	CHECK(service.HasDirtyAssets());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain recovered candidate uses the rejected generation for conflict ordering")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-recovered-order-root");
	const std::filesystem::path path = root / "recovered-order.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 5u, "Observed Five");
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Unsaved Six";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	REQUIRE(service.GetWorkingSet()->content_generation == 6u);

	AshEngine::TerrainAssetSnapshot previous = MakeExternalTerrainGeneration(
		initial, 4u, "Recovered Four");
	previous.source_path = path;
	SaveTerrainEditorSnapshot(path, previous);
	const AshEngine::TerrainAssetSnapshot rejected = AppendChangedTerrainGeneration(
		path, previous, 6u, "Rejected Six");
	CorruptNewestTerrainIndexDescriptor(path);

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));
	CHECK(service.GetWorkingSet()->content_generation == 6u);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Unsaved Six");
	CHECK(service.GetExternalChangeState().disk_generation == rejected.content_generation);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::KeepLocal)));
	CHECK(service.GetWorkingSet()->content_generation == 7u);
	CHECK(service.HasDirtyAssets());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain external modification Reload discards only after conflict confirmation")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-reload-discard-root");
	const std::filesystem::path path = root / "discard.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Discard Me";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, service.GetWorkingSet()->content_generation + 4u, "Disk Wins");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Disk Wins");
	CHECK_FALSE(service.HasDirtyAssets());
	REQUIRE(commands.removed_terrain_asset_ids.size() == 1u);
	CHECK(commands.removed_terrain_asset_ids.front() == initial.asset_id);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain conflict SaveAs preserves local copy before applying the disk candidate")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-save-as-root");
	const std::filesystem::path path = root / "source.AshTerrain";
	const std::filesystem::path rescue = root / "local-rescue.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Rescued Local";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, service.GetWorkingSet()->content_generation + 4u, "Disk Candidate");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));

	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = rescue;
	REQUIRE(service.SubmitIntent(saveAs));
	REQUIRE(service.HasFileOperationInProgress());
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	CHECK(service.HasFileOperationInProgress());
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::Conflict);
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::KeepLocal)));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::Conflict);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Rescued Local");
	REQUIRE(WaitForTerrainFileOperation(service));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Disk Candidate");

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> rescued{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(rescue, rescued, nullptr, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(rescued);
	REQUIRE(rescued->edit_layers);
	CHECK(rescued->edit_layers->front().name == "Rescued Local");
	REQUIRE(commands.removed_terrain_asset_ids.size() == 1u);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain asset selection bypasses a stale shared cache before observing source time")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("selection-stale-cache-root");
	const std::filesystem::path path = root / "selected.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> cached{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, cached));
	REQUIRE(cached);
	CHECK(cached->content_generation == initial.content_generation);

	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 4u, "Selected From Disk");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);

	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	AshEditor::TerrainEditorIntent select{};
	select.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	select.asset_id = initial.asset_id;
	REQUIRE(service.SubmitIntent(select));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Selected From Disk");

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> published{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, published));
	REQUIRE(published);
	CHECK(published->content_generation == external.content_generation);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain asset selection keeps the clean source session until the valid target commits")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("selection-commit-boundary-root");
	const std::filesystem::path pathA = root / "terrain-a.AshTerrain";
	const std::filesystem::path pathB = root / "terrain-b.AshTerrain";
	AshEngine::TerrainAssetSnapshot terrainA = MakeEditorStrokeSnapshot();
	terrainA.source_path = pathA;
	SaveTerrainEditorSnapshot(pathA, terrainA);
	AshEngine::TerrainAssetSnapshot terrainB = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 2u, "Terrain B");
	terrainB.source_path = pathB;
	SaveTerrainEditorSnapshot(pathB, terrainB);

	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathA, terrainA);
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathB, terrainB);
	REQUIRE(terrainA.asset_id != terrainB.asset_id);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(terrainA));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Clean A With History";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	REQUIRE(WaitForTerrainFileOperation(service));
	REQUIRE_FALSE(service.HasDirtyAssets());
	REQUIRE(commands.commands.size() == 1u);
	const std::shared_ptr<const AshEngine::TerrainAssetSnapshot> publishedA =
		service.GetPublishedSnapshot();
	REQUIRE(publishedA);

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent selectB{};
	selectB.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	selectB.asset_id = terrainB.asset_id;
	REQUIRE(service.SubmitIntent(selectB));

	CHECK(service.GetSelectedAssetId() == terrainA.asset_id);
	CHECK(service.GetWorkingSet() != nullptr);
	if (service.GetWorkingSet())
	{
		CHECK(service.GetWorkingSet()->asset_id == terrainA.asset_id);
	}
	CHECK(service.GetPublishedSnapshot() == publishedA);
	CHECK(commands.removed_terrain_asset_ids.empty());

	blocker.Release();
	const auto commitDeadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < commitDeadline &&
		(service.GetSelectedAssetId() != terrainB.asset_id || service.HasBlockingOperation()))
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE_FALSE(service.HasBlockingOperation());
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetSelectedAssetId() == terrainB.asset_id);
	CHECK(service.GetWorkingSet()->asset_id == terrainB.asset_id);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Terrain B");
	CHECK(commands.removed_terrain_asset_ids ==
		std::vector<uint64_t>{ terrainA.asset_id });
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain uncached selection rejects a candidate from a refreshed catalog lineage")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("selection-uncached-refresh-root");
	const std::filesystem::path pathA = root / "terrain-a.AshTerrain";
	const std::filesystem::path pathB = root / "terrain-b.AshTerrain";
	AshEngine::TerrainAssetSnapshot terrainA = MakeEditorStrokeSnapshot();
	terrainA.source_path = pathA;
	SaveTerrainEditorSnapshot(pathA, terrainA);
	AshEngine::TerrainAssetSnapshot terrainB = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 2u, "Uncached Terrain B");
	terrainB.source_path = pathB;
	SaveTerrainEditorSnapshot(pathB, terrainB);

	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathA, terrainA);
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathB, terrainB);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(terrainA));
	const auto publishedA = service.GetPublishedSnapshot();
	REQUIRE(publishedA);

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent selectB{};
	selectB.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	selectB.asset_id = terrainB.asset_id;
	REQUIRE(service.SubmitIntent(selectB));
	REQUIRE(service.HasBlockingOperation());
	REQUIRE(assets.refresh());
	blocker.Release();
	const auto rejectedDeadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < rejectedDeadline &&
		service.HasBlockingOperation())
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE_FALSE(service.HasBlockingOperation());
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetSelectedAssetId() == terrainA.asset_id);
	CHECK(service.GetWorkingSet()->asset_id == terrainA.asset_id);
	CHECK(service.GetPublishedSnapshot() == publishedA);
	CHECK(service.GetLastError().find("publication") != std::string::npos);

	REQUIRE(service.SubmitIntent(selectB));
	const auto acceptedDeadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < acceptedDeadline &&
		(service.HasBlockingOperation() || service.GetSelectedAssetId() != terrainB.asset_id))
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetSelectedAssetId() == terrainB.asset_id);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Uncached Terrain B");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain asset selection failure preserves the clean source session and history")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("selection-failure-transaction-root");
	const std::filesystem::path pathA = root / "terrain-a.AshTerrain";
	const std::filesystem::path pathB = root / "terrain-b.AshTerrain";
	AshEngine::TerrainAssetSnapshot terrainA = MakeEditorStrokeSnapshot();
	terrainA.source_path = pathA;
	SaveTerrainEditorSnapshot(pathA, terrainA);
	AshEngine::TerrainAssetSnapshot terrainB = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 2u, "Broken Terrain B");
	terrainB.source_path = pathB;
	SaveTerrainEditorSnapshot(pathB, terrainB);
	{
		std::ofstream corrupt(pathB, std::ios::binary | std::ios::trunc);
		REQUIRE(corrupt.is_open());
		corrupt << "broken terrain container";
	}

	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathA, terrainA);
	const AshEngine::AssetInfo* terrainBInfo =
		assets.find_asset_by_path(pathB.lexically_relative(root));
	REQUIRE(terrainBInfo != nullptr);
	REQUIRE(terrainBInfo->type == AshEngine::AssetType::Terrain);
	const AshEngine::TerrainAssetId terrainBId = terrainBInfo->id;
	REQUIRE(terrainA.asset_id != terrainBId);

	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(terrainA));
	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Clean A Must Survive";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	REQUIRE(WaitForTerrainFileOperation(service));
	REQUIRE_FALSE(service.HasDirtyAssets());

	const std::shared_ptr<const AshEngine::TerrainAssetSnapshot> publishedA =
		service.GetPublishedSnapshot();
	REQUIRE(publishedA);
	const AshEditor::TerrainExternalChangeState externalA =
		service.GetExternalChangeState();
	const size_t historySize = commands.commands.size();
	const std::vector<uint64_t> removedBefore = commands.removed_terrain_asset_ids;

	AshEditor::TerrainEditorIntent selectB{};
	selectB.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	selectB.asset_id = terrainBId;
	REQUIRE(service.SubmitIntent(selectB));
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline && service.HasBlockingOperation())
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE_FALSE(service.HasBlockingOperation());

	CHECK(service.GetSelectedAssetId() == terrainA.asset_id);
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->asset_id == terrainA.asset_id);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Clean A Must Survive");
	CHECK(service.GetPublishedSnapshot() == publishedA);
	const AshEditor::TerrainExternalChangeState& externalAfter =
		service.GetExternalChangeState();
	CHECK(externalAfter.status == externalA.status);
	CHECK(externalAfter.local_generation == externalA.local_generation);
	CHECK(externalAfter.disk_generation == externalA.disk_generation);
	CHECK(externalAfter.diagnostic == externalA.diagnostic);
	CHECK(externalAfter.read_only == externalA.read_only);
	CHECK(commands.commands.size() == historySize);
	CHECK(commands.removed_terrain_asset_ids == removedBefore);
	CHECK_FALSE(service.GetLastError().empty());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain conflict SaveAs failure preserves the local session and remains retryable")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-save-as-failure-root");
	const std::filesystem::path path = root / "source.AshTerrain";
	const std::filesystem::path rescue = root / "local-rescue.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Local Retry Bytes";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	const uint64_t localGeneration = service.GetWorkingSet()->content_generation;
	const auto localPublished = service.GetPublishedSnapshot();
	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, localGeneration + 4u, "Disk After Retry");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = rescue;
	REQUIRE(service.SubmitIntent(saveAs));
	SaveTerrainEditorSnapshot(rescue, initial);
	blocker.Release();
	CHECK_FALSE(WaitForTerrainFileOperation(service));
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Failed);
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::Conflict);
	CHECK(service.GetWorkingSet()->content_generation == localGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Local Retry Bytes");
	CHECK(service.GetPublishedSnapshot() == localPublished);
	CHECK(service.HasDirtyAssets());
	CHECK(commands.removed_terrain_asset_ids.empty());

	REQUIRE(std::filesystem::remove(rescue));
	REQUIRE(service.SubmitIntent(saveAs));
	REQUIRE(WaitForTerrainFileOperation(service));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Disk After Retry");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain conflict SaveAs preserves recovered read-only diagnostics")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-save-as-recovery-root");
	const std::filesystem::path path = root / "source.AshTerrain";
	const std::filesystem::path rescue = root / "local-rescue.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Rescued Local";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();

	const AshEngine::TerrainAssetSnapshot rejected = AppendChangedTerrainGeneration(
		path, initial, initial.content_generation + 1u, "Rejected Disk Candidate");
	CorruptNewestTerrainIndexDescriptor(path);

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));
	AshEditor::TerrainEditorIntent saveAs = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::SaveAs);
	saveAs.asset_path = rescue;
	REQUIRE(service.SubmitIntent(saveAs));
	REQUIRE(WaitForTerrainFileOperation(service));

	const AshEditor::TerrainExternalChangeState& state = service.GetExternalChangeState();
	CHECK(state.status == AshEditor::TerrainExternalChangeStatus::RecoveredReadOnly);
	CHECK(state.read_only);
	CHECK(state.can_repair);
	CHECK(state.can_save_as);
	CHECK(state.disk_generation == rejected.content_generation);
	CHECK(state.diagnostic.find("rejecting generation") != std::string::npos);
	CHECK(state.diagnostic.find("generation 2 index CRC") != std::string::npos);
	CHECK(service.GetLastError() == state.diagnostic);
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	AshEditor::TerrainEditorIntent selectSame{};
	selectSame.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	selectSame.asset_id = initial.asset_id;
	REQUIRE(service.SubmitIntent(selectSame));
	CHECK(service.GetLastError() == state.diagnostic);

	AshEngine::TerrainAssetSnapshot repaired = MakeExternalTerrainGeneration(
		initial, rejected.content_generation + 1u, "Valid Generation Three");
	repaired.source_path = path;
	SaveTerrainEditorSnapshot(path, repaired);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Repair)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	CHECK(service.GetWorkingSet()->content_generation == repaired.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Valid Generation Three");
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	AshEditor::TerrainEditorIntent renameAfterRepair = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	renameAfterRepair.layer_action.name = "Writable Again";
	CHECK(service.SubmitIntent(renameAfterRepair));
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain reload failure is read only and Repair restores a valid generation")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("external-failed-repair-root");
	const std::filesystem::path path = root / "repair.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	{
		std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
		REQUIRE(corrupt.is_open());
		corrupt << "broken terrain container";
	}

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Failed));
	CHECK(service.GetWorkingSet()->content_generation == initial.content_generation);
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	CHECK(service.GetExternalChangeState().read_only);
	CHECK(service.GetExternalChangeState().can_repair);
	CHECK(service.GetLastError().find("header") != std::string::npos);
	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Must Stay Read Only";
	CHECK_FALSE(service.SubmitIntent(rename));

	AshEngine::TerrainAssetSnapshot repaired = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 3u, "Repaired");
	repaired.source_path = path;
	SaveTerrainEditorSnapshot(path, repaired);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Repair)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	CHECK(service.GetWorkingSet()->content_generation == repaired.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Repaired");
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	std::filesystem::remove_all(root);
}

TEST_CASE("Failed Scene reload preserves Terrain recovery and the current editor state")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("scene-reload-preserves-terrain-recovery");
	const std::filesystem::path terrainPath = root / "terrain" / "source.AshTerrain";
	std::filesystem::create_directories(terrainPath.parent_path());
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = terrainPath;
	SaveTerrainEditorSnapshot(terrainPath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, terrainPath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService terrain{};
	REQUIRE(terrain.Initialize(assets, commands));
	REQUIRE(terrain.OpenSnapshotForAuthoring(initial));
	{
		std::ofstream corrupt(terrainPath, std::ios::binary | std::ios::trunc);
		REQUIRE(corrupt.is_open());
		corrupt << "broken terrain container";
	}
	REQUIRE(terrain.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		terrain, AshEditor::TerrainExternalChangeStatus::Failed));

	AshEditor::SceneService scenes{};
	REQUIRE(scenes.Initialize({}));
	const AshEngine::Entity marker = scenes.CreateEntity("Preserved marker");
	REQUIRE(marker.is_valid());
	const uint64_t markerId = marker.get_id();
	const std::filesystem::path scenePath = root / "scenes" / "active.scene.json";
	std::filesystem::create_directories(scenePath.parent_path());
	REQUIRE(scenes.SaveScene(scenePath));
	{
		std::ofstream corrupt(scenePath, std::ios::binary | std::ios::trunc);
		REQUIRE(corrupt.is_open());
		corrupt << "not a scene";
	}

	AshEditor::SelectionService selection{};
	selection.SelectSingle({ AshEditor::EditorSelectionKind::Entity, markerId, "Preserved marker", {} });
	AshEditor::UndoRedoService history{};
	AshEditor::EditorSettingsService settings{};
	REQUIRE(settings.Initialize(root));
	AshEditor::SceneWorkflowCoordinator workflow{};
	AshEditor::SceneWorkflowContext context{
		scenes, selection, history, settings, &terrain, nullptr, nullptr };

	const auto publishedBefore = terrain.GetPublishedSnapshot();
	REQUIRE(publishedBefore);
	const AshEditor::TerrainExternalChangeState recoveryBefore =
		terrain.GetExternalChangeState();
	const AshEditor::SceneReloadResult result = workflow.ReloadActiveScene(context);

	CHECK(result == AshEditor::SceneReloadResult::Failed);
	CHECK(scenes.GetActiveScenePath() == scenePath);
	CHECK(scenes.FindEntity(markerId).is_valid());
	CHECK(selection.IsSelected(AshEditor::EditorSelectionKind::Entity, markerId));
	CHECK(terrain.GetPublishedSnapshot() == publishedBefore);
	REQUIRE(terrain.GetWorkingSet() != nullptr);
	CHECK(terrain.GetWorkingSet()->content_generation == initial.content_generation);
	CHECK(terrain.GetExternalChangeState().status == recoveryBefore.status);
	CHECK(terrain.GetExternalChangeState().diagnostic == recoveryBefore.diagnostic);
	CHECK(terrain.GetExternalChangeState().read_only == recoveryBefore.read_only);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain Repair accepts a valid disk generation below the failed in-memory generation")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("repair-lower-generation-root");
	const std::filesystem::path path = root / "repair-lower.AshTerrain";
	AshEngine::TerrainAssetSnapshot highGeneration = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 9u, "In-Memory Generation Nine");
	highGeneration.source_path = path;
	SaveTerrainEditorSnapshot(path, highGeneration);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, highGeneration);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> highCached{};
	REQUIRE(assets.load_terrain_by_id(highGeneration.asset_id, highCached));
	REQUIRE(highCached);
	REQUIRE(highCached->content_generation == highGeneration.content_generation);

	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(highGeneration));
	{
		std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
		REQUIRE(corrupt.is_open());
		corrupt << "broken terrain container";
	}
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Failed));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == 9u);
	CHECK(service.GetExternalChangeState().read_only);

	AshEngine::TerrainAssetSnapshot repaired = MakeExternalTerrainGeneration(
		highGeneration, 3u, "Valid Disk Generation Three");
	repaired.source_path = path;
	SaveTerrainEditorSnapshot(path, repaired);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Repair)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));

	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == 3u);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Valid Disk Generation Three");
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	CHECK_FALSE(service.GetExternalChangeState().read_only);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> repairedCached{};
	REQUIRE(assets.load_terrain_by_id(highGeneration.asset_id, repairedCached));
	REQUIRE(repairedCached);
	CHECK(repairedCached->content_generation == 3u);
	CHECK(repairedCached->edit_layers->front().name == "Valid Disk Generation Three");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain same-asset selection cannot cancel an in-flight reload")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("same-asset-reload-root");
	const std::filesystem::path path = root / "same-asset.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 2u, "Reload Still Completes");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	TerrainEditorWorkerBlocker blocker{};
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::Reloading);

	AshEditor::TerrainEditorIntent select{};
	select.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	select.asset_id = initial.asset_id;
	REQUIRE(service.SubmitIntent(select));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::Reloading);
	blocker.Release();

	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Reload Still Completes");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain save completion does not swallow a later external write")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("save-external-race-root");
	const std::filesystem::path path = root / "save-race.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Worker Save A";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	const uint64_t savedGeneration = service.GetWorkingSet()->content_generation;
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> written{};
	const auto writeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < writeDeadline)
	{
		std::string loadError{};
		written.reset();
		if (AshEngine::load_terrain_container(path, written, nullptr, &loadError) ==
				AshEngine::TerrainContainerResult::Success &&
			written && written->content_generation == savedGeneration)
		{
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(written);
	REQUIRE(written->content_generation == savedGeneration);

	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, savedGeneration + 3u, "External B After Save");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	std::filesystem::last_write_time(
		path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(4));

	service.Update();
	REQUIRE(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Succeeded);
	const auto reloadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < reloadDeadline &&
		service.GetWorkingSet()->content_generation != external.content_generation)
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "External B After Save");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain reload preserves dirty local state when history removal fails")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("reload-history-failure-root");
	const std::filesystem::path path = root / "history-failure.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Dirty Local Must Survive";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	const uint64_t localGeneration = service.GetWorkingSet()->content_generation;
	const auto localPublished = service.GetPublishedSnapshot();
	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, localGeneration + 4u, "External Candidate");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));

	commands.fail_remove = true;
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::Conflict);
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == localGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Dirty Local Must Survive");
	CHECK(service.GetPublishedSnapshot() == localPublished);
	CHECK(service.HasDirtyAssets());
	CHECK(service.GetLastError().find("history") != std::string::npos);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> cachedAfterFailedRemoval{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, cachedAfterFailedRemoval));
	REQUIRE(cachedAfterFailedRemoval);
	CHECK(cachedAfterFailedRemoval == localPublished);
	CHECK(cachedAfterFailedRemoval->content_generation == localGeneration);
	CHECK(cachedAfterFailedRemoval->edit_layers->front().name == "Dirty Local Must Survive");

	commands.fail_remove = false;
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "External Candidate");
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> cachedAfterRetry{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, cachedAfterRetry));
	REQUIRE(cachedAfterRetry);
	CHECK(cachedAfterRetry->content_generation == external.content_generation);
	CHECK(cachedAfterRetry->edit_layers->front().name == "External Candidate");
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain asset reselect retires a pending replacement before it can commit")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("selection-reselect-retires-pending-root");
	const std::filesystem::path pathA = root / "terrain-a.AshTerrain";
	const std::filesystem::path pathB = root / "terrain-b.AshTerrain";
	AshEngine::TerrainAssetSnapshot terrainA = MakeEditorStrokeSnapshot();
	terrainA.source_path = pathA;
	SaveTerrainEditorSnapshot(pathA, terrainA);
	AshEngine::TerrainAssetSnapshot terrainB = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 2u, "Pending Terrain B");
	terrainB.source_path = pathB;
	SaveTerrainEditorSnapshot(pathB, terrainB);

	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathA, terrainA);
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathB, terrainB);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(terrainA));
	const auto publishedA = service.GetPublishedSnapshot();
	REQUIRE(publishedA);

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent selectB{};
	selectB.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	selectB.asset_id = terrainB.asset_id;
	REQUIRE(service.SubmitIntent(selectB));
	REQUIRE(service.HasBlockingOperation());

	AshEditor::TerrainEditorIntent reselectA{};
	reselectA.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	reselectA.asset_id = terrainA.asset_id;
	REQUIRE(service.SubmitIntent(reselectA));
	CHECK_FALSE(service.HasBlockingOperation());
	CHECK(service.GetSelectedAssetId() == terrainA.asset_id);
	CHECK(service.GetPublishedSnapshot() == publishedA);

	blocker.Release();
	auto workerFence = AshEngine::dispatch_background_task(
		"TerrainEditorServiceTests::PendingSelectionFence", []() {});
	REQUIRE(workerFence.valid());
	workerFence.wait();
	for (uint32_t attempt = 0u; attempt < 4u; ++attempt)
	{
		service.Update();
	}
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetSelectedAssetId() == terrainA.asset_id);
	CHECK(service.GetWorkingSet()->asset_id == terrainA.asset_id);
	CHECK(service.GetPublishedSnapshot() == publishedA);
	CHECK(commands.removed_terrain_asset_ids.empty());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain asset selection rejects a second target while the first target is pending")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("selection-rejects-second-pending-root");
	const std::filesystem::path pathA = root / "terrain-a.AshTerrain";
	const std::filesystem::path pathB = root / "terrain-b.AshTerrain";
	const std::filesystem::path pathC = root / "terrain-c.AshTerrain";
	AshEngine::TerrainAssetSnapshot terrainA = MakeEditorStrokeSnapshot();
	terrainA.source_path = pathA;
	SaveTerrainEditorSnapshot(pathA, terrainA);
	AshEngine::TerrainAssetSnapshot terrainB = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 2u, "Pending Terrain B");
	terrainB.source_path = pathB;
	SaveTerrainEditorSnapshot(pathB, terrainB);
	AshEngine::TerrainAssetSnapshot terrainC = MakeExternalTerrainGeneration(
		MakeEditorStrokeSnapshot(), 3u, "Rejected Terrain C");
	terrainC.source_path = pathC;
	SaveTerrainEditorSnapshot(pathC, terrainC);

	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathA, terrainA);
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathB, terrainB);
	BindTerrainEditorSnapshotToAssetDatabase(assets, pathC, terrainC);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(terrainA));

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent selectB{};
	selectB.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	selectB.asset_id = terrainB.asset_id;
	REQUIRE(service.SubmitIntent(selectB));
	AshEditor::TerrainEditorIntent selectC{};
	selectC.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	selectC.asset_id = terrainC.asset_id;
	CHECK_FALSE(service.SubmitIntent(selectC));
	CHECK(service.HasBlockingOperation());

	blocker.Release();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline &&
		service.GetSelectedAssetId() != terrainB.asset_id)
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetSelectedAssetId() == terrainB.asset_id);
	CHECK(service.GetWorkingSet()->asset_id == terrainB.asset_id);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Pending Terrain B");
	CHECK(service.GetSelectedAssetId() != terrainC.asset_id);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain clean reload survives an AssetDatabase refresh during candidate loading")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("reload-refresh-during-load-root");
	const std::filesystem::path path = root / "refresh-during-load.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> cachedInitial{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, cachedInitial));
	REQUIRE(cachedInitial);

	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, initial.content_generation + 3u, "Reloaded After Refresh");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);

	TerrainEditorWorkerBlocker blocker{};
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(assets.refresh());
	blocker.Release();
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == initial.content_generation);

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Reloaded After Refresh");
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Ready);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain conflict reload reacquires publication lineage after AssetDatabase refresh")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("conflict-refresh-lineage-root");
	const std::filesystem::path path = root / "conflict-refresh.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Dirty Local Before Refresh";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, service.GetWorkingSet()->content_generation + 4u, "Disk After Refresh");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));

	REQUIRE(assets.refresh());
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Disk After Refresh");
	CHECK_FALSE(service.HasDirtyAssets());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain clean reload history failure preserves its candidate and retries in place")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("clean-reload-history-retry-root");
	const std::filesystem::path path = root / "clean-history-retry.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Clean Local With History";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	REQUIRE(WaitForTerrainFileOperation(service));
	REQUIRE_FALSE(service.HasDirtyAssets());
	const uint64_t localGeneration = service.GetWorkingSet()->content_generation;
	const auto localPublished = service.GetPublishedSnapshot();
	const AshEditor::TerrainExternalChangeState originalExternal =
		service.GetExternalChangeState();

	AshEngine::TerrainAssetSnapshot external = MakeExternalTerrainGeneration(
		initial, localGeneration + 3u, "Clean External Candidate");
	external.source_path = path;
	SaveTerrainEditorSnapshot(path, external);
	commands.fail_remove = true;
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	const auto failureDeadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < failureDeadline &&
		service.HasBlockingOperation())
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE_FALSE(service.HasBlockingOperation());
	CHECK(service.GetExternalChangeState().status == originalExternal.status);
	CHECK(service.GetExternalChangeState().diagnostic == originalExternal.diagnostic);
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == localGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Clean Local With History");
	CHECK(service.GetPublishedSnapshot() == localPublished);
	CHECK(service.GetLastError().find("history") != std::string::npos);

	commands.fail_remove = false;
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == external.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Clean External Candidate");
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain external polling uses container revision when write time is unchanged")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("revision-poll-same-mtime-root");
	const std::filesystem::path path = root / "revision-poll.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loadedInitial{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, loadedInitial));
	REQUIRE(loadedInitial);
	REQUIRE(loadedInitial->source_revision.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(*loadedInitial));

	AshEditor::TerrainEditorIntent firstRename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	firstRename.layer_action.name = "Saved A";
	REQUIRE(service.SubmitIntent(firstRename));
	service.Update();
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	REQUIRE(WaitForTerrainFileOperation(service));
	REQUIRE_FALSE(service.HasDirtyAssets());

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> savedA{};
	AshEngine::TerrainContainerLoadReport savedReport{};
	std::string loadError{};
	REQUIRE(AshEngine::load_terrain_container(path, savedA, &savedReport, &loadError) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(savedA);
	REQUIRE(savedReport.source_revision.is_valid());
	std::error_code timeError{};
	const std::filesystem::file_time_type savedWriteTime =
		std::filesystem::last_write_time(path, timeError);
	REQUIRE_FALSE(timeError);

	AshEngine::TerrainAssetSnapshot externalB = AppendChangedTerrainGeneration(
		path, *savedA, savedA->content_generation + 4u, "External B Same Mtime");
	std::filesystem::last_write_time(path, savedWriteTime, timeError);
	REQUIRE_FALSE(timeError);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loadedB{};
	AshEngine::TerrainContainerLoadReport externalReport{};
	REQUIRE(AshEngine::load_terrain_container(path, loadedB, &externalReport, &loadError) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loadedB);
	REQUIRE(externalReport.source_revision.is_valid());
	REQUIRE(externalReport.source_revision != savedReport.source_revision);
	CHECK(std::filesystem::last_write_time(path) == savedWriteTime);

	AshEditor::TerrainEditorIntent secondRename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	secondRename.layer_action.name = "Local After External B";
	REQUIRE(service.SubmitIntent(secondRename));
	service.Update();
	const uint64_t localGeneration = service.GetWorkingSet()->content_generation;
	const auto localPublished = service.GetPublishedSnapshot();
	const size_t historySize = commands.commands.size();

	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == localGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Local After External B");
	CHECK(service.GetPublishedSnapshot() == localPublished);
	CHECK(service.GetExternalChangeState().disk_generation == externalB.content_generation);
	CHECK(commands.commands.size() == historySize);
	CHECK(service.HasDirtyAssets());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain clean physical rollback enters conflict before accepting an older revision")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("clean-physical-rollback-root");
	const std::filesystem::path path = root / "clean-physical-rollback.AshTerrain";
	const std::filesystem::path backup = root / "clean-physical-rollback.backup";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	REQUIRE(std::filesystem::copy_file(path, backup));
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loadedInitial{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, loadedInitial));
	REQUIRE(loadedInitial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(*loadedInitial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Saved Newer Local";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	REQUIRE(WaitForTerrainFileOperation(service));
	REQUIRE_FALSE(service.HasDirtyAssets());
	const uint64_t newerGeneration = service.GetWorkingSet()->content_generation;
	const auto newerPublished = service.GetPublishedSnapshot();
	REQUIRE(newerGeneration > initial.content_generation);

	REQUIRE(std::filesystem::copy_file(
		backup, path, std::filesystem::copy_options::overwrite_existing));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == newerGeneration);
	CHECK(service.GetPublishedSnapshot() == newerPublished);
	CHECK(service.GetExternalChangeState().disk_generation == initial.content_generation);
	CHECK(service.GetExternalChangeState().diagnostic.find("older") != std::string::npos);
	CHECK_FALSE(service.HasDirtyAssets());

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Reload)));
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::None));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == initial.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name ==
		loadedInitial->edit_layers->front().name);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain checked Save source change preserves dirty state and enters conflict")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("checked-save-source-change-root");
	const std::filesystem::path path = root / "checked-save-source-change.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loadedInitial{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, loadedInitial));
	REQUIRE(loadedInitial);
	REQUIRE(loadedInitial->source_revision.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(*loadedInitial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Local Save Must Stay Dirty";
	REQUIRE(service.SubmitIntent(rename));
	service.Update();
	const uint64_t localGeneration = service.GetWorkingSet()->content_generation;
	const auto localPublished = service.GetPublishedSnapshot();
	const size_t historySize = commands.commands.size();

	TerrainEditorWorkerBlocker blocker{};
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	REQUIRE(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Running);

	AshEngine::TerrainAssetSnapshot externalB = AppendChangedTerrainGeneration(
		path, initial, localGeneration + 4u, "External B Before Save Commit");
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loadedB{};
	AshEngine::TerrainContainerLoadReport externalReport{};
	std::string loadError{};
	REQUIRE(AshEngine::load_terrain_container(path, loadedB, &externalReport, &loadError) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE(loadedB);
	REQUIRE(externalReport.source_revision.is_valid());
	blocker.Release();

	const auto failureDeadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < failureDeadline &&
		service.GetFileOperationState().status != AshEditor::TerrainFileOperationStatus::Failed)
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Failed);
	CHECK(service.GetFileOperationState().error.find("revision") != std::string::npos);
	REQUIRE(WaitForTerrainExternalState(
		service, AshEditor::TerrainExternalChangeStatus::Conflict));
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == localGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "Local Save Must Stay Dirty");
	CHECK(service.GetPublishedSnapshot() == localPublished);
	CHECK(service.GetExternalChangeState().disk_generation == externalB.content_generation);
	CHECK(service.HasDirtyAssets());
	CHECK(commands.commands.size() == historySize);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> cachedLocal{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, cachedLocal));
	REQUIRE(cachedLocal);
	CHECK(cachedLocal == localPublished);
	std::filesystem::remove_all(root);
}

#if defined(_WIN32)
TEST_CASE("Terrain external polling debounces a cooperative writer Busy lease")
{
	const std::filesystem::path root = TerrainEditorAssetRoot("poll-writer-busy-root");
	const std::filesystem::path path = root / "poll-writer-busy.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loaded{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, loaded));
	REQUIRE(loaded);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(*loaded));

	{
		TerrainTests::ScopedTerrainCommitLeaseForTest lease(path);
		REQUIRE(lease.acquired());
		for (int poll = 0; poll < 3; ++poll)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(550));
			service.Update();
			CHECK(service.GetExternalChangeState().status ==
				AshEditor::TerrainExternalChangeStatus::None);
			CHECK_FALSE(service.HasBlockingOperation());
		}
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(550));
	service.Update();
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	CHECK(service.GetLastError().empty());
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain explicit reload retries automatically after a cooperative writer releases")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot("reload-writer-busy-root");
	const std::filesystem::path path = root / "reload-writer-busy.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = path;
	SaveTerrainEditorSnapshot(path, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, path, initial);
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> loadedA{};
	REQUIRE(assets.load_terrain_by_id(initial.asset_id, loadedA));
	REQUIRE(loadedA);
	REQUIRE(loadedA->source_revision.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(*loadedA));

	const AshEngine::TerrainAssetSnapshot externalB = AppendChangedTerrainGeneration(
		path, *loadedA, loadedA->content_generation + 1u, "External After Busy");
	{
		TerrainTests::ScopedTerrainCommitLeaseForTest lease(path);
		REQUIRE(lease.acquired());
		REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::Reload)));
		const auto blockedDeadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (service.HasBlockingOperation() &&
			std::chrono::steady_clock::now() < blockedDeadline)
		{
			service.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		REQUIRE_FALSE(service.HasBlockingOperation());
		REQUIRE(service.GetWorkingSet());
		CHECK(service.GetWorkingSet()->content_generation == loadedA->content_generation);
		CHECK(service.GetExternalChangeState().status ==
			AshEditor::TerrainExternalChangeStatus::None);
	}

	const auto retryDeadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < retryDeadline &&
		service.GetWorkingSet() &&
		service.GetWorkingSet()->content_generation != externalB.content_generation)
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(service.GetWorkingSet());
	CHECK(service.GetWorkingSet()->content_generation == externalB.content_generation);
	CHECK(service.GetWorkingSet()->edit_layers.front().name == "External After Busy");
	CHECK(service.GetExternalChangeState().status ==
		AshEditor::TerrainExternalChangeStatus::None);
	std::filesystem::remove_all(root);
}
#endif

TEST_CASE("Terrain editor file jobs create refreshes the catalog and opens a clean real asset")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-create-root");
	std::filesystem::create_directories(root / "terrain");
	const std::filesystem::path relativePath = "terrain/created.AshTerrain";
	const std::filesystem::path finalPath = root / relativePath;
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));

	AshEditor::TerrainEditorIntent create = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Create);
	create.asset_path = relativePath;
	create.create_desc.layout = TerrainTests::MakeSmallLayout();
	create.create_desc.height_mapping = { -10.0f, 20.0f };
	create.create_desc.flat_height = 3.5f;
	REQUIRE_MESSAGE(service.SubmitIntent(create), service.GetLastError());
	REQUIRE(WaitForTerrainFileOperation(service));

	const AshEngine::AssetInfo* catalogAsset = assets.find_asset_by_path(relativePath);
	REQUIRE(catalogAsset != nullptr);
	REQUIRE(catalogAsset->type == AshEngine::AssetType::Terrain);
	const AshEngine::TerrainAssetId realAssetId = catalogAsset->id;
	CHECK(realAssetId != 0u);
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetSelectedAssetId() == realAssetId);
	CHECK(service.GetWorkingSet()->asset_id == realAssetId);
	CHECK(service.GetWorkingSet()->source_path == relativePath);
	CHECK_FALSE(service.HasDirtyAssets());
	REQUIRE(service.GetWorkingSet()->base_heights.size() == 81u);
	const uint16_t encodedFlat = AshEngine::encode_terrain_height_r16(
		create.create_desc.flat_height, create.create_desc.height_mapping);
	CHECK(std::all_of(
		service.GetWorkingSet()->base_heights.begin(),
		service.GetWorkingSet()->base_heights.end(),
		[encodedFlat](const uint16_t value) { return value == encodedFlat; }));
	REQUIRE(service.GetPublishedSnapshot());
	CHECK(service.GetPublishedSnapshot()->asset_id == realAssetId);
	CHECK(std::filesystem::exists(finalPath));
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs import an absolute external RAW source and open exact values")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-import-root");
	std::filesystem::create_directories(root / "terrain");
	const std::filesystem::path sourcePath = std::filesystem::absolute(
		TerrainEditorFilePath("file-jobs-external-source.raw"));
	std::filesystem::remove(sourcePath);
	std::vector<uint16_t> sourceValues(81u);
	for (size_t index = 0u; index < sourceValues.size(); ++index)
	{
		sourceValues[index] = static_cast<uint16_t>(index * 701u);
	}
	WriteTerrainEditorR16(sourcePath, sourceValues);

	const std::filesystem::path relativePath = "terrain/imported.AshTerrain";
	const std::filesystem::path finalPath = root / relativePath;
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));

	AshEditor::TerrainEditorIntent import = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Import);
	import.asset_path = relativePath;
	AshEngine::TerrainHeightImportDesc importDesc{};
	importDesc.source_path = sourcePath;
	importDesc.format = AshEngine::TerrainHeightFileFormat::RawR16;
	importDesc.target_layout = TerrainTests::MakeSmallLayout();
	importDesc.height_mapping = { 0.0f, 65535.0f };
	importDesc.source_width = 9u;
	importDesc.source_height = 9u;
	importDesc.byte_order = AshEngine::TerrainByteOrder::LittleEndian;
	importDesc.resize_policy = AshEngine::TerrainResizePolicy::Reject;
	import.import_desc = importDesc;
	REQUIRE_MESSAGE(service.SubmitIntent(import), service.GetLastError());
	REQUIRE(WaitForTerrainFileOperation(service));

	const AshEngine::AssetInfo* catalogAsset = assets.find_asset_by_path(relativePath);
	REQUIRE(catalogAsset != nullptr);
	REQUIRE(catalogAsset->type == AshEngine::AssetType::Terrain);
	const AshEngine::TerrainAssetId realAssetId = catalogAsset->id;
	CHECK(realAssetId != 0u);
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetSelectedAssetId() == realAssetId);
	CHECK(service.GetWorkingSet()->asset_id == realAssetId);
	CHECK(service.GetWorkingSet()->source_path == relativePath);
	CHECK(service.GetWorkingSet()->base_heights == sourceValues);
	CHECK_FALSE(service.HasDirtyAssets());
	CHECK(std::filesystem::exists(finalPath));
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	std::filesystem::remove(sourcePath);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs preserve PNG8 precision warnings through catalog binding")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-png8-warning-root");
	std::filesystem::create_directories(root / "terrain");
	const std::filesystem::path sourcePath = std::filesystem::absolute(
		TerrainEditorFilePath("file-jobs-png8-warning.png"));
	const std::array<uint8_t, 77> png = {
		0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au,
		0x00u, 0x00u, 0x00u, 0x0du, 0x49u, 0x48u, 0x44u, 0x52u,
		0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x03u,
		0x08u, 0x00u, 0x00u, 0x00u, 0x00u, 0x73u, 0x43u, 0xeau,
		0x63u, 0x00u, 0x00u, 0x00u, 0x14u, 0x49u, 0x44u, 0x41u,
		0x54u, 0x78u, 0xdau, 0x63u, 0x60u, 0x50u, 0x70u, 0x60u,
		0x48u, 0x68u, 0x58u, 0xc0u, 0x70u, 0xe0u, 0xc1u, 0x7fu,
		0x00u, 0x11u, 0x4bu, 0x04u, 0x80u, 0xf9u, 0xdfu, 0x38u,
		0xcfu, 0x00u, 0x00u, 0x00u, 0x00u, 0x49u, 0x45u, 0x4eu,
		0x44u, 0xaeu, 0x42u, 0x60u, 0x82u
	};
	{
		std::ofstream output(sourcePath, std::ios::binary | std::ios::trunc);
		REQUIRE(output.is_open());
		REQUIRE(output.write(
			reinterpret_cast<const char*>(png.data()),
			static_cast<std::streamsize>(png.size())));
	}

	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	AshEditor::TerrainEditorIntent import = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Import);
	import.asset_path = "terrain/png8-warning.AshTerrain";
	AshEngine::TerrainHeightImportDesc importDesc{};
	importDesc.source_path = sourcePath;
	importDesc.format = AshEngine::TerrainHeightFileFormat::Png;
	importDesc.target_layout = { 3u, 3u, 1u, 1u, 2u, 1.0f };
	importDesc.height_mapping = { 0.0f, 1024.0f };
	importDesc.source_width = 3u;
	importDesc.source_height = 3u;
	importDesc.resize_policy = AshEngine::TerrainResizePolicy::Reject;
	import.import_desc = importDesc;
	REQUIRE_MESSAGE(service.SubmitIntent(import), service.GetLastError());
	REQUIRE_MESSAGE(WaitForTerrainFileOperation(service),
		service.GetFileOperationState().error);
	REQUIRE(service.GetFileOperationState().warnings.size() == 1u);
	CHECK(service.GetFileOperationState().warnings.front() ==
		"8-bit PNG height source reduces terrain precision.");
	CHECK(service.GetWorkingSet() != nullptr);

	std::filesystem::remove(sourcePath);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs never overwrite destinations that appear after submit")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-destination-race-root");
	std::filesystem::create_directories(root / "terrain");
	std::filesystem::create_directories(root / "exports");
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	const std::string appearedBytes = "external writer owns this destination";

	SUBCASE("Create")
	{
		const std::filesystem::path relativePath = "terrain/create-race.AshTerrain";
		const std::filesystem::path finalPath = root / relativePath;
		TerrainEditorWorkerBlocker blocker{};
		AshEditor::TerrainEditorIntent create = MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::Create);
		create.asset_path = relativePath;
		create.create_desc.layout = TerrainTests::MakeSmallLayout();
		create.create_desc.height_mapping = { 0.0f, 1024.0f };
		create.create_desc.flat_height = 10.0f;
		REQUIRE_MESSAGE(service.SubmitIntent(create), service.GetLastError());
		CHECK(service.GetFileOperationState().status ==
			AshEditor::TerrainFileOperationStatus::Running);
		WriteTerrainEditorText(finalPath, appearedBytes);
		blocker.Release();

		CHECK_FALSE(WaitForTerrainFileOperation(service));
		CHECK(service.GetFileOperationState().status ==
			AshEditor::TerrainFileOperationStatus::Failed);
		CHECK(ReadTerrainEditorText(finalPath) == appearedBytes);
		CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	}

	SUBCASE("Import")
	{
		const std::filesystem::path sourcePath = std::filesystem::absolute(
			TerrainEditorFilePath("file-jobs-import-race-source.raw"));
		std::filesystem::remove(sourcePath);
		WriteTerrainEditorR16(sourcePath, std::vector<uint16_t>(81u, 32768u));
		const std::filesystem::path relativePath = "terrain/import-race.AshTerrain";
		const std::filesystem::path finalPath = root / relativePath;
		TerrainEditorWorkerBlocker blocker{};
		AshEditor::TerrainEditorIntent import = MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::Import);
		import.asset_path = relativePath;
		AshEngine::TerrainHeightImportDesc importDesc{};
		importDesc.source_path = sourcePath;
		importDesc.format = AshEngine::TerrainHeightFileFormat::RawR16;
		importDesc.target_layout = TerrainTests::MakeSmallLayout();
		importDesc.height_mapping = { 0.0f, 65535.0f };
		importDesc.source_width = 9u;
		importDesc.source_height = 9u;
		import.import_desc = importDesc;
		REQUIRE_MESSAGE(service.SubmitIntent(import), service.GetLastError());
		CHECK(service.GetFileOperationState().status ==
			AshEditor::TerrainFileOperationStatus::Running);
		WriteTerrainEditorText(finalPath, appearedBytes);
		blocker.Release();

		CHECK_FALSE(WaitForTerrainFileOperation(service));
		CHECK(service.GetFileOperationState().status ==
			AshEditor::TerrainFileOperationStatus::Failed);
		CHECK(ReadTerrainEditorText(finalPath) == appearedBytes);
		CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
		std::filesystem::remove(sourcePath);
	}

	SUBCASE("Export")
	{
		const std::filesystem::path sourcePath = root / "terrain/source.AshTerrain";
		AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
		initial.source_path = sourcePath;
		SaveTerrainEditorSnapshot(sourcePath, initial);
		REQUIRE(assets.refresh());
		BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
		REQUIRE(service.OpenSnapshotForAuthoring(initial));
		const std::filesystem::path relativePath = "exports/export-race.raw";
		const std::filesystem::path finalPath = root / relativePath;
		TerrainEditorWorkerBlocker blocker{};
		AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::Export);
		AshEngine::TerrainHeightExportDesc exportDesc{};
		exportDesc.destination_path = relativePath;
		exportDesc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
		exportDesc.source = AshEngine::TerrainExportSource::FinalComposedHeight;
		exportIntent.export_desc = exportDesc;
		REQUIRE_MESSAGE(service.SubmitIntent(exportIntent), service.GetLastError());
		CHECK(service.GetFileOperationState().status ==
			AshEditor::TerrainFileOperationStatus::Running);
		WriteTerrainEditorText(finalPath, appearedBytes);
		blocker.Release();

		CHECK_FALSE(WaitForTerrainFileOperation(service));
		CHECK(service.GetFileOperationState().status ==
			AshEditor::TerrainFileOperationStatus::Failed);
		CHECK(ReadTerrainEditorText(finalPath) == appearedBytes);
		CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	}

	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs export final base and height layers through every approved height format")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-export-formats-root");
	std::filesystem::create_directories(root / "exports");
	const std::filesystem::path sourcePath = root / "source.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	auto exportLayers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(
		*initial.edit_layers);
	exportLayers->front().height_blocks.push_back({
		{ 0u, 0u }, { 0u, 0u, 1u, 1u }, { 7.5f }, { 1.0f }
	});
	initial.edit_layers = std::move(exportLayers);
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent rename = MakeLayerActionIntent(
		AshEditor::TerrainLayerActionKind::Rename, service.GetSelectedLayerId());
	rename.layer_action.name = "Dirty During Exports";
	REQUIRE(service.SubmitIntent(rename));
	CHECK(service.HasDirtyAssets());

	struct ExportCase
	{
		const char* name = nullptr;
		const char* extension = nullptr;
		AshEngine::TerrainHeightFileFormat format =
			AshEngine::TerrainHeightFileFormat::RawR32F;
	};
	const std::array<ExportCase, 4> formats = { {
		{ "png", ".png", AshEngine::TerrainHeightFileFormat::Png },
		{ "raw-r16", ".raw", AshEngine::TerrainHeightFileFormat::RawR16 },
		{ "raw-r32f", ".raw", AshEngine::TerrainHeightFileFormat::RawR32F },
		{ "exr", ".exr", AshEngine::TerrainHeightFileFormat::Exr }
	} };
	struct SourceCase
	{
		const char* name = nullptr;
		AshEngine::TerrainExportSource source =
			AshEngine::TerrainExportSource::FinalComposedHeight;
	};
	const std::array<SourceCase, 3> sources = { {
		{ "final", AshEngine::TerrainExportSource::FinalComposedHeight },
		{ "base", AshEngine::TerrainExportSource::BaseHeight },
		{ "height-layer", AshEngine::TerrainExportSource::HeightEditLayer }
	} };
	for (const SourceCase& sourceCase : sources)
	{
		for (const ExportCase& exportCase : formats)
		{
			const std::filesystem::path relativePath =
				std::filesystem::path("exports") /
				(sourceCase.name + std::string("-") + exportCase.name + exportCase.extension);
			AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
				AshEditor::TerrainEditorIntent::Kind::Export);
			AshEngine::TerrainHeightExportDesc exportDesc{};
			exportDesc.destination_path = relativePath;
			exportDesc.format = exportCase.format;
			exportDesc.source = sourceCase.source;
			if (sourceCase.source == AshEngine::TerrainExportSource::HeightEditLayer)
			{
				exportDesc.source_layer_id = MakeEditorStrokeLayerId();
			}
			exportIntent.export_desc = exportDesc;
			REQUIRE_MESSAGE(service.SubmitIntent(exportIntent), service.GetLastError());
			REQUIRE_MESSAGE(WaitForTerrainFileOperation(service),
				service.GetFileOperationState().error);
			const std::filesystem::path finalPath = root / relativePath;
			REQUIRE(std::filesystem::exists(finalPath));
			CHECK(std::filesystem::file_size(finalPath) > 0u);
			CHECK(service.HasDirtyAssets());
			CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
		}
	}
	CHECK(ReadTerrainEditorR32F(root / "exports/final-raw-r32f.raw").size() == 81u);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs export an immutable generation while later edits continue")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-export-generation-root");
	std::filesystem::create_directories(root / "exports");
	const std::filesystem::path sourcePath = root / "source.AshTerrain";
	const std::filesystem::path relativeExportPath = "exports/captured.raw";
	const std::filesystem::path exportPath = root / relativeExportPath;
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	TerrainEditorWorkerBlocker blocker{};
	AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Export);
	AshEngine::TerrainHeightExportDesc exportDesc{};
	exportDesc.destination_path = relativeExportPath;
	exportDesc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	exportDesc.source = AshEngine::TerrainExportSource::FinalComposedHeight;
	exportIntent.export_desc = exportDesc;
	REQUIRE_MESSAGE(service.SubmitIntent(exportIntent), service.GetLastError());
	REQUIRE(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Running);
	const uint64_t capturedGeneration =
		service.GetFileOperationState().content_generation;

	REQUIRE(SubmitConfiguredBeginStroke(service));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.GetWorkingSet()->content_generation > capturedGeneration);
	CHECK(service.HasDirtyAssets());
	blocker.Release();

	REQUIRE_MESSAGE(WaitForTerrainFileOperation(service),
		service.GetFileOperationState().error);
	const auto compositionDeadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (service.HasPendingComposition() &&
		std::chrono::steady_clock::now() < compositionDeadline)
	{
		service.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetFileOperationState().content_generation == capturedGeneration);
	CHECK(service.GetWorkingSet()->content_generation > capturedGeneration);
	CHECK(service.HasDirtyAssets());
	const std::vector<float> capturedHeights = ReadTerrainEditorR32F(exportPath);
	REQUIRE(capturedHeights.size() == 81u);
	CHECK(std::all_of(
		capturedHeights.begin(), capturedHeights.end(),
		[](const float value) { return value == 0.0f; }));
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor unrooted file jobs export normalized material weights through every approved format")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-external-weight-export-root");
	const std::filesystem::path externalDirectory =
		TerrainEditorAssetRoot("file-jobs-external-weight-export-destination");
	const std::filesystem::path sourcePath = root / "source.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	auto layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(
		*initial.edit_layers);
	std::array<float, AshEngine::k_terrain_material_layer_count> weights{};
	weights[0] = 1.0f;
	weights[1] = 3.0f;
	layers->front().weight_blocks.push_back({
		{ 0u, 0u }, { 0u, 0u, 1u, 1u }, { weights }, { 1.0f }
	});
	initial.edit_layers = std::move(layers);
	SaveTerrainEditorSnapshot(sourcePath, initial);

	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	struct ExportCase
	{
		std::filesystem::path path{};
		AshEngine::TerrainHeightFileFormat format =
			AshEngine::TerrainHeightFileFormat::RawR16;
	};
	const std::array<ExportCase, 4> exports = { {
		{ std::filesystem::absolute(externalDirectory / "weight.png"),
			AshEngine::TerrainHeightFileFormat::Png },
		{ std::filesystem::absolute(externalDirectory / "weight-r16.raw"),
			AshEngine::TerrainHeightFileFormat::RawR16 },
		{ std::filesystem::absolute(externalDirectory / "weight-r32f.raw"),
			AshEngine::TerrainHeightFileFormat::RawR32F },
		{ std::filesystem::absolute(externalDirectory / "weight.exr"),
			AshEngine::TerrainHeightFileFormat::Exr }
	} };
	for (const ExportCase& exportCase : exports)
	{
		std::filesystem::remove(exportCase.path);
		AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::Export);
		AshEngine::TerrainHeightExportDesc exportDesc{};
		exportDesc.destination_path = exportCase.path;
		exportDesc.format = exportCase.format;
		exportDesc.source = AshEngine::TerrainExportSource::MaterialWeightLayer;
		exportDesc.source_layer_id = MakeEditorStrokeLayerId();
		exportDesc.material_layer_index = 1u;
		exportIntent.export_desc = exportDesc;
		REQUIRE_MESSAGE(service.SubmitIntent(exportIntent), service.GetLastError());
		REQUIRE_MESSAGE(WaitForTerrainFileOperation(service),
			service.GetFileOperationState().error);
		REQUIRE(std::filesystem::exists(exportCase.path));

		AshEngine::TerrainHeightImportDesc verifyDesc{};
		verifyDesc.source_path = exportCase.path;
		verifyDesc.format = exportCase.format;
		verifyDesc.target_layout = TerrainTests::MakeSmallLayout();
		verifyDesc.height_mapping = { 0.0f, 1.0f };
		verifyDesc.source_width = 9u;
		verifyDesc.source_height = 9u;
		verifyDesc.resize_policy = AshEngine::TerrainResizePolicy::Reject;
		verifyDesc.exr_channel = "Y";
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> verified{};
		std::string error{};
		REQUIRE_MESSAGE(AshEngine::import_terrain_height(
			84u, verifyDesc, verified, nullptr, &error) ==
			AshEngine::TerrainImportResult::Success, error);
		REQUIRE(verified);
		REQUIRE(verified->base_heights);
		REQUIRE_FALSE(verified->base_heights->empty());
		CHECK(AshEngine::decode_terrain_height_r16(
			verified->base_heights->front(), verifyDesc.height_mapping) ==
			doctest::Approx(0.75f).epsilon(0.001f));
	}
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(externalDirectory));
	std::filesystem::remove_all(externalDirectory);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs resolve relative external export paths from the asset root without containment")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-relative-external-export-root");
	const std::filesystem::path externalDirectory =
		TerrainEditorAssetRoot("file-jobs-relative-external-export-destination");
	const std::filesystem::path sourcePath = root / "source.AshTerrain";
	const std::filesystem::path externalPath = externalDirectory / "relative.raw";
	std::filesystem::remove(externalPath);

	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Export);
	AshEngine::TerrainHeightExportDesc exportDesc{};
	exportDesc.destination_path =
		std::filesystem::path("..") / externalDirectory.filename() / externalPath.filename();
	exportDesc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	exportDesc.source = AshEngine::TerrainExportSource::FinalComposedHeight;
	exportIntent.export_desc = exportDesc;
	REQUIRE_MESSAGE(service.SubmitIntent(exportIntent), service.GetLastError());
	REQUIRE_MESSAGE(WaitForTerrainFileOperation(service),
		service.GetFileOperationState().error);
	CHECK(std::filesystem::equivalent(
		service.GetFileOperationState().path, externalPath));
	CHECK(std::filesystem::exists(externalPath));
	CHECK(ReadTerrainEditorR32F(externalPath).size() == 81u);
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(externalDirectory));

	std::filesystem::remove_all(externalDirectory);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs reject unsafe paths mismatched formats and invalid sources")
{
	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-rejection-root");
	std::filesystem::create_directories(root / "terrain");
	std::filesystem::create_directories(root / "exports");
	const std::filesystem::path outsideAsset = std::filesystem::absolute(
		TerrainEditorFilePath("file-jobs-outside.AshTerrain"));
	std::filesystem::remove(outsideAsset);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));

	AshEditor::TerrainEditorIntent create = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Create);
	create.create_desc.layout = TerrainTests::MakeSmallLayout();
	create.create_desc.height_mapping = { 0.0f, 1024.0f };
	create.create_desc.flat_height = 0.0f;
	create.asset_path = "../escaped.AshTerrain";
	CHECK_FALSE(service.SubmitIntent(create));
	create.asset_path = outsideAsset;
	CHECK_FALSE(service.SubmitIntent(create));
	create.asset_path = "terrain/wrong-extension.raw";
	CHECK_FALSE(service.SubmitIntent(create));
	CHECK_FALSE(std::filesystem::exists(outsideAsset));

	const std::filesystem::path mismatchedSource = std::filesystem::absolute(
		TerrainEditorFilePath("file-jobs-mismatched-source.png"));
	std::filesystem::remove(mismatchedSource);
	WriteTerrainEditorR16(mismatchedSource, std::vector<uint16_t>(81u, 1u));
	AshEditor::TerrainEditorIntent import = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Import);
	import.asset_path = "terrain/rejected-import.AshTerrain";
	AshEngine::TerrainHeightImportDesc importDesc{};
	importDesc.source_path = mismatchedSource;
	importDesc.format = AshEngine::TerrainHeightFileFormat::RawR16;
	importDesc.target_layout = TerrainTests::MakeSmallLayout();
	importDesc.height_mapping = { 0.0f, 65535.0f };
	importDesc.source_width = 9u;
	importDesc.source_height = 9u;
	import.import_desc = importDesc;
	CHECK_FALSE(service.SubmitIntent(import));
	importDesc.source_path = std::filesystem::absolute(
		TerrainEditorFilePath("file-jobs-missing-source.raw"));
	std::filesystem::remove(importDesc.source_path);
	import.import_desc = importDesc;
	CHECK_FALSE(service.SubmitIntent(import));
	CHECK_FALSE(std::filesystem::exists(root / import.asset_path));

	const std::filesystem::path sourceAsset = root / "source.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourceAsset;
	SaveTerrainEditorSnapshot(sourceAsset, initial);
	REQUIRE(assets.refresh());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourceAsset, initial);
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Export);
	AshEngine::TerrainHeightExportDesc exportDesc{};
	exportDesc.destination_path = "exports/mismatched.png";
	exportDesc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	exportIntent.export_desc = exportDesc;
	CHECK_FALSE(service.SubmitIntent(exportIntent));
	exportDesc.destination_path = "exports/invalid-layer.raw";
	exportDesc.source = AshEngine::TerrainExportSource::HeightEditLayer;
	exportDesc.source_layer_id = {};
	exportIntent.export_desc = exportDesc;
	CHECK_FALSE(service.SubmitIntent(exportIntent));
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));

	std::filesystem::remove(mismatchedSource);
	std::filesystem::remove(outsideAsset);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs fail quickly when no worker is available")
{
	AshEngine::shutdown_threading();
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-no-worker-root");
	std::filesystem::create_directories(root / "terrain");
	const std::filesystem::path relativePath = "terrain/no-worker.AshTerrain";
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	AshEditor::TerrainEditorIntent create = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Create);
	create.asset_path = relativePath;
	create.create_desc.layout = TerrainTests::MakeSmallLayout();
	create.create_desc.height_mapping = { 0.0f, 1024.0f };
	create.create_desc.flat_height = 0.0f;

	const auto started = std::chrono::steady_clock::now();
	REQUIRE(service.SubmitIntent(create));
	CHECK_FALSE(WaitForTerrainFileOperation(
		service, std::chrono::milliseconds(250)));
	const auto elapsed = std::chrono::steady_clock::now() - started;
	CHECK(elapsed < std::chrono::milliseconds(250));
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Failed);
	CHECK(service.GetFileOperationState().error.find("worker") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(root / relativePath));
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs cancel a queued import without publishing partial output")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-cancel-import-root");
	std::filesystem::create_directories(root / "terrain");
	const std::filesystem::path sourcePath = std::filesystem::absolute(
		TerrainEditorFilePath("file-jobs-cancel-import-source.raw"));
	std::filesystem::remove(sourcePath);
	WriteTerrainEditorR16(sourcePath, std::vector<uint16_t>(81u, 32768u));
	const std::filesystem::path relativePath = "terrain/cancelled.AshTerrain";
	const std::filesystem::path finalPath = root / relativePath;
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	TerrainEditorWorkerBlocker blocker{};

	AshEditor::TerrainEditorIntent import = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Import);
	import.asset_path = relativePath;
	AshEngine::TerrainHeightImportDesc importDesc{};
	importDesc.source_path = sourcePath;
	importDesc.format = AshEngine::TerrainHeightFileFormat::RawR16;
	importDesc.target_layout = TerrainTests::MakeSmallLayout();
	importDesc.height_mapping = { 0.0f, 65535.0f };
	importDesc.source_width = 9u;
	importDesc.source_height = 9u;
	import.import_desc = importDesc;
	REQUIRE_MESSAGE(service.SubmitIntent(import), service.GetLastError());
	REQUIRE(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Running);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelFileOperation)));
	blocker.Release();

	REQUIRE(WaitForTerrainFileOperationStatus(
		service, AshEditor::TerrainFileOperationStatus::Cancelled));
	CHECK_FALSE(std::filesystem::exists(finalPath));
	CHECK(assets.find_asset_by_path(relativePath) == nullptr);
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	std::filesystem::remove(sourcePath);
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file jobs cancel active Import and Export workers exactly once before publication")
{
	bool importCase = false;
	SUBCASE("Import")
	{
		importCase = true;
	}
	SUBCASE("Export")
	{
		importCase = false;
	}

	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root = TerrainEditorAssetRoot(
		importCase ? "file-jobs-active-cancel-import-root" :
		"file-jobs-active-cancel-export-root");
	std::filesystem::create_directories(root / "terrain");
	std::filesystem::create_directories(root / "exports");
	const std::filesystem::path sourceAsset = root / "terrain/source.AshTerrain";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourceAsset;
	SaveTerrainEditorSnapshot(sourceAsset, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourceAsset, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));

	const AshEditor::TerrainFileOperationKind operationKind = importCase
		? AshEditor::TerrainFileOperationKind::Import
		: AshEditor::TerrainFileOperationKind::Export;
	TerrainEditorFileJobHookBlocker hook(
		operationKind,
		AshEditor::TerrainEditorService::FileJobTestPoint::BeforeCodec);
	service.SetFileJobTestHook(hook.Callback());
	std::filesystem::path finalPath{};
	if (importCase)
	{
		const std::filesystem::path sourceHeight = std::filesystem::absolute(
			TerrainEditorFilePath("file-jobs-active-cancel-import.raw"));
		std::filesystem::remove(sourceHeight);
		WriteTerrainEditorR16(sourceHeight, std::vector<uint16_t>(81u, 32768u));
		finalPath = root / "terrain/cancel-active.AshTerrain";
		AshEditor::TerrainEditorIntent import = MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::Import);
		import.asset_path = "terrain/cancel-active.AshTerrain";
		AshEngine::TerrainHeightImportDesc importDesc{};
		importDesc.source_path = sourceHeight;
		importDesc.format = AshEngine::TerrainHeightFileFormat::RawR16;
		importDesc.target_layout = TerrainTests::MakeSmallLayout();
		importDesc.height_mapping = { 0.0f, 65535.0f };
		importDesc.source_width = 9u;
		importDesc.source_height = 9u;
		import.import_desc = importDesc;
		REQUIRE_MESSAGE(service.SubmitIntent(import), service.GetLastError());
		hook.WaitUntilEntered();
		REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::CancelFileOperation)));
		CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::CancelFileOperation)));
		hook.Release();
		REQUIRE(WaitForTerrainFileOperationStatus(
			service, AshEditor::TerrainFileOperationStatus::Cancelled));
		std::filesystem::remove(sourceHeight);
	}
	else
	{
		finalPath = root / "exports/cancel-active.raw";
		AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::Export);
		AshEngine::TerrainHeightExportDesc exportDesc{};
		exportDesc.destination_path = "exports/cancel-active.raw";
		exportDesc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
		exportIntent.export_desc = exportDesc;
		REQUIRE_MESSAGE(service.SubmitIntent(exportIntent), service.GetLastError());
		hook.WaitUntilEntered();
		REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::CancelFileOperation)));
		CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
			AshEditor::TerrainEditorIntent::Kind::CancelFileOperation)));
		hook.Release();
		REQUIRE(WaitForTerrainFileOperationStatus(
			service, AshEditor::TerrainFileOperationStatus::Cancelled));
	}

	service.Update();
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Cancelled);
	CHECK(hook.state->entry_count.load(std::memory_order_acquire) == 1u);
	CHECK_FALSE(std::filesystem::exists(finalPath));
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	std::filesystem::remove_all(root);
}

TEST_CASE("Terrain editor file job cancellation loses atomically once final publication begins")
{
	AshEngine::shutdown_threading();
	AshEngine::EngineThreadingConfig threadingConfig{};
	threadingConfig.worker_thread_count = 1u;
	REQUIRE(AshEngine::initialize_threading(threadingConfig));
	TerrainEditorThreadingScope threadingScope{};

	const std::filesystem::path root =
		TerrainEditorAssetRoot("file-jobs-cancel-publish-race-root");
	std::filesystem::create_directories(root / "exports");
	const std::filesystem::path sourcePath = root / "source.AshTerrain";
	const std::filesystem::path finalPath = root / "exports/published.raw";
	AshEngine::TerrainAssetSnapshot initial = MakeEditorStrokeSnapshot();
	initial.source_path = sourcePath;
	SaveTerrainEditorSnapshot(sourcePath, initial);
	AshEngine::AssetDatabase assets = AshEngine::AssetDatabase::create(root);
	REQUIRE(assets.is_valid());
	BindTerrainEditorSnapshotToAssetDatabase(assets, sourcePath, initial);
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(assets, commands));
	REQUIRE(service.OpenSnapshotForAuthoring(initial));
	TerrainEditorFileJobHookBlocker hook(
		AshEditor::TerrainFileOperationKind::Export,
		AshEditor::TerrainEditorService::FileJobTestPoint::AfterPublishClaim);
	service.SetFileJobTestHook(hook.Callback());

	AshEditor::TerrainEditorIntent exportIntent = MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Export);
	AshEngine::TerrainHeightExportDesc exportDesc{};
	exportDesc.destination_path = "exports/published.raw";
	exportDesc.format = AshEngine::TerrainHeightFileFormat::RawR32F;
	exportIntent.export_desc = exportDesc;
	REQUIRE_MESSAGE(service.SubmitIntent(exportIntent), service.GetLastError());
	hook.WaitUntilEntered();
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelFileOperation)));
	CHECK(service.GetLastError().find("publication") != std::string::npos);
	hook.Release();
	REQUIRE_MESSAGE(WaitForTerrainFileOperation(service),
		service.GetFileOperationState().error);
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Succeeded);
	CHECK(hook.state->entry_count.load(std::memory_order_acquire) == 1u);
	CHECK(std::filesystem::exists(finalPath));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelFileOperation)));
	service.Update();
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Succeeded);
	CHECK_FALSE(HasTerrainEditorFileJobTemporary(root));
	std::filesystem::remove_all(root);
}

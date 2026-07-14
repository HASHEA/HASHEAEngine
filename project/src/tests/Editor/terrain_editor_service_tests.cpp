#include "Core/TerrainEditorSessionCore.h"
#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/IEditorCommandExecutor.h"
#include "Base/hthreading.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainContainer.h"
#include "Services/TerrainEditorService.h"
#include "Terrain/TerrainTestUtils.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
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

		uint32_t execute_count = 0;
		uint32_t record_count = 0;
		std::vector<std::unique_ptr<AshEditor::EditorCommand>> commands{};
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
	CHECK(service.find("load_terrain_by_id_async") != std::string::npos);
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

	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::Save)));
	CHECK_FALSE(WaitForTerrainFileOperation(service));
	CHECK(service.GetFileOperationState().status ==
		AshEditor::TerrainFileOperationStatus::Failed);
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

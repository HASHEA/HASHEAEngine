#include "Core/TerrainEditorSessionCore.h"
#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/TerrainComposition.h"
#include "Services/TerrainEditorService.h"
#include "Terrain/TerrainTestUtils.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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
	REQUIRE(service.SubmitIntent(begin));
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	CHECK(commands.record_count == 0u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration);

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
	CHECK(commands.record_count == 0u);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration);
	CHECK_FALSE(service.GetPreviewState().stroke_active);
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetWorkingSet()->dirty_components.empty());
	CHECK(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));

	AshEditor::TerrainEditorIntent invalidMetric = MakeBeginStrokeIntent();
	invalidMetric.brush_metric.world_meters_per_terrain_meter.x = 0.0f;
	CHECK_FALSE(service.SubmitIntent(invalidMetric));
	invalidMetric = MakeBeginStrokeIntent();
	invalidMetric.brush_metric.world_meters_per_terrain_meter.y = -1.0f;
	CHECK_FALSE(service.SubmitIntent(invalidMetric));
	invalidMetric = MakeBeginStrokeIntent();
	invalidMetric.brush_metric.world_meters_per_terrain_meter.x =
		std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(service.SubmitIntent(invalidMetric));
}

TEST_CASE("Terrain editor reserves a generation for history rollback")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	AshEngine::TerrainAssetSnapshot snapshot = MakeEditorStrokeSnapshot();
	snapshot.content_generation = std::numeric_limits<uint64_t>::max() - 1u;
	REQUIRE(service.OpenSnapshotForAuthoring(snapshot));

	CHECK_FALSE(service.SubmitIntent(MakeBeginStrokeIntent()));
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(1.0f, 1.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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

TEST_CASE("Terrain editor undo invalidates pending composition and publishes the undo generation")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));
	const uint64_t editedGeneration = service.GetWorkingSet()->content_generation;
	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));

	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	CHECK_FALSE(commands.UndoLatest(context));
	CHECK(service.GetWorkingSet()->content_generation == editedGeneration);
	CHECK(service.GetPreviewState().stroke_active);
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
}

TEST_CASE("Terrain editor quarantines a malformed rollback claim")
{
	MalformedRollbackTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	const auto initialPublishedSnapshot = service.GetPublishedSnapshot();

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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
	CHECK_FALSE(service.SubmitIntent(MakeBeginStrokeIntent()));

	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	AshEditor::TerrainEditorIntent smooth = MakeBeginStrokeIntent();
	smooth.brush.tool = AshEngine::TerrainBrushTool::Smooth;
	CHECK_FALSE(service.SubmitIntent(smooth));

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

TEST_CASE("Terrain editor invalid raw samples fail without mutation or history")
{
	RecordingTerrainCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.GetWorkingSet() != nullptr);
	const uint64_t initialGeneration = service.GetWorkingSet()->content_generation;

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
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
	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));

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

	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
	REQUIRE(service.SubmitIntent(MakeStrokeSampleIntent(2.0f, 2.0f)));
	CHECK_FALSE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::EndStroke)));

	CHECK(commands.record_count == 1u);
	CHECK(commands.command_received);
	CHECK(service.GetWorkingSet()->content_generation == initialGeneration + 1u);
	CHECK_FALSE(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
	CHECK_FALSE(service.HasPendingComposition());
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);
	CHECK_FALSE(service.SubmitIntent(MakeBeginStrokeIntent()));

	service.Update();
	CHECK(service.GetPublishedSnapshot() == initialPublishedSnapshot);
	CHECK_FALSE(service.GetWorkingSet()->dirty_components.empty());

	REQUIRE(service.OpenSnapshotForAuthoring(MakeEditorStrokeSnapshot()));
	REQUIRE(service.SubmitIntent(MakeBeginStrokeIntent()));
	REQUIRE(service.SubmitIntent(MakeSimpleTerrainIntent(
		AshEditor::TerrainEditorIntent::Kind::CancelStroke)));
}

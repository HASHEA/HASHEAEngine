#include "Core/EditorCommand.h"
#include "Core/EditorIds.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Asset/TerrainComposition.h"
#include "Panels/Terrain/TerrainModeState.h"
#include "Services/TerrainEditorService.h"
#include "Terrain/TerrainTestUtils.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	std::string ReadTerrainContractText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	AshEngine::TerrainLayerId MakeTerrainModeLayerId()
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = 0x4du;
		id.bytes[15] = 0x6fu;
		return id;
	}

	AshEngine::TerrainAssetSnapshot MakeTerrainModeSnapshot()
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> flat{};
		std::string error{};
		if (!AshEngine::create_flat_terrain_snapshot(
				7001u,
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
		layer.id = MakeTerrainModeLayerId();
		layer.name = "Sculpt";
		layer.height_blend_mode = AshEngine::TerrainHeightBlendMode::Additive;
		snapshot.edit_layers = std::make_shared<const std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ std::move(layer) });
		return snapshot;
	}

	class TerrainModeCommandExecutor final : public AshEditor::IEditorCommandExecutor
	{
	public:
		AshEditor::EditorCommandRecordResult record_result =
			AshEditor::EditorCommandRecordResult::Recorded;

		bool ExecuteCommand(std::unique_ptr<AshEditor::EditorCommand>) override
		{
			return false;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> command) override
		{
			return command
				? record_result
				: AshEditor::EditorCommandRecordResult::RollbackFailed;
		}
	};
}

TEST_CASE("Terrain Mode ids are stable Editor contracts")
{
	CHECK(std::string(AshEditor::EditorPanelIds::TerrainMode) == "terrain_mode");
	CHECK(std::string(AshEditor::EditorWindowTitles::TerrainMode) == "Terrain");
}

TEST_CASE("Terrain Mode is a UIContext panel backed by TerrainEditorService")
{
	const std::string panel = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModePanel.cpp");
	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	const std::string bootstrap = ReadTerrainContractText(
		"project/src/editor/App/PanelBootstrapper.cpp");
	const std::string application = ReadTerrainContractText(
		"project/src/editor/App/EditorApplicationImpl.cpp");
	const std::string dock = ReadTerrainContractText(
		"project/src/editor/Shell/DockLayoutController.cpp");

	REQUIRE_FALSE(panel.empty());
	REQUIRE_FALSE(widgets.empty());
	const std::string terrainUi = panel + widgets;
	CHECK(std::regex_search(terrainUi, std::regex(R"(\bbegin_tab_bar\s*\()")));
	CHECK(std::regex_search(terrainUi, std::regex(R"(\bbegin_tab_item\s*\(\s*"Manage")")));
	CHECK(std::regex_search(terrainUi, std::regex(R"(\bbegin_tab_item\s*\(\s*"Sculpt")")));
	CHECK(std::regex_search(terrainUi, std::regex(R"(\bbegin_tab_item\s*\(\s*"Paint")")));
	CHECK(std::regex_search(terrainUi, std::regex(R"(\bbegin_tab_item\s*\(\s*"Layers")")));
	CHECK(panel.find("SubmitIntent(") != std::string::npos);
	CHECK(panel.find("EditorSelectionChangedEvent") != std::string::npos);
	CHECK(panel.find("AshEngine::AssetType::Terrain") != std::string::npos);
	CHECK(panel.find("TerrainEditorIntent::Kind::SelectAsset") != std::string::npos);
	CHECK(terrainUi.find("ImGui::") == std::string::npos);
	CHECK(terrainUi.find("Graphics/") == std::string::npos);
	CHECK(terrainUi.find("Vulkan") == std::string::npos);
	CHECK(terrainUi.find("DirectX12") == std::string::npos);

	CHECK(bootstrap.find("CreatePanel<TerrainModePanel>") != std::string::npos);
	CHECK(bootstrap.find("pTerrainEditorService") != std::string::npos);
	CHECK(bootstrap.find("pTerrainModePanel->SetOpen(false)") != std::string::npos);
	CHECK(bootstrap.find("pTerrainModePanel->BindEventBus(&refEventBus)") != std::string::npos);
	CHECK(application.find("_upTerrainEditorService.get()") != std::string::npos);
	CHECK(std::regex_search(
		dock,
		std::regex(R"(dock_builder_dock_window\s*\(\s*EditorWindowTitles::TerrainMode\s*,\s*uInspectorNode\s*\))")));
}

TEST_CASE("Terrain Mode exposes approved manage sculpt paint and stable layer actions")
{
	const std::string panel = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModePanel.cpp");
	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	const std::string state = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeState.h");
	const std::string terrainUi = panel + widgets + state;

	for (const char* label : {
		"Create Flat", "Import Heightmap", "Export Heightmap", "Save", "Save As", "Reload", "Optimize" })
	{
		CHECK_MESSAGE(terrainUi.find(label) != std::string::npos, label);
	}
	for (const char* tool : { "Raise", "Lower", "Smooth", "Flatten", "Noise", "Paint", "Erase" })
	{
		CHECK_MESSAGE(terrainUi.find(tool) != std::string::npos, tool);
	}
	for (const char* action : {
		"TerrainLayerActionKind::Add",
		"TerrainLayerActionKind::Delete",
		"TerrainLayerActionKind::Duplicate",
		"TerrainLayerActionKind::Rename",
		"TerrainLayerActionKind::Move",
		"TerrainLayerActionKind::SetVisible",
		"TerrainLayerActionKind::SetLocked",
		"TerrainLayerActionKind::SetOpacity" })
	{
		CHECK_MESSAGE(terrainUi.find(action) != std::string::npos, action);
	}
	CHECK(terrainUi.find("k_terrain_material_layer_count") != std::string::npos);
	CHECK(terrainUi.find("TerrainLayerId") != std::string::npos);
	CHECK(terrainUi.find("DrawUnavailableFileOperation") != std::string::npos);
}

TEST_CASE("Terrain Mode locks tab changes while a stroke owns the authoring configuration")
{
	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");

	CHECK(widgets.find("const bool lockTabs = refView.preview.stroke_active;") != std::string::npos);
	CHECK(widgets.find("refUi.begin_disabled(lockTabs);") != std::string::npos);
	CHECK(widgets.find("configChanged && !lockTabs") != std::string::npos);
}

TEST_CASE("Terrain Mode layer drafts survive unrelated edits and reset across assets")
{
	AshEditor::TerrainModeState state{};
	AshEngine::TerrainEditLayer layer{};
	layer.id = MakeTerrainModeLayerId();
	layer.name = "Committed";
	layer.strength = 0.75f;

	state.SyncLayerDrafts(7001u, layer);
	CHECK(state.rename_layer_name == "Committed");
	CHECK(state.opacity_draft == doctest::Approx(0.75f));

	state.rename_layer_name = "Local rename";
	state.rename_draft_dirty = true;
	state.opacity_draft = 0.25f;
	state.opacity_draft_dirty = true;
	layer.visible = false;
	state.SyncLayerDrafts(7001u, layer);
	CHECK(state.rename_layer_name == "Local rename");
	CHECK(state.opacity_draft == doctest::Approx(0.25f));

	layer.name = "Undo rename";
	layer.strength = 0.5f;
	state.SyncLayerDrafts(7001u, layer);
	CHECK(state.rename_layer_name == "Local rename");
	CHECK(state.opacity_draft == doctest::Approx(0.25f));

	state.rename_draft_dirty = false;
	state.opacity_draft_dirty = false;
	state.SyncLayerDrafts(7001u, layer);
	CHECK(state.rename_layer_name == "Undo rename");
	CHECK(state.opacity_draft == doctest::Approx(0.5f));

	state.rename_layer_name = "Rejected rename";
	state.rename_draft_dirty = false;
	state.opacity_draft = 0.125f;
	state.opacity_draft_dirty = false;
	state.SyncLayerDrafts(7001u, layer);
	CHECK(state.rename_layer_name == "Undo rename");
	CHECK(state.opacity_draft == doctest::Approx(0.5f));

	state.rename_layer_name = "Wrong asset draft";
	state.rename_draft_dirty = true;
	state.SyncLayerDrafts(8002u, layer);
	CHECK(state.rename_layer_name == "Undo rename");
	CHECK_FALSE(state.rename_draft_dirty);
}

TEST_CASE("Terrain Mode authoring configuration is validated and service owned")
{
	TerrainModeCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeTerrainModeSnapshot()));

	AshEditor::TerrainEditorIntent configure{};
	configure.kind = AshEditor::TerrainEditorIntent::Kind::ConfigureAuthoring;
	configure.mode = AshEditor::TerrainEditorMode::Sculpt;
	configure.brush.tool = AshEngine::TerrainBrushTool::Noise;
	configure.brush.radius_meters = 64.0f;
	configure.brush.strength = 0.35f;
	configure.brush.falloff = 0.7f;
	configure.brush.stroke_spacing_meters = 4.0f;
	configure.brush.material_layer_index = 3u;
	configure.brush.random_seed = 0x123456789abcdef0ull;
	configure.brush.layer_id = {};
	REQUIRE(service.SubmitIntent(configure));

	const AshEditor::TerrainAuthoringConfig accepted = service.GetAuthoringConfig();
	CHECK(accepted.mode == AshEditor::TerrainEditorMode::Sculpt);
	CHECK(accepted.brush.tool == AshEngine::TerrainBrushTool::Noise);
	CHECK(accepted.brush.radius_meters == doctest::Approx(64.0f));
	CHECK(accepted.brush.material_layer_index == 3u);
	CHECK(accepted.brush.random_seed == 0x123456789abcdef0ull);
	CHECK(accepted.brush.layer_id == service.GetSelectedLayerId());

	AshEditor::TerrainEditorIntent invalid = configure;
	invalid.brush.radius_meters = std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(service.SubmitIntent(invalid));
	CHECK(service.GetAuthoringConfig().brush.radius_meters == doctest::Approx(64.0f));

	invalid = configure;
	invalid.brush.material_layer_index = AshEngine::k_terrain_material_layer_count;
	CHECK_FALSE(service.SubmitIntent(invalid));
	CHECK(service.GetAuthoringConfig().brush.material_layer_index == 3u);

	invalid = configure;
	invalid.mode = AshEditor::TerrainEditorMode::Paint;
	invalid.brush.tool = AshEngine::TerrainBrushTool::Raise;
	CHECK_FALSE(service.SubmitIntent(invalid));
	CHECK(service.GetAuthoringConfig().mode == AshEditor::TerrainEditorMode::Sculpt);

	AshEditor::TerrainEditorIntent addAlpha{};
	addAlpha.kind = AshEditor::TerrainEditorIntent::Kind::LayerAction;
	addAlpha.layer_action.kind = AshEditor::TerrainLayerActionKind::Add;
	addAlpha.layer_action.name = "Alpha";
	addAlpha.layer_action.blend_mode = AshEngine::TerrainHeightBlendMode::Alpha;
	REQUIRE(service.SubmitIntent(addAlpha));
	CHECK(service.GetAuthoringConfig().brush.layer_id == service.GetSelectedLayerId());
	CHECK(service.GetAuthoringConfig().brush.tool == AshEngine::TerrainBrushTool::Smooth);
}

TEST_CASE("Terrain Mode authoring configuration is the only stroke source")
{
	TerrainModeCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeTerrainModeSnapshot()));

	AshEditor::TerrainEditorIntent begin{};
	begin.kind = AshEditor::TerrainEditorIntent::Kind::BeginStroke;
	begin.asset_id = service.GetSelectedAssetId();
	begin.layer_id = service.GetSelectedLayerId();
	begin.brush = service.GetAuthoringConfig().brush;
	begin.brush.layer_id = service.GetSelectedLayerId();
	begin.brush_metric.world_meters_per_terrain_meter = { 1.0f, 1.0f };
	CHECK_FALSE(service.SubmitIntent(begin));
	if (service.GetPreviewState().stroke_active)
	{
		AshEditor::TerrainEditorIntent cleanup{};
		cleanup.kind = AshEditor::TerrainEditorIntent::Kind::CancelStroke;
		REQUIRE(service.SubmitIntent(cleanup));
	}

	AshEditor::TerrainEditorIntent configure{};
	configure.kind = AshEditor::TerrainEditorIntent::Kind::ConfigureAuthoring;
	configure.mode = AshEditor::TerrainEditorMode::Sculpt;
	configure.brush = begin.brush;
	configure.brush.radius_meters = 32.0f;
	configure.brush.strength = 0.25f;
	configure.brush.falloff = 0.75f;
	configure.brush.stroke_spacing_meters = 2.0f;
	configure.brush.random_seed = 44u;
	REQUIRE(service.SubmitIntent(configure));

	AshEditor::TerrainEditorIntent stale = begin;
	stale.brush = service.GetAuthoringConfig().brush;
	stale.brush.radius_meters = 64.0f;
	CHECK_FALSE(service.SubmitIntent(stale));

	begin.brush = service.GetAuthoringConfig().brush;
	REQUIRE(service.SubmitIntent(begin));
	AshEditor::TerrainEditorIntent cancel{};
	cancel.kind = AshEditor::TerrainEditorIntent::Kind::CancelStroke;
	REQUIRE(service.SubmitIntent(cancel));

	configure.mode = AshEditor::TerrainEditorMode::Layers;
	configure.brush = service.GetAuthoringConfig().brush;
	REQUIRE(service.SubmitIntent(configure));
	begin.brush = service.GetAuthoringConfig().brush;
	CHECK_FALSE(service.SubmitIntent(begin));
}

TEST_CASE("Terrain Mode asset selection preserves a dirty authoring session")
{
	TerrainModeCommandExecutor commands{};
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeTerrainModeSnapshot()));

	AshEditor::TerrainEditorIntent add{};
	add.kind = AshEditor::TerrainEditorIntent::Kind::LayerAction;
	add.layer_action.kind = AshEditor::TerrainLayerActionKind::Add;
	add.layer_action.name = "Unsaved";
	add.layer_action.blend_mode = AshEngine::TerrainHeightBlendMode::Additive;
	REQUIRE(service.SubmitIntent(add));
	service.Update();
	REQUIRE(service.HasDirtyAssets());
	REQUIRE_FALSE(service.HasPendingComposition());
	const AshEngine::TerrainAssetId dirtyAssetId = service.GetSelectedAssetId();

	AshEditor::TerrainEditorIntent close{};
	close.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	close.asset_id = 0u;
	CHECK_FALSE(service.SubmitIntent(close));
	CHECK(service.GetSelectedAssetId() == dirtyAssetId);
	CHECK(service.GetWorkingSet() != nullptr);
	CHECK(service.HasDirtyAssets());
	CHECK(service.GetLastError().find("dirty") != std::string::npos);
}

TEST_CASE("Terrain Mode can leave an authoring session quarantined by unverifiable history")
{
	TerrainModeCommandExecutor commands{};
	commands.record_result = AshEditor::EditorCommandRecordResult::RollbackFailed;
	AshEditor::TerrainEditorService service{};
	REQUIRE(service.Initialize(commands));
	REQUIRE(service.OpenSnapshotForAuthoring(MakeTerrainModeSnapshot()));

	AshEditor::TerrainEditorIntent rename{};
	rename.kind = AshEditor::TerrainEditorIntent::Kind::LayerAction;
	rename.layer_action.kind = AshEditor::TerrainLayerActionKind::Rename;
	rename.layer_action.layer_id = service.GetSelectedLayerId();
	rename.layer_action.name = "Untracked";
	CHECK_FALSE(service.SubmitIntent(rename));
	REQUIRE(service.GetWorkingSet() != nullptr);
	CHECK(service.HasDirtyAssets());
	CHECK(service.GetPreviewState().query_status == AshEngine::TerrainQueryStatus::Failed);

	AshEditor::TerrainEditorIntent close{};
	close.kind = AshEditor::TerrainEditorIntent::Kind::SelectAsset;
	close.asset_id = 0u;
	CHECK(service.SubmitIntent(close));
	CHECK(service.GetSelectedAssetId() == 0u);
	CHECK(service.GetWorkingSet() == nullptr);
	CHECK_FALSE(service.HasDirtyAssets());
}

TEST_CASE("Terrain viewport interaction keeps camera terrain gizmo and selection ordering")
{
	const std::string interaction = ReadTerrainContractText(
		"project/src/editor/Panels/ViewportPanelInteraction.cpp");
	const size_t camera = interaction.rfind("UpdateViewportInput(");
	const size_t terrain = interaction.rfind("ViewportPanelTerrainInteraction::Update(");
	const size_t gizmo = interaction.rfind("UpdateSceneGizmoInteraction(");
	const size_t selection = interaction.rfind("UpdateSceneViewportSelectionInput(");

	REQUIRE(camera != std::string::npos);
	REQUIRE(terrain != std::string::npos);
	REQUIRE(gizmo != std::string::npos);
	REQUIRE(selection != std::string::npos);
	CHECK(camera < terrain);
	CHECK(terrain < gizmo);
	CHECK(gizmo < selection);
	CHECK(interaction.find("terrainInteraction.consume_mouse_left ||") != std::string::npos);
	CHECK(interaction.find("ClearScenePendingPick(refSceneBoxSelectionState);") != std::string::npos);
}

TEST_CASE("Terrain viewport interaction reuses rays validates assets and handles cancellation")
{
	const std::string interaction = ReadTerrainContractText(
		"project/src/editor/Panels/ViewportPanelInteraction.cpp");
	const std::string terrainInteraction = ReadTerrainContractText(
		"project/src/editor/Panels/ViewportPanelTerrainInteraction.cpp");
	const std::string support = ReadTerrainContractText(
		"project/src/editor/Panels/ViewportPanelInteractionSupport.cpp");
	const std::string panel = ReadTerrainContractText(
		"project/src/editor/Panels/ViewportPanel.cpp");
	const std::string toolbar = ReadTerrainContractText(
		"project/src/editor/Panels/ViewportPanelToolbar.cpp");
	const std::string service = ReadTerrainContractText(
		"project/src/editor/Services/TerrainEditorService.cpp");
	const std::string bootstrap = ReadTerrainContractText(
		"project/src/editor/App/PanelBootstrapper.cpp");

	CHECK(support.find("TryBuildViewportRay(") != std::string::npos);
	CHECK(terrainInteraction.find("TryBuildSceneInteractionRay(") != std::string::npos);
	const size_t rayBuild = terrainInteraction.find("TryBuildSceneInteractionRay(");
	const size_t failedRay = terrainInteraction.find(
		"query.status = AshEngine::TerrainQueryStatus::Failed;",
		rayBuild);
	REQUIRE(rayBuild != std::string::npos);
	REQUIRE(failedRay != std::string::npos);
	CHECK(rayBuild < failedRay);
	CHECK(terrainInteraction.find("FindByPath(terrain.asset_path)") != std::string::npos);
	CHECK(terrainInteraction.find(
		"pHitAsset->id != refDeps.pTerrainEditorService->GetSelectedAssetId()") !=
		std::string::npos);
	CHECK(terrainInteraction.find("sample_spacing_meters") != std::string::npos);
	CHECK(terrainInteraction.find("worldTransform[0]") != std::string::npos);
	CHECK(terrainInteraction.find("worldTransform[2]") != std::string::npos);
	CHECK(terrainInteraction.find("UIKey::Escape") != std::string::npos);
	CHECK(terrainInteraction.find("UIKey::F") != std::string::npos);
	CHECK(terrainInteraction.find("vecMouseWheelDelta") != std::string::npos);
	CHECK(terrainInteraction.find("route.release_mouse_left_press") != std::string::npos);
	CHECK(panel.find("CancelTerrainStrokeIfActive") != std::string::npos);
	CHECK(toolbar.find("begin_disabled(terrainOwnsSceneTools)") != std::string::npos);
	CHECK(service.find("refIntent.asset_id != pWorkingSet->asset_id") != std::string::npos);
	CHECK(bootstrap.find("deps.pTerrainEditorService = refContext.pTerrainEditorService;") !=
		std::string::npos);
}

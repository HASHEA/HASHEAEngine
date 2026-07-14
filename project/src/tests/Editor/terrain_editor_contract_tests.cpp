#include "Core/EditorCommand.h"
#include "Core/EditorIds.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Asset/TerrainComposition.h"
#include "Panels/Terrain/TerrainModeState.h"
#include "Services/TerrainBrushOverlayRenderer.h"
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

#include <glm/mat4x4.hpp>
#include <glm/geometric.hpp>

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
		"Create Flat", "Import Heightmap", "Export Heightmap", "Save", "Save Copy As", "Reload", "Optimize" })
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

TEST_CASE("Terrain Mode wires generation-aware file operations and keeps reload unavailable")
{
	const std::string panel = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModePanel.cpp");
	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	const std::string state = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeState.h");

	CHECK(state.find("save_as_asset_path") != std::string::npos);
	CHECK(panel.find("GetFileOperationState()") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::Save") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::SaveAs") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::Optimize") != std::string::npos);
	CHECK(widgets.find("intent.asset_path = refState.save_as_asset_path") != std::string::npos);
	CHECK(widgets.find("const bool canOptimize") != std::string::npos);
	CHECK(widgets.find("!refView.dirty") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::AwaitingPublication") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::Running") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::Succeeded") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::Failed") != std::string::npos);
	CHECK(widgets.find(
		"DrawUnavailableFileOperation(refUi, \"Reload\", \"Available with conflict resolution.\")") !=
		std::string::npos);
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

TEST_CASE("Terrain Ctrl+S waits for referenced Terrain before saving the Scene")
{
	const std::string coordinator = ReadTerrainContractText(
		"project/src/editor/App/EditorActionCoordinator.cpp");
	const std::string application = ReadTerrainContractText(
		"project/src/editor/App/EditorApplicationImpl.cpp");
	const size_t handleSave = coordinator.find("EditorActionCoordinator::HandleSaveScene()");
	const size_t saveTerrain = coordinator.find(
		"SaveDirtyReferencedTerrains(pending)", handleSave);
	const size_t waitState = coordinator.find(
		"_optPendingSceneSave = std::move(pending)", saveTerrain);
	const size_t saveScene = coordinator.find("SaveSceneNow(pathScene", saveTerrain);
	REQUIRE(handleSave != std::string::npos);
	REQUIRE(saveTerrain != std::string::npos);
	REQUIRE(waitState != std::string::npos);
	REQUIRE(saveScene != std::string::npos);
	CHECK(saveTerrain < waitState);
	CHECK(waitState < saveScene);
	CHECK(coordinator.find("TerrainFileOperationStatus::Succeeded") != std::string::npos);
	CHECK(coordinator.find("TerrainFileOperationStatus::Failed") != std::string::npos);
	CHECK(coordinator.find("get_content_epoch()") != std::string::npos);
	CHECK(coordinator.find("canMarkCurrentHistorySaved") != std::string::npos);
	CHECK(coordinator.find(
		"existing.content_generation != pWorkingSet->content_generation") !=
		std::string::npos);

	const size_t terrainUpdate = application.find("_upTerrainEditorService->Update()");
	const size_t actionUpdate = application.find("_upActionCoordinator->Update()");
	const size_t panelUpdate = application.find("_upPanelManager->Update()");
	REQUIRE(terrainUpdate != std::string::npos);
	REQUIRE(actionUpdate != std::string::npos);
	REQUIRE(panelUpdate != std::string::npos);
	CHECK(terrainUpdate < actionUpdate);
	CHECK(actionUpdate < panelUpdate);
}

TEST_CASE("Terrain Ctrl+S batches catalog misses before resolving or failing closed")
{
	const std::string coordinator = ReadTerrainContractText(
		"project/src/editor/App/EditorActionCoordinator.cpp");
	const size_t saveTerrains = coordinator.find(
		"EditorActionCoordinator::SaveDirtyReferencedTerrains");
	const size_t catalogLookup = coordinator.find(
		"refAssetDatabaseService.FindByPath", saveTerrains);
	const size_t catalogMiss = coordinator.find("if (!pAsset)", catalogLookup);
	const size_t collectFallback = coordinator.find(
		"unresolvedTerrainReferences.push_back(pathReference)", catalogMiss);
	const size_t classifyGuard = coordinator.find(
		"if (!referenced && !unresolvedTerrainReferences.empty())", collectFallback);
	const size_t classifyFallback = coordinator.find(
		"ClassifyCurrentAssetReferences(unresolvedTerrainReferences)", classifyGuard);
	const size_t currentMatch = coordinator.find(
		"TerrainAssetReferenceMatch::Current", classifyFallback);
	const size_t currentWins = coordinator.find("referenced = true;", currentMatch);
	const size_t unsafeMatch = coordinator.find(
		"TerrainAssetReferenceMatch::Unsafe", currentWins);
	const size_t failClosed = coordinator.find(
		"Scene save cannot safely identify an unresolved Terrain reference", unsafeMatch);
	const size_t failClosedReturn = coordinator.find(
		"return DirtyTerrainSaveStartResult::Failed;", failClosed);

	REQUIRE(saveTerrains != std::string::npos);
	REQUIRE(catalogLookup != std::string::npos);
	REQUIRE(catalogMiss != std::string::npos);
	REQUIRE(collectFallback != std::string::npos);
	REQUIRE(classifyGuard != std::string::npos);
	REQUIRE(classifyFallback != std::string::npos);
	REQUIRE(currentMatch != std::string::npos);
	REQUIRE(currentWins != std::string::npos);
	REQUIRE(unsafeMatch != std::string::npos);
	REQUIRE(failClosed != std::string::npos);
	REQUIRE(failClosedReturn != std::string::npos);
	CHECK(catalogLookup < catalogMiss);
	CHECK(catalogMiss < collectFallback);
	CHECK(collectFallback < classifyGuard);
	CHECK(classifyGuard < classifyFallback);
	CHECK(classifyFallback < currentMatch);
	CHECK(currentMatch < currentWins);
	CHECK(currentWins < unsafeMatch);
	CHECK(failClosed < failClosedReturn);
}

TEST_CASE("Terrain file worker owns immutable inputs and resolves worker exceptions")
{
	const std::string service = ReadTerrainContractText(
		"project/src/editor/Services/TerrainEditorService.cpp");
	const size_t dispatch = service.find("dispatch_background_task(");
	const size_t capture = service.find("[dispatchState,", dispatch);
	const size_t captureEnd = service.find("]() mutable", capture);
	const size_t enqueueComplete = service.find(
		"dispatchState->enqueue_in_progress.store", captureEnd);
	const size_t workerCatch = service.find(
		"catch (const std::exception& exception)", captureEnd);
	const size_t workerResolve = service.find(
		"dispatchState->Resolve(false, exception.what())", workerCatch);
	REQUIRE(dispatch != std::string::npos);
	REQUIRE(capture != std::string::npos);
	REQUIRE(captureEnd != std::string::npos);
	REQUIRE(enqueueComplete != std::string::npos);
	REQUIRE(workerCatch != std::string::npos);
	REQUIRE(workerResolve != std::string::npos);
	CHECK(service.substr(capture, captureEnd - capture).find("this") == std::string::npos);
	CHECK(workerCatch < enqueueComplete);
	CHECK(workerResolve < enqueueComplete);
}

TEST_CASE("Terrain Save Copy As stages and atomically publishes without replacement")
{
	const std::string service = ReadTerrainContractText(
		"project/src/editor/Services/TerrainEditorService.cpp");
	const size_t helper = service.find("SaveTerrainCopyNew(");
	const size_t helperEnd = service.find("bool IsTerrainFileOperationInProgress", helper);
	const size_t temporaryGuard = service.find("TerrainCopyTemporaryGuard", helper);
	const size_t stagedWrite = service.find(
		"save_terrain_container_incremental(temporary", helper);
	const size_t atomicPublish = service.find("MoveFileExW(", stagedWrite);
	const size_t writeThrough = service.find("MOVEFILE_WRITE_THROUGH", atomicPublish);

	REQUIRE(helper != std::string::npos);
	REQUIRE(helperEnd != std::string::npos);
	REQUIRE(temporaryGuard != std::string::npos);
	REQUIRE(stagedWrite != std::string::npos);
	REQUIRE(atomicPublish != std::string::npos);
	REQUIRE(writeThrough != std::string::npos);
	CHECK(helper < temporaryGuard);
	CHECK(temporaryGuard < stagedWrite);
	CHECK(stagedWrite < atomicPublish);
	CHECK(atomicPublish < writeThrough);
	CHECK(service.substr(helper, helperEnd - helper).find("MOVEFILE_REPLACE_EXISTING") ==
		std::string::npos);
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

namespace
{
	AshEngine::TerrainAssetSnapshot MakeTerrainOverlaySnapshot(float slopeX = 0.0f, float slopeZ = 0.0f)
	{
		AshEngine::TerrainAssetSnapshot snapshot = MakeTerrainModeSnapshot();
		for (uint32_t componentZ = 0u; componentZ < snapshot.layout.component_count_z; ++componentZ)
		{
			for (uint32_t componentX = 0u; componentX < snapshot.layout.component_count_x; ++componentX)
			{
				const AshEngine::TerrainComponentCoord coord{
					static_cast<uint16_t>(componentX),
					static_cast<uint16_t>(componentZ)
				};
				const size_t componentIndex =
					static_cast<size_t>(componentZ) * snapshot.layout.component_count_x + componentX;
				if (componentIndex >= snapshot.components.size() || !snapshot.components[componentIndex])
				{
					throw std::runtime_error("Terrain overlay fixture is missing a component.");
				}

				auto component = std::make_shared<AshEngine::TerrainComponentSnapshot>(
					*snapshot.components[componentIndex]);
				const AshEngine::TerrainSampleRect rect =
					AshEngine::get_terrain_component_snapshot_rect(snapshot.layout, coord);
				for (uint32_t localZ = 0u; localZ < component->sample_height; ++localZ)
				{
					for (uint32_t localX = 0u; localX < component->sample_width; ++localX)
					{
						const float terrainX = static_cast<float>(rect.min_x + localX) *
							snapshot.layout.sample_spacing_meters;
						const float terrainZ = static_cast<float>(rect.min_z + localZ) *
							snapshot.layout.sample_spacing_meters;
						component->heights[
							static_cast<size_t>(localZ) * component->sample_width + localX] =
							slopeX * terrainX + slopeZ * terrainZ;
					}
				}
				snapshot.components[componentIndex] = std::move(component);
			}
		}
		return snapshot;
	}

	glm::mat4 MakeTerrainOverlayTransform(const glm::vec3& translation, const glm::vec3& scale)
	{
		glm::mat4 transform{ 1.0f };
		transform[0][0] = scale.x;
		transform[1][1] = scale.y;
		transform[2][2] = scale.z;
		transform[3] = glm::vec4(translation, 1.0f);
		return transform;
	}

	AshEditor::TerrainEditorPreviewState MakeTerrainOverlayPreview(
		AshEngine::TerrainQueryStatus status,
		const glm::vec3& center,
		float radius)
	{
		AshEditor::TerrainEditorPreviewState preview{};
		preview.query_status = AshEngine::TerrainQueryStatus::Ready;
		preview.viewport.query_status = status;
		preview.viewport.center_ws = center;
		preview.viewport.radius_meters = radius;
		preview.viewport.terrain_entity_id = 77u;
		preview.viewport.has_world_position = true;
		return preview;
	}

	void CheckOverlayVector(const glm::vec3& actual, const glm::vec3& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x));
		CHECK(actual.y == doctest::Approx(expected.y));
		CHECK(actual.z == doctest::Approx(expected.z));
	}

	void CheckOverlayColor(const glm::vec4& actual, const glm::vec4& expected)
	{
		CHECK(actual.r == doctest::Approx(expected.r));
		CHECK(actual.g == doctest::Approx(expected.g));
		CHECK(actual.b == doctest::Approx(expected.b));
		CHECK(actual.a == doctest::Approx(expected.a));
	}

	uint32_t gTerrainOverlaySubmitCalls = 0u;
	AshEngine::SceneViewBindingHandle gTerrainOverlaySubmittedBinding{};
	std::vector<AshEngine::SceneOverlayLine> gTerrainOverlaySubmittedLines{};

	bool CaptureTerrainOverlaySubmission(
		AshEngine::SceneViewBindingHandle binding,
		const AshEngine::SceneOverlayBatchDesc& desc)
	{
		++gTerrainOverlaySubmitCalls;
		gTerrainOverlaySubmittedBinding = binding;
		gTerrainOverlaySubmittedLines.assign(desc.lines, desc.lines + desc.line_count);
		return true;
	}

	void ResetTerrainOverlaySubmissionCapture()
	{
		gTerrainOverlaySubmitCalls = 0u;
		gTerrainOverlaySubmittedBinding = {};
		gTerrainOverlaySubmittedLines.clear();
	}
}

TEST_CASE("Terrain brush preview builds a closed 64 segment world space loop")
{
	const AshEngine::TerrainAssetSnapshot snapshot = MakeTerrainOverlaySnapshot();
	const glm::mat4 transform = MakeTerrainOverlayTransform({ 10.0f, 5.0f, 20.0f }, { 1.0f, 2.0f, 1.0f });
	const AshEditor::TerrainEditorPreviewState preview = MakeTerrainOverlayPreview(
		AshEngine::TerrainQueryStatus::Ready,
		{ 14.0f, 5.0f, 24.0f },
		2.0f);

	const std::vector<AshEngine::SceneOverlayLine> lines =
		AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, transform);
	REQUIRE(lines.size() == 64u);
	for (size_t index = 0u; index < lines.size(); ++index)
	{
		const AshEngine::SceneOverlayLine& line = lines[index];
		const AshEngine::SceneOverlayLine& next = lines[(index + 1u) % lines.size()];
		CheckOverlayVector(line.end, next.start);
		CHECK(line.start.y == doctest::Approx(5.0f));
		CHECK(glm::length(glm::vec2(line.start.x - preview.viewport.center_ws.x,
			line.start.z - preview.viewport.center_ws.z)) == doctest::Approx(2.0f));
		CHECK(line.depth_mode == AshEngine::SceneOverlayDepthMode::DepthTestNoWrite);
	}
	CheckOverlayColor(lines.front().color, { 0.20f, 1.00f, 0.25f, 1.00f });
}

TEST_CASE("Terrain brush preview preserves world XZ radius on sloped nonuniform terrain")
{
	const AshEngine::TerrainAssetSnapshot snapshot = MakeTerrainOverlaySnapshot(0.25f, 0.5f);
	const glm::vec3 translation{ 10.0f, 5.0f, 20.0f };
	const glm::vec3 scale{ 2.0f, 4.0f, 3.0f };
	const glm::mat4 transform = MakeTerrainOverlayTransform(translation, scale);
	const AshEditor::TerrainEditorPreviewState preview = MakeTerrainOverlayPreview(
		AshEngine::TerrainQueryStatus::Ready,
		{ 18.0f, 17.0f, 32.0f },
		3.0f);

	const std::vector<AshEngine::SceneOverlayLine> lines =
		AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, transform);
	REQUIRE(lines.size() == 64u);
	for (const AshEngine::SceneOverlayLine& line : lines)
	{
		const float worldRadius = glm::length(glm::vec2(
			line.start.x - preview.viewport.center_ws.x,
			line.start.z - preview.viewport.center_ws.z));
		CHECK(worldRadius == doctest::Approx(preview.viewport.radius_meters));

		const float terrainX = (line.start.x - translation.x) / scale.x;
		const float terrainZ = (line.start.z - translation.z) / scale.z;
		const float expectedWorldY = translation.y +
			(0.25f * terrainX + 0.5f * terrainZ) * scale.y;
		CHECK(line.start.y == doctest::Approx(expectedWorldY));
	}
}

TEST_CASE("Terrain brush preview leaves gaps for unavailable height samples")
{
	AshEngine::TerrainAssetSnapshot snapshot = MakeTerrainOverlaySnapshot();
	REQUIRE(snapshot.components.size() == 4u);
	snapshot.components[1].reset();
	const AshEditor::TerrainEditorPreviewState preview = MakeTerrainOverlayPreview(
		AshEngine::TerrainQueryStatus::Pending,
		{ 4.0f, 0.0f, 4.0f },
		3.0f);

	const std::vector<AshEngine::SceneOverlayLine> lines =
		AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, glm::mat4(1.0f));
	REQUIRE_FALSE(lines.empty());
	CHECK(lines.size() < 64u);
	const float oneSegmentChord = 2.0f * preview.viewport.radius_meters *
		std::sin(3.14159265358979323846f / 64.0f);
	for (const AshEngine::SceneOverlayLine& line : lines)
	{
		const float chord = glm::length(glm::vec2(
			line.end.x - line.start.x,
			line.end.z - line.start.z));
		CHECK(chord == doctest::Approx(oneSegmentChord).epsilon(0.001));
	}
	CheckOverlayColor(lines.front().color, { 1.00f, 0.65f, 0.10f, 1.00f });
}

TEST_CASE("Terrain brush preview colors failure and rejects unavailable submissions")
{
	const AshEngine::TerrainAssetSnapshot snapshot = MakeTerrainOverlaySnapshot();
	const glm::mat4 validTransform{ 1.0f };
	AshEditor::TerrainEditorPreviewState preview = MakeTerrainOverlayPreview(
		AshEngine::TerrainQueryStatus::Failed,
		{ 4.0f, 0.0f, 4.0f },
		2.0f);

	std::vector<AshEngine::SceneOverlayLine> lines =
		AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, validTransform);
	REQUIRE_FALSE(lines.empty());
	CheckOverlayColor(lines.front().color, { 1.00f, 0.20f, 0.20f, 1.00f });

	preview.viewport.query_status = AshEngine::TerrainQueryStatus::Ready;
	preview.layer_locked = true;
	lines = AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, validTransform);
	REQUIRE_FALSE(lines.empty());
	CheckOverlayColor(lines.front().color, { 1.00f, 0.20f, 0.20f, 1.00f });

	ResetTerrainOverlaySubmissionCapture();
	CHECK_FALSE(AshEditor::TerrainBrushOverlayRenderer::Submit(
		preview,
		std::make_shared<const AshEngine::TerrainAssetSnapshot>(snapshot),
		validTransform,
		{},
		&CaptureTerrainOverlaySubmission));
	CHECK(gTerrainOverlaySubmitCalls == 0u);

	preview.layer_locked = false;
	preview.viewport.query_status = AshEngine::TerrainQueryStatus::Outside;
	CHECK(AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, validTransform).empty());
	ResetTerrainOverlaySubmissionCapture();
	CHECK_FALSE(AshEditor::TerrainBrushOverlayRenderer::Submit(
		preview,
		std::make_shared<const AshEngine::TerrainAssetSnapshot>(snapshot),
		validTransform,
		AshEngine::SceneViewBindingHandle{ 7u },
		&CaptureTerrainOverlaySubmission));
	CHECK(gTerrainOverlaySubmitCalls == 0u);

	preview.viewport.query_status = AshEngine::TerrainQueryStatus::Ready;
	preview.viewport.has_world_position = false;
	CHECK(AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, validTransform).empty());

	preview.viewport.has_world_position = true;
	glm::mat4 invalidTransform{ 1.0f };
	invalidTransform[0][0] = 0.0f;
	CHECK(AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, invalidTransform).empty());
	ResetTerrainOverlaySubmissionCapture();
	CHECK_FALSE(AshEditor::TerrainBrushOverlayRenderer::Submit(
		preview,
		std::make_shared<const AshEngine::TerrainAssetSnapshot>(snapshot),
		invalidTransform,
		AshEngine::SceneViewBindingHandle{ 9u },
		&CaptureTerrainOverlaySubmission));
	CHECK(gTerrainOverlaySubmitCalls == 0u);

	preview.query_status = AshEngine::TerrainQueryStatus::Pending;
	CHECK(AshEditor::TerrainBrushOverlayRenderer::BuildLines(preview, snapshot, validTransform).empty());
	preview.query_status = AshEngine::TerrainQueryStatus::Ready;

	ResetTerrainOverlaySubmissionCapture();
	CHECK(AshEditor::TerrainBrushOverlayRenderer::Submit(
		preview,
		std::make_shared<const AshEngine::TerrainAssetSnapshot>(snapshot),
		validTransform,
		AshEngine::SceneViewBindingHandle{ 11u },
		&CaptureTerrainOverlaySubmission));
	CHECK(gTerrainOverlaySubmitCalls == 1u);
	CHECK(gTerrainOverlaySubmittedBinding.value == 11u);
	CHECK(gTerrainOverlaySubmittedLines.size() == 64u);
}

TEST_CASE("Terrain brush preview uses the ScenePresentation overlay facade")
{
	const std::string overlay = ReadTerrainContractText(
		"project/src/editor/Services/TerrainBrushOverlayRenderer.cpp");
	const std::string canvas = ReadTerrainContractText(
		"project/src/editor/Panels/ViewportPanelCanvas.cpp");

	CHECK(overlay.find("SceneOverlayLine") != std::string::npos);
	CHECK(overlay.find("submit_scene_overlay") != std::string::npos);
	CHECK(overlay.find("SceneOverlayDepthMode::DepthTestNoWrite") != std::string::npos);
	CHECK(overlay.find("draw_window_line") == std::string::npos);
	CHECK(overlay.find("Graphics/") == std::string::npos);
	CHECK(canvas.find("TerrainBrushOverlayRenderer::Submit") != std::string::npos);
	CHECK(canvas.find("IsPrimaryViewport(strViewportId)") != std::string::npos);
	CHECK(canvas.find("refPresentation.bAcceptsInput") != std::string::npos);
	CHECK(canvas.find("GetPublishedSnapshot()") != std::string::npos);
	CHECK(canvas.find("GetSelectedAssetId()") != std::string::npos);
	CHECK(canvas.find("FindByPath(terrain.asset_path)") != std::string::npos);
	CHECK(canvas.find("get_entity_world_transform") != std::string::npos);
	const size_t helpers = canvas.find("UpdateSceneViewportOverlayHelpers(");
	const size_t terrain = canvas.find("SubmitTerrainBrushOverlay(refDeps");
	const size_t selection = canvas.find("DrawSceneBoxSelectionOverlay(");
	REQUIRE(helpers != std::string::npos);
	REQUIRE(terrain != std::string::npos);
	REQUIRE(selection != std::string::npos);
	CHECK(helpers < terrain);
	CHECK(terrain < selection);
}

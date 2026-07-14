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
	CHECK(terrainUi.find("DrawUnavailableFileOperation") == std::string::npos);
}

TEST_CASE("Terrain Mode submits approved create import and export file jobs")
{
	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	const std::string state = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeState.h");

	CHECK(widgets.find("TerrainEditorIntent::Kind::Create") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::Import") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::Export") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::CancelFileOperation") != std::string::npos);
	CHECK(widgets.find("intent.create_desc") != std::string::npos);
	CHECK(widgets.find("intent.import_desc") != std::string::npos);
	CHECK(widgets.find("intent.export_desc") != std::string::npos);
	CHECK(widgets.find("BuildCreateDesc()") != std::string::npos);
	CHECK(widgets.find("BuildImportDesc()") != std::string::npos);
	CHECK(widgets.find("BuildExportDesc(") != std::string::npos);
	CHECK(state.find("TerrainHeightFileFormat::Png") != std::string::npos);
	CHECK(state.find("TerrainHeightFileFormat::RawR16") != std::string::npos);
	CHECK(state.find("TerrainHeightFileFormat::RawR32F") != std::string::npos);
	CHECK(state.find("TerrainHeightFileFormat::Exr") != std::string::npos);
	CHECK(state.find("TerrainExportSource::MaterialWeightLayer") != std::string::npos);
	CHECK(state.find("TerrainExrPixelType::Half") != std::string::npos);
	CHECK(state.find("TerrainExrPixelType::Float") != std::string::npos);
	CHECK(state.find("import_source_width") != std::string::npos);
	CHECK(state.find("import_source_height") != std::string::npos);
	CHECK(state.find("flat_height") != std::string::npos);
	CHECK(state.find("create_height_min") != std::string::npos);
	CHECK(state.find("create_height_max") != std::string::npos);
	CHECK(state.find("import_raw_format_index") != std::string::npos);
	CHECK(state.find("export_raw_format_index") != std::string::npos);
	CHECK(state.find("import_raw_endian_index") != std::string::npos);
	CHECK(state.find("export_raw_endian_index") != std::string::npos);
	CHECK(state.find("std::string import_exr_channel") != std::string::npos);
	CHECK(state.find("std::string export_exr_channel") != std::string::npos);
	CHECK(state.find("int32_t raw_format_index") == std::string::npos);
	CHECK(state.find("int32_t raw_endian_index") == std::string::npos);
	CHECK(state.find("exr_channel_index") == std::string::npos);
	CHECK(state.find("export_format_index") != std::string::npos);
	CHECK(state.find("export_material_layer_index") != std::string::npos);
	CHECK(state.find("terrain/NewTerrain.AshTerrain") != std::string::npos);
}

TEST_CASE("Terrain create descriptor defaults to the production grid layout")
{
	const AshEditor::TerrainCreateAssetDesc desc{};
	const AshEngine::TerrainGridLayout expected =
		AshEngine::make_default_terrain_grid_layout();

	CHECK(AshEngine::is_valid_terrain_grid_layout(desc.layout));
	CHECK(desc.layout.sample_count_x == expected.sample_count_x);
	CHECK(desc.layout.sample_count_z == expected.sample_count_z);
	CHECK(desc.layout.component_count_x == expected.component_count_x);
	CHECK(desc.layout.component_count_z == expected.component_count_z);
	CHECK(desc.layout.component_quad_count == expected.component_quad_count);
	CHECK(desc.layout.sample_spacing_meters ==
		doctest::Approx(expected.sample_spacing_meters));

	const std::string sessionContract = ReadTerrainContractText(
		"project/src/editor/Core/TerrainEditorSessionCore.h");
	CHECK(sessionContract.find(
		"layout = AshEngine::make_default_terrain_grid_layout()") != std::string::npos);
}

TEST_CASE("Terrain Mode state builds an explicit valid create height mapping")
{
	AshEditor::TerrainModeState state{};
	state.create_height_min = -125.0f;
	state.create_height_max = 1875.0f;

	REQUIRE(state.HasValidCreateHeightMapping());
	const AshEngine::TerrainHeightMapping mapping = state.BuildCreateHeightMapping();
	CHECK(mapping.height_offset == doctest::Approx(-125.0f));
	CHECK(mapping.height_range == doctest::Approx(2000.0f));
	state.flat_height = -125.0f;
	CHECK(state.HasValidCreateParameters());
	const AshEditor::TerrainCreateAssetDesc createDesc = state.BuildCreateDesc();
	const AshEngine::TerrainGridLayout defaultLayout =
		AshEngine::make_default_terrain_grid_layout();
	CHECK(createDesc.layout.sample_count_x == defaultLayout.sample_count_x);
	CHECK(createDesc.layout.sample_count_z == defaultLayout.sample_count_z);
	CHECK(createDesc.layout.component_count_x == defaultLayout.component_count_x);
	CHECK(createDesc.layout.component_count_z == defaultLayout.component_count_z);
	CHECK(createDesc.layout.component_quad_count == defaultLayout.component_quad_count);
	CHECK(createDesc.layout.sample_spacing_meters ==
		doctest::Approx(defaultLayout.sample_spacing_meters));
	CHECK(createDesc.height_mapping.height_offset == doctest::Approx(-125.0f));
	CHECK(createDesc.height_mapping.height_range == doctest::Approx(2000.0f));
	CHECK(createDesc.flat_height == doctest::Approx(-125.0f));
	state.flat_height = 1875.0f;
	CHECK(state.HasValidCreateParameters());
	state.flat_height = 1875.1f;
	CHECK_FALSE(state.HasValidCreateParameters());

	state.create_height_max = state.create_height_min;
	CHECK_FALSE(state.HasValidCreateHeightMapping());
	CHECK_FALSE(state.HasValidCreateParameters());
	state.create_height_max = std::numeric_limits<float>::infinity();
	CHECK_FALSE(state.HasValidCreateHeightMapping());
}

TEST_CASE("Terrain Mode state keeps import and export encoding controls independent")
{
	AshEditor::TerrainModeState state{};
	state.import_heightmap_path = "height/input.raw";
	state.import_format_index = 1;
	state.import_raw_format_index = 0;
	state.import_raw_endian_index = 1;
	state.import_raw_axis_index = 1;
	state.import_resize_policy_index = 2;
	state.import_exr_channel = "IMPORT_HEIGHT";
	state.export_heightmap_path = "height/output.raw";
	state.export_format_index = 1;
	state.export_raw_format_index = 1;
	state.export_raw_endian_index = 0;
	state.export_source_index = 2;
	state.export_exr_channel = "EXPORT_HEIGHT";
	state.export_exr_pixel_type_index = 0;

	const AshEngine::TerrainLayerId layerId = MakeTerrainModeLayerId();
	const AshEngine::TerrainHeightImportDesc importDesc = state.BuildImportDesc();
	const AshEngine::TerrainHeightExportDesc exportDesc = state.BuildExportDesc(layerId);
	CHECK(importDesc.source_path == std::filesystem::path("height/input.raw"));
	CHECK(importDesc.format == AshEngine::TerrainHeightFileFormat::RawR16);
	CHECK(importDesc.byte_order == AshEngine::TerrainByteOrder::BigEndian);
	CHECK(importDesc.flip_z);
	CHECK(importDesc.resize_policy == AshEngine::TerrainResizePolicy::CatmullRom);
	CHECK(importDesc.exr_channel == "IMPORT_HEIGHT");
	CHECK(AshEngine::is_valid_terrain_grid_layout(importDesc.target_layout));
	CHECK(exportDesc.destination_path == std::filesystem::path("height/output.raw"));
	CHECK(exportDesc.format == AshEngine::TerrainHeightFileFormat::RawR32F);
	CHECK(exportDesc.byte_order == AshEngine::TerrainByteOrder::LittleEndian);
	CHECK(exportDesc.source == AshEngine::TerrainExportSource::HeightEditLayer);
	CHECK(exportDesc.source_layer_id == layerId);
	CHECK(exportDesc.exr_channel == "EXPORT_HEIGHT");
	CHECK(exportDesc.exr_pixel_type == AshEngine::TerrainExrPixelType::Half);

	state.import_format_index = 2;
	state.export_format_index = 2;
	CHECK(state.BuildImportDesc().format == AshEngine::TerrainHeightFileFormat::Exr);
	CHECK(state.BuildImportDesc().exr_channel == "IMPORT_HEIGHT");
	CHECK(state.BuildExportDesc(layerId).format == AshEngine::TerrainHeightFileFormat::Exr);
	CHECK(state.BuildExportDesc(layerId).exr_channel == "EXPORT_HEIGHT");
}

TEST_CASE("Terrain Mode keeps create and import target asset drafts independent")
{
	AshEditor::TerrainModeState state{};
	state.create_asset_path = "terrain/CreateOnly.AshTerrain";
	state.import_asset_path = "terrain/ImportOnly.AshTerrain";

	CHECK(state.create_asset_path == "terrain/CreateOnly.AshTerrain");
	CHECK(state.import_asset_path == "terrain/ImportOnly.AshTerrain");

	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	CHECK(widgets.find(
		"refUi.input_text(\"Create asset path\", refState.create_asset_path)") !=
		std::string::npos);
	CHECK(widgets.find(
		"refUi.input_text(\"Import asset path\", refState.import_asset_path)") !=
		std::string::npos);

	const size_t importIntent = widgets.find("TerrainEditorIntent::Kind::Import");
	REQUIRE(importIntent != std::string::npos);
	const std::string importBlock = widgets.substr(importIntent, 320u);
	CHECK(importBlock.find("intent.asset_path = refState.import_asset_path") !=
		std::string::npos);
	CHECK(importBlock.find("intent.asset_path = refState.create_asset_path") ==
		std::string::npos);
}

TEST_CASE("Terrain Mode material export supports normalized PNG and RAW R16 encodings")
{
	AshEditor::TerrainModeState state{};
	state.export_source_index = 3;
	state.export_material_layer_index = AshEngine::k_terrain_material_layer_count - 1u;
	state.export_format_index = 0;
	const AshEngine::TerrainLayerId layerId = MakeTerrainModeLayerId();
	CHECK(state.BuildExportDesc(layerId).source == AshEngine::TerrainExportSource::MaterialWeightLayer);
	CHECK(state.BuildExportDesc(layerId).source_layer_id == layerId);
	CHECK(state.BuildExportDesc(layerId).format == AshEngine::TerrainHeightFileFormat::Png);

	state.export_format_index = 1;
	state.export_raw_format_index = 0;
	CHECK(state.BuildExportDesc(layerId).format == AshEngine::TerrainHeightFileFormat::RawR16);
}

TEST_CASE("Terrain Mode only offers cancellation for running import or export jobs")
{
	for (const AshEditor::TerrainFileOperationStatus status : {
			AshEditor::TerrainFileOperationStatus::Idle,
			AshEditor::TerrainFileOperationStatus::AwaitingPublication,
			AshEditor::TerrainFileOperationStatus::Running,
			AshEditor::TerrainFileOperationStatus::PublishedAwaitingCatalog,
			AshEditor::TerrainFileOperationStatus::Succeeded,
			AshEditor::TerrainFileOperationStatus::Failed,
			AshEditor::TerrainFileOperationStatus::Cancelled })
	{
		for (const AshEditor::TerrainFileOperationKind kind : {
				AshEditor::TerrainFileOperationKind::None,
				AshEditor::TerrainFileOperationKind::Save,
				AshEditor::TerrainFileOperationKind::SaveAs,
				AshEditor::TerrainFileOperationKind::Optimize,
				AshEditor::TerrainFileOperationKind::Create,
				AshEditor::TerrainFileOperationKind::Import,
				AshEditor::TerrainFileOperationKind::Export })
		{
			const bool expected = status == AshEditor::TerrainFileOperationStatus::Running &&
				(kind == AshEditor::TerrainFileOperationKind::Import ||
				 kind == AshEditor::TerrainFileOperationKind::Export);
			CHECK(AshEditor::TerrainModeState::ShouldShowCancelFileOperation(status, kind) == expected);
		}
	}

	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");

	const size_t cancelPredicate = widgets.find("const bool canCancelFileOperation");
	const size_t cancelGuard = widgets.find("if (canCancelFileOperation)", cancelPredicate);
	const size_t cancelButton = widgets.find("refUi.button(\"Cancel File Operation\")", cancelGuard);
	const size_t replacePredicate = widgets.find("const bool canReplaceSession", cancelButton);
	REQUIRE(cancelPredicate != std::string::npos);
	REQUIRE(cancelGuard != std::string::npos);
	REQUIRE(cancelButton != std::string::npos);
	REQUIRE(replacePredicate != std::string::npos);
	const std::string cancelBlock = widgets.substr(cancelGuard, replacePredicate - cancelGuard);
	CHECK(cancelBlock.find("begin_disabled") == std::string::npos);
	const std::string cancelPredicateBlock = widgets.substr(
		cancelPredicate, cancelGuard - cancelPredicate);
	CHECK(cancelPredicateBlock.find("ShouldShowCancelFileOperation(") != std::string::npos);

	const size_t replaceEnd = widgets.find("refUi.begin_disabled(fileOperationInProgress)", replacePredicate);
	REQUIRE(replaceEnd != std::string::npos);
	CHECK(widgets.substr(replacePredicate, replaceEnd - replacePredicate)
		.find("!refView.blocking_operation") != std::string::npos);
}

TEST_CASE("Terrain Mode explains durable publication while catalog binding retries")
{
	AshEditor::TerrainFileOperationState operation{};
	operation.kind = AshEditor::TerrainFileOperationKind::Import;
	operation.status = AshEditor::TerrainFileOperationStatus::PublishedAwaitingCatalog;
	operation.path = "terrain/ImportedTerrain.AshTerrain";
	operation.error = "Catalog refresh did not expose the published Terrain.";

	const std::string message =
		AshEditor::TerrainModeState::BuildPublishedAwaitingCatalogMessage(operation);
	CHECK(message.find("terrain/ImportedTerrain.AshTerrain") != std::string::npos);
	CHECK(message.find("durably published") != std::string::npos);
	CHECK(message.find("retrying") != std::string::npos);
	CHECK(message.find(operation.error) != std::string::npos);

	operation.error.clear();
	const std::string retryMessage =
		AshEditor::TerrainModeState::BuildPublishedAwaitingCatalogMessage(operation);
	CHECK(retryMessage.find("terrain/ImportedTerrain.AshTerrain") != std::string::npos);
	CHECK(retryMessage.find("retrying") != std::string::npos);

	operation.status = AshEditor::TerrainFileOperationStatus::Succeeded;
	CHECK(AshEditor::TerrainModeState::BuildPublishedAwaitingCatalogMessage(operation).empty());

	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	CHECK(widgets.find("BuildPublishedAwaitingCatalogMessage(refState)") !=
		std::string::npos);
}

TEST_CASE("Terrain Mode export is explicit about non-overwrite behavior and accepts normalized material formats")
{
	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	CHECK(widgets.find("never overwrites an existing destination") != std::string::npos);
	CHECK(widgets.find("Import EXR channel") != std::string::npos);
	CHECK(widgets.find("Export EXR channel") != std::string::npos);

	const size_t validation = widgets.find("const bool validMaterialExport");
	const size_t channelValidation = widgets.find("const bool validExportChannel", validation);
	const size_t canExport = widgets.find("const bool canExport", channelValidation);
	REQUIRE(validation != std::string::npos);
	REQUIRE(channelValidation != std::string::npos);
	REQUIRE(canExport != std::string::npos);
	const std::string validationBlock = widgets.substr(
		validation, channelValidation - validation);
	CHECK(validationBlock.find("k_terrain_material_layer_count") != std::string::npos);
	CHECK(validationBlock.find("TerrainHeightFileFormat::RawR32F") == std::string::npos);
	CHECK(validationBlock.find("TerrainHeightFileFormat::Exr") == std::string::npos);
	CHECK(widgets.find("Material weights require RAW R32F or EXR") == std::string::npos);
	const size_t layerSelection = widgets.find("const bool hasSelectedExportLayer");
	REQUIRE(layerSelection != std::string::npos);
	const std::string layerSelectionBlock = widgets.substr(
		layerSelection, validation - layerSelection);
	CHECK(layerSelectionBlock.find("TerrainExportSource::HeightEditLayer") != std::string::npos);
	CHECK(layerSelectionBlock.find("TerrainExportSource::MaterialWeightLayer") != std::string::npos);
	CHECK(layerSelectionBlock.find("brush.layer_id.is_valid()") != std::string::npos);
}

TEST_CASE("Terrain Mode wires generation-aware save optimize and reload operations")
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
	CHECK(widgets.find("TerrainEditorIntent::Kind::Reload") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::Optimize") != std::string::npos);
	CHECK(widgets.find("intent.asset_path = refState.save_as_asset_path") != std::string::npos);
	CHECK(widgets.find("const bool canOptimize") != std::string::npos);
	CHECK(widgets.find("!refView.dirty") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::AwaitingPublication") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::Running") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::PublishedAwaitingCatalog") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::Cancelled") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::Succeeded") != std::string::npos);
	CHECK(widgets.find("TerrainFileOperationStatus::Failed") != std::string::npos);
	CHECK(widgets.find("refState.warnings") != std::string::npos);
	CHECK(widgets.find("refUi.button(\"Reload\")") != std::string::npos);
}

TEST_CASE("Terrain Mode presents external conflicts through a latched UIContext modal")
{
	const std::string panel = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModePanel.cpp");
	const std::string state = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeState.h");

	CHECK(state.find("last_external_change_serial") != std::string::npos);
	CHECK(state.find("conflict_save_as_pending") != std::string::npos);
	CHECK(panel.find("GetExternalChangeState()") != std::string::npos);
	CHECK(panel.find("kTerrainExternalChangePopupId") != std::string::npos);
	CHECK(panel.find("refUi.open_popup(kTerrainExternalChangePopupId)") != std::string::npos);
	CHECK(panel.find("refUi.begin_popup_modal(") != std::string::npos);
	CHECK(panel.find("Local content generation") != std::string::npos);
	CHECK(panel.find("Disk content generation") != std::string::npos);
	CHECK(panel.find("TerrainEditorIntent::Kind::Reload") != std::string::npos);
	CHECK(panel.find("TerrainEditorIntent::Kind::KeepLocal") != std::string::npos);
	CHECK(panel.find("TerrainEditorIntent::Kind::SaveAs") != std::string::npos);

	const size_t modal = panel.find("DrawTerrainExternalChangeModal");
	const size_t reloadAction = panel.find("Reload / Discard Local", modal);
	const size_t keepLocalAction = panel.find("Keep Local", reloadAction);
	const size_t saveAsAction = panel.find("Save Local Copy As", keepLocalAction);
	const size_t modalEnd = panel.find("refUi.end_popup()", saveAsAction);
	REQUIRE(modal != std::string::npos);
	REQUIRE(reloadAction != std::string::npos);
	REQUIRE(keepLocalAction != std::string::npos);
	REQUIRE(saveAsAction != std::string::npos);
	REQUIRE(modalEnd != std::string::npos);

	const std::string reloadBlock = panel.substr(reloadAction, keepLocalAction - reloadAction);
	const std::string keepLocalBlock = panel.substr(keepLocalAction, saveAsAction - keepLocalAction);
	const std::string saveAsBlock = panel.substr(saveAsAction, modalEnd - saveAsAction);
	const size_t reloadSubmit = reloadBlock.find("SubmitIntent(intent)");
	const size_t reloadClose = reloadBlock.find("close_current_popup()");
	const size_t keepLocalSubmit = keepLocalBlock.find("SubmitIntent(intent)");
	const size_t keepLocalClose = keepLocalBlock.find("close_current_popup()");
	REQUIRE(reloadSubmit != std::string::npos);
	REQUIRE(reloadClose != std::string::npos);
	REQUIRE(keepLocalSubmit != std::string::npos);
	REQUIRE(keepLocalClose != std::string::npos);
	CHECK(reloadSubmit < reloadClose);
	CHECK(keepLocalSubmit < keepLocalClose);
	CHECK(saveAsBlock.find("SubmitIntent(intent)") != std::string::npos);
	CHECK(saveAsBlock.find("close_current_popup()") == std::string::npos);
	CHECK(saveAsBlock.find("refState.conflict_save_as_pending = true") != std::string::npos);

	const size_t completionGuard = panel.find(
		"refState.conflict_save_as_pending && external.status != TerrainExternalChangeStatus::Conflict",
		modal);
	REQUIRE(completionGuard != std::string::npos);
	const size_t completionReturn = panel.find("return;", completionGuard);
	REQUIRE(completionReturn != std::string::npos);
	const std::string completionBlock = panel.substr(
		completionGuard, completionReturn - completionGuard);
	CHECK(completionBlock.find("begin_popup_modal") != std::string::npos);
	CHECK(completionBlock.find("close_current_popup()") != std::string::npos);
	CHECK(completionBlock.find("end_popup()") != std::string::npos);
	CHECK(completionBlock.find("refState.conflict_save_as_pending = false") != std::string::npos);
	CHECK(panel.find("TerrainFileOperationStatus::Failed") != std::string::npos);
	CHECK(panel.find("const bool fileOperationInProgress") != std::string::npos);
	CHECK(panel.find("ImGui::") == std::string::npos);
}

TEST_CASE("Terrain Mode keeps recovered and failed assets read only with recovery actions")
{
	const std::string panel = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModePanel.cpp");
	const std::string widgets = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.cpp");
	const std::string widgetHeader = ReadTerrainContractText(
		"project/src/editor/Panels/Terrain/TerrainModeWidgets.h");

	CHECK(panel.find("GetExternalChangeState()") != std::string::npos);
	CHECK(widgetHeader.find("p_external_change_state") != std::string::npos);
	CHECK(widgets.find("IsTerrainReadOnly") != std::string::npos);
	CHECK(widgets.find("const bool readOnly = IsTerrainReadOnly(refView);") != std::string::npos);
	CHECK(widgets.find("TerrainEditorIntent::Kind::Repair") != std::string::npos);
	CHECK(widgets.find("Recovered Terrain source is read only") != std::string::npos);
	CHECK(widgets.find("Failed Terrain source is read only") != std::string::npos);
	CHECK(widgets.find("canSave") != std::string::npos);
	CHECK(widgets.find("canOptimize") != std::string::npos);
	CHECK(widgets.find("canEditLayers") != std::string::npos);
	CHECK(widgets.find("!readOnly") != std::string::npos);
	CHECK(widgets.find("refUi.begin_disabled(refView.preview.stroke_active || readOnly);") !=
		std::string::npos);
}

TEST_CASE("Terrain Scene workflow preflights and commits authoring state in lifecycle order")
{
	const std::string header = ReadTerrainContractText(
		"project/src/editor/App/SceneWorkflowCoordinator.h");
	const std::string workflow = ReadTerrainContractText(
		"project/src/editor/App/SceneWorkflowCoordinator.cpp");
	const std::string actions = ReadTerrainContractText(
		"project/src/editor/App/EditorActionCoordinator.cpp");

	CHECK(header.find("TerrainEditorService* pTerrainEditorService") != std::string::npos);
	CHECK(workflow.find("PrepareTerrainForSceneChange") != std::string::npos);
	CHECK(workflow.find("CommitTerrainSceneChange") != std::string::npos);

	const size_t reset = workflow.find("SceneWorkflowCoordinator::ResetEditorStateAfterSceneChange");
	const size_t commit = workflow.find("CommitTerrainSceneChange(context)", reset);
	const size_t clearSelection = workflow.find("context.refSelectionService.Clear()", reset);
	const size_t clearHistory = workflow.find("context.refUndoRedoService.Clear()", clearSelection);
	REQUIRE(reset != std::string::npos);
	REQUIRE(commit != std::string::npos);
	REQUIRE(clearSelection != std::string::npos);
	REQUIRE(clearHistory != std::string::npos);
	CHECK(commit < clearSelection);
	CHECK(clearSelection < clearHistory);

	const size_t activate = workflow.find("SceneWorkflowCoordinator::ActivateNewScene(");
	const size_t prepareNew = workflow.find("PrepareTerrainForSceneChange(context)", activate);
	const size_t activatePrepared = workflow.find("ActivateNewScenePrepared(context", prepareNew);
	REQUIRE(activate != std::string::npos);
	REQUIRE(prepareNew != std::string::npos);
	REQUIRE(activatePrepared != std::string::npos);
	CHECK(prepareNew < activatePrepared);

	const size_t preparedNew = workflow.find("SceneWorkflowCoordinator::ActivateNewScenePrepared");
	const size_t mutateNew = workflow.find("context.refSceneService.NewScene", preparedNew);
	const size_t resetAfterNew = workflow.find("ResetEditorStateAfterSceneChange(context)", mutateNew);
	REQUIRE(preparedNew != std::string::npos);
	REQUIRE(mutateNew != std::string::npos);
	REQUIRE(resetAfterNew != std::string::npos);
	CHECK(mutateNew < resetAfterNew);

	const size_t publicLoad = workflow.find("SceneWorkflowCoordinator::LoadSceneIntoEditor(");
	const size_t prepareLoad = workflow.find("PrepareTerrainForSceneChange(context)", publicLoad);
	const size_t callPreparedLoad = workflow.find("LoadSceneIntoEditorPrepared(", prepareLoad);
	const size_t preparedLoad = workflow.find("SceneWorkflowCoordinator::LoadSceneIntoEditorPrepared");
	const size_t mutateLoad = workflow.find("context.refSceneService.LoadScene(pathScene)", preparedLoad);
	const size_t resetAfterLoad = workflow.find("ResetEditorStateAfterSceneChange(context)", mutateLoad);
	REQUIRE(publicLoad != std::string::npos);
	REQUIRE(prepareLoad != std::string::npos);
	REQUIRE(callPreparedLoad != std::string::npos);
	CHECK(prepareLoad < callPreparedLoad);
	REQUIRE(preparedLoad != std::string::npos);
	REQUIRE(mutateLoad != std::string::npos);
	REQUIRE(resetAfterLoad != std::string::npos);
	CHECK(mutateLoad < resetAfterLoad);

	const size_t reload = workflow.find("SceneWorkflowCoordinator::ReloadActiveScene");
	const size_t prepareReload = workflow.find("PrepareTerrainForSceneChange(context)", reload);
	const size_t reloadLoad = workflow.find("LoadSceneIntoEditorPrepared", prepareReload);
	const size_t reloadFailed = workflow.find("return SceneReloadResult::Failed", reloadLoad);
	const size_t reloadEnd = workflow.find("SceneWorkflowCoordinator::UpdateLastScenePathSetting", reloadLoad);
	REQUIRE(reload != std::string::npos);
	REQUIRE(prepareReload != std::string::npos);
	REQUIRE(reloadLoad != std::string::npos);
	REQUIRE(reloadFailed != std::string::npos);
	REQUIRE(reloadEnd != std::string::npos);
	CHECK(prepareReload < reloadLoad);
	CHECK(reloadLoad < reloadFailed);
	CHECK(workflow.substr(reloadLoad, reloadEnd - reloadLoad).find(
		"ActivateNewScenePrepared") == std::string::npos);

	for (const char* functionName : {
		"EditorActionCoordinator::HandleNewScene()",
		"EditorActionCoordinator::OpenSceneFromPath",
		"EditorActionCoordinator::HandleReloadScene()" })
	{
		const size_t function = actions.find(functionName);
		const size_t pendingGuard = actions.find("_optPendingSceneSave", function);
		const size_t cancelPending = actions.find("CancelPendingSceneSave", function);
		REQUIRE(function != std::string::npos);
		REQUIRE(pendingGuard != std::string::npos);
		REQUIRE(cancelPending != std::string::npos);
		CHECK_MESSAGE(pendingGuard < cancelPending, functionName);
	}
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
	const std::string container = ReadTerrainContractText(
		"project/src/engine/Function/Asset/TerrainContainer.cpp");
	const size_t helper = service.find("SaveTerrainCopyNew(");
	const size_t helperEnd = service.find("bool IsTerrainFileOperationInProgress", helper);
	const size_t temporaryGuard = service.find("TerrainCopyTemporaryGuard", helper);
	const size_t stagedWrite = service.find(
		"save_terrain_container_incremental(temporary", helper);
	const size_t publishCall = service.find(
		"publish_staged_terrain_container_new(", stagedWrite);
	const size_t publishHelper = container.find(
		"TerrainContainerResult publish_staged_terrain_container_new(");
	const size_t destinationLease = container.find(
		"destination_lease.acquire", publishHelper);
	const size_t destinationCheck = container.find(
		"std::filesystem::exists(canonical_destination", destinationLease);
	const size_t atomicPublish = container.find("MoveFileExW(", destinationCheck);
	const size_t writeThrough = container.find("MOVEFILE_WRITE_THROUGH", atomicPublish);

	REQUIRE(helper != std::string::npos);
	REQUIRE(helperEnd != std::string::npos);
	REQUIRE(temporaryGuard != std::string::npos);
	REQUIRE(stagedWrite != std::string::npos);
	REQUIRE(publishCall != std::string::npos);
	REQUIRE(publishHelper != std::string::npos);
	REQUIRE(destinationLease != std::string::npos);
	REQUIRE(destinationCheck != std::string::npos);
	REQUIRE(atomicPublish != std::string::npos);
	REQUIRE(writeThrough != std::string::npos);
	CHECK(helper < temporaryGuard);
	CHECK(temporaryGuard < stagedWrite);
	CHECK(stagedWrite < publishCall);
	CHECK(publishHelper < destinationLease);
	CHECK(destinationLease < destinationCheck);
	CHECK(destinationCheck < atomicPublish);
	CHECK(atomicPublish < writeThrough);
	CHECK(container.substr(publishHelper, writeThrough - publishHelper).find(
		"MOVEFILE_REPLACE_EXISTING") ==
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

#include "Core/TerrainEditorSessionCore.h"
#include "Function/Asset/TerrainComposition.h"
#include "Terrain/TerrainTestUtils.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	std::string ReadTerrainEditorText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
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

#include "Core/EditorContext.h"
#include "Core/TerrainCommands.h"
#include "Function/Asset/TerrainBrush.h"
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
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	AshEngine::TerrainLayerId MakeTerrainCommandLayerId()
	{
		AshEngine::TerrainLayerId id{};
		id.bytes[0] = 31u;
		return id;
	}

	AshEngine::TerrainAssetSnapshot MakeTerrainCommandSnapshot()
	{
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		if (!AshEngine::create_flat_terrain_snapshot(
				73u,
				TerrainTests::MakeSmallLayout(),
				{ 0.0f, 1024.0f },
				0.0f,
				snapshot))
		{
			throw std::runtime_error("could not create Terrain command snapshot");
		}

		AshEngine::TerrainAssetSnapshot result = *snapshot;
		AshEngine::TerrainEditLayer layer{};
		layer.id = MakeTerrainCommandLayerId();
		layer.name = "Sculpt";
		result.edit_layers = std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(
			std::vector<AshEngine::TerrainEditLayer>{ std::move(layer) });
		return result;
	}

	AshEngine::TerrainWorkingSet MakeTerrainCommandWorkingSet(
		const AshEngine::TerrainAssetSnapshot& refSnapshot)
	{
		AshEngine::TerrainWorkingSet workingSet{};
		if (!AshEngine::make_terrain_working_set(refSnapshot, workingSet))
		{
			throw std::runtime_error("could not create Terrain command working set");
		}
		return workingSet;
	}

	std::vector<AshEngine::TerrainEditPatch> MakeRaisePatches(
		AshEngine::TerrainWorkingSet& refWorkingSet)
	{
		AshEngine::TerrainBrushParameters brush{};
		brush.tool = AshEngine::TerrainBrushTool::Raise;
		brush.radius_meters = 1.0f;
		brush.strength = 1.0f;
		brush.falloff = 1.0f;
		brush.stroke_spacing_meters = 1.0f;
		brush.layer_id = MakeTerrainCommandLayerId();

		std::vector<AshEngine::TerrainEditPatch> patches{};
		std::vector<AshEngine::TerrainComponentCoord> dirty{};
		if (!AshEngine::apply_terrain_brush_stroke(
				refWorkingSet,
				brush,
				{},
				{ { { 2.0f, 2.0f }, 1.0f } },
				patches,
				dirty))
		{
			throw std::runtime_error("could not create Terrain command patches");
		}
		return patches;
	}

	std::string ReadTerrainCommandText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}
}

TEST_CASE("Terrain stroke command replays the Engine patch API by stable identity")
{
	const AshEngine::TerrainAssetSnapshot snapshot = MakeTerrainCommandSnapshot();
	AshEngine::TerrainWorkingSet pristine = MakeTerrainCommandWorkingSet(snapshot);
	AshEngine::TerrainWorkingSet edited = pristine;
	const std::vector<AshEngine::TerrainEditPatch> patches = MakeRaisePatches(edited);
	REQUIRE_FALSE(patches.empty());
	REQUIRE_FALSE(edited.edit_layers.front().height_blocks.empty());
	const std::vector<float> expectedAfter = edited.edit_layers.front().height_blocks.front().values;

	AshEditor::TerrainEditorService service{};
	REQUIRE(service.OpenSnapshotForAuthoring(snapshot));
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	AshEditor::TerrainStrokeCommand command(
		73u,
		MakeTerrainCommandLayerId(),
		3u,
		patches);

	CHECK(command.Execute(context));
	const AshEngine::TerrainWorkingSet* applied = service.GetWorkingSet();
	REQUIRE(applied != nullptr);
	REQUIRE_FALSE(applied->edit_layers.front().height_blocks.empty());
	CHECK(applied->edit_layers.front().height_blocks.front().values == expectedAfter);
	const uint64_t afterExecuteGeneration = applied->content_generation;

	CHECK(command.Undo(context));
	const AshEngine::TerrainWorkingSet* undone = service.GetWorkingSet();
	REQUIRE(undone != nullptr);
	CHECK(undone->edit_layers.front().height_blocks.empty());
	CHECK(undone->content_generation > afterExecuteGeneration);
	const uint64_t afterUndoGeneration = undone->content_generation;

	CHECK(command.Execute(context));
	const AshEngine::TerrainWorkingSet* redone = service.GetWorkingSet();
	REQUIRE(redone != nullptr);
	REQUIRE_FALSE(redone->edit_layers.front().height_blocks.empty());
	CHECK(redone->edit_layers.front().height_blocks.front().values == expectedAfter);
	CHECK(redone->content_generation > afterUndoGeneration);
}

TEST_CASE("Terrain stroke command rejects a mismatched session without mutation")
{
	const AshEngine::TerrainAssetSnapshot snapshot = MakeTerrainCommandSnapshot();
	AshEngine::TerrainWorkingSet pristine = MakeTerrainCommandWorkingSet(snapshot);
	AshEngine::TerrainWorkingSet edited = pristine;
	const std::vector<AshEngine::TerrainEditPatch> patches = MakeRaisePatches(edited);

	AshEditor::TerrainEditorService service{};
	AshEngine::TerrainAssetSnapshot otherSnapshot = snapshot;
	otherSnapshot.asset_id = 74u;
	REQUIRE(service.OpenSnapshotForAuthoring(otherSnapshot));
	AshEditor::EditorContext context{};
	context.pTerrainEditorService = &service;
	AshEditor::TerrainStrokeCommand command(
		73u,
		MakeTerrainCommandLayerId(),
		3u,
		patches);

	const uint64_t beforeGeneration = service.GetWorkingSet()->content_generation;
	CHECK_FALSE(command.Execute(context));
	CHECK(service.GetWorkingSet()->content_generation == beforeGeneration);
	CHECK(service.GetWorkingSet()->edit_layers.front().height_blocks.empty());
}

TEST_CASE("Terrain already-executed history path never re-executes a command")
{
	const std::string executor = ReadTerrainCommandText(
		"project/src/editor/Core/IEditorCommandExecutor.h");
	const std::string undoRedo = ReadTerrainCommandText(
		"project/src/editor/Services/UndoRedoService.cpp");
	const std::string command = ReadTerrainCommandText(
		"project/src/editor/Core/TerrainCommands.cpp");
	const size_t recordBegin = undoRedo.find("UndoRedoService::RecordExecuted");
	const size_t recordEnd = undoRedo.find("UndoRedoService::Undo", recordBegin);

	CHECK(executor.find("RecordExecutedCommand") != std::string::npos);
	CHECK(executor.find("EditorCommandRecordResult") != std::string::npos);
	REQUIRE(recordBegin != std::string::npos);
	REQUIRE(recordEnd != std::string::npos);
	const std::string recordBody = undoRedo.substr(recordBegin, recordEnd - recordBegin);
	CHECK(recordBody.find("->Execute(") == std::string::npos);
	CHECK(recordBody.find("RollbackFailed") != std::string::npos);
	CHECK(recordBody.find("_upPendingTransaction") != std::string::npos);
	CHECK(recordBody.find("vecCommands.push_back") == std::string::npos);
	CHECK(recordBody.find(".reserve(") < recordBody.find(".push_back("));
	CHECK(command.find("TerrainEditPatchDirection::Undo") != std::string::npos);
	CHECK(command.find("TerrainEditPatchDirection::Redo") != std::string::npos);
	CHECK(command.find("decode_terrain_rle") == std::string::npos);
}

#include "Core/TerrainCommands.h"

#include "Core/EditorContext.h"
#include "Services/TerrainEditorService.h"

#include <utility>

namespace AshEditor
{
	TerrainStrokeCommand::TerrainStrokeCommand(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerId layerId,
		const uint64_t sequence,
		std::vector<AshEngine::TerrainEditPatch> patches)
		: _assetId(assetId)
		, _layerId(layerId)
		, _sequence(sequence)
		, _patches(std::move(patches))
	{
	}

	const char* TerrainStrokeCommand::GetLabel() const
	{
		return "Terrain Stroke";
	}

	bool TerrainStrokeCommand::Execute(EditorContext& refContext)
	{
		return Replay(refContext, AshEngine::TerrainEditPatchDirection::Redo);
	}

	bool TerrainStrokeCommand::Undo(EditorContext& refContext)
	{
		return Replay(refContext, AshEngine::TerrainEditPatchDirection::Undo);
	}

	bool TerrainStrokeCommand::Replay(
		EditorContext& refContext,
		const AshEngine::TerrainEditPatchDirection eDirection)
	{
		return _assetId != 0u && _layerId.is_valid() && _sequence != 0u && !_patches.empty() &&
			refContext.pTerrainEditorService &&
			refContext.pTerrainEditorService->ApplyStrokePatches(
				_assetId,
				_layerId,
				_patches,
				eDirection,
				_sequence);
	}
}

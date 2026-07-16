#include "Core/TerrainCommands.h"

#include "Core/EditorContext.h"
#include "Services/TerrainEditorService.h"

#include <algorithm>
#include <utility>

namespace AshEditor
{
	TerrainStrokeCommand::TerrainStrokeCommand(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerId layerId,
		const uint64_t sequence,
		std::vector<AshEngine::TerrainEditPatch> patches,
		std::optional<AshEngine::TerrainLayerStackPatch> autoLayerPatch,
		const AshEngine::TerrainLayerId selectedBefore,
		const AshEngine::TerrainLayerId selectedAfter)
		: _assetId(assetId)
		, _layerId(layerId)
		, _sequence(sequence)
		, _patches(std::move(patches))
		, _autoLayerPatch(std::move(autoLayerPatch))
		, _selectedBefore(selectedBefore)
		, _selectedAfter(selectedAfter)
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

	AshEngine::TerrainAssetId TerrainStrokeCommand::GetAffectedTerrainAssetId() const noexcept
	{
		return _assetId;
	}

	bool TerrainStrokeCommand::Replay(
		EditorContext& refContext,
		const AshEngine::TerrainEditPatchDirection eDirection)
	{
		if (_autoLayerPatch)
		{
			const auto isValidSelection = [](
				const AshEngine::TerrainLayerId selectedLayerId,
				const std::vector<AshEngine::TerrainLayerId>& layerOrder)
			{
				return layerOrder.empty()
					? !selectedLayerId.is_valid()
					: selectedLayerId.is_valid() &&
						std::find(layerOrder.begin(), layerOrder.end(), selectedLayerId) !=
							layerOrder.end();
			};
			if (_autoLayerPatch->asset_id != _assetId || !_autoLayerPatch->has_change() ||
				_autoLayerPatch->layer_id != _layerId ||
				!isValidSelection(_selectedBefore, _autoLayerPatch->before_order) ||
				!isValidSelection(_selectedAfter, _autoLayerPatch->after_order))
			{
				return false;
			}
		}
		return _assetId != 0u && _layerId.is_valid() && _sequence != 0u && !_patches.empty() &&
			refContext.pTerrainEditorService &&
			refContext.pTerrainEditorService->ApplyStrokeTransactionPatches(
				_assetId,
				_layerId,
				_patches,
				_autoLayerPatch,
				_selectedBefore,
				_selectedAfter,
				eDirection,
				_sequence);
	}

	TerrainLayerCommand::TerrainLayerCommand(
		const AshEngine::TerrainAssetId assetId,
		const uint64_t sequence,
		AshEngine::TerrainLayerStackPatch patch,
		const AshEngine::TerrainLayerId selectedBefore,
		const AshEngine::TerrainLayerId selectedAfter)
		: _assetId(assetId)
		, _sequence(sequence)
		, _patch(std::move(patch))
		, _selectedBefore(selectedBefore)
		, _selectedAfter(selectedAfter)
	{
	}

	const char* TerrainLayerCommand::GetLabel() const
	{
		return "Terrain Layer";
	}

	bool TerrainLayerCommand::Execute(EditorContext& refContext)
	{
		return Replay(refContext, AshEngine::TerrainEditPatchDirection::Redo);
	}

	bool TerrainLayerCommand::Undo(EditorContext& refContext)
	{
		return Replay(refContext, AshEngine::TerrainEditPatchDirection::Undo);
	}

	AshEngine::TerrainAssetId TerrainLayerCommand::GetAffectedTerrainAssetId() const noexcept
	{
		return _assetId;
	}

	bool TerrainLayerCommand::Replay(
		EditorContext& refContext,
		const AshEngine::TerrainEditPatchDirection eDirection)
	{
		const auto isValidSelection = [](
			const AshEngine::TerrainLayerId selectedLayerId,
			const std::vector<AshEngine::TerrainLayerId>& layerOrder)
		{
			return layerOrder.empty()
				? !selectedLayerId.is_valid()
				: selectedLayerId.is_valid() &&
					std::find(layerOrder.begin(), layerOrder.end(), selectedLayerId) != layerOrder.end();
		};

		if (!isValidSelection(_selectedBefore, _patch.before_order) ||
			!isValidSelection(_selectedAfter, _patch.after_order))
		{
			return false;
		}

		const AshEngine::TerrainLayerId selectedLayerId =
			eDirection == AshEngine::TerrainEditPatchDirection::Undo
			? _selectedBefore : _selectedAfter;
		return _assetId != 0u && _sequence != 0u && _patch.asset_id == _assetId &&
			_patch.has_change() && refContext.pTerrainEditorService &&
			refContext.pTerrainEditorService->ApplyLayerStackPatch(
				_assetId,
				_patch,
				eDirection,
				selectedLayerId,
				_sequence);
	}
}

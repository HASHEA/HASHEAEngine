#include "Core/TerrainEditorSessionCore.h"

#include <utility>

namespace AshEditor
{
	AshEngine::TerrainAssetId TerrainEditorSessionCore::GetAssetId() const
	{
		return _assetId;
	}

	const TerrainEditorPreviewState& TerrainEditorSessionCore::GetPreviewState() const
	{
		return _preview;
	}

	bool TerrainEditorSessionCore::HasActiveStroke() const
	{
		return _activeSequence != 0u;
	}

	bool TerrainEditorSessionCore::Reduce(const TerrainEditorIntent& refIntent)
	{
		if (refIntent.kind != TerrainEditorIntent::Kind::SelectAsset)
		{
			return false;
		}

		if (_assetId != refIntent.asset_id)
		{
			_optWorkingSet.reset();
			_persistedContentGeneration = 0u;
			_activeSequence = 0u;
			_preview = {};
		}
		_assetId = refIntent.asset_id;
		return true;
	}

	bool TerrainEditorSessionCore::Open(AshEngine::TerrainWorkingSet workingSet)
	{
		if (workingSet.asset_id == 0u || !AshEngine::is_valid_terrain_grid_layout(workingSet.layout))
		{
			return false;
		}

		_assetId = workingSet.asset_id;
		_persistedContentGeneration = workingSet.content_generation;
		_optWorkingSet = std::move(workingSet);
		_activeSequence = 0u;
		_preview = {};
		_preview.query_status = AshEngine::TerrainQueryStatus::Ready;
		return true;
	}

	void TerrainEditorSessionCore::Close()
	{
		_assetId = 0u;
		_activeSequence = 0u;
		_persistedContentGeneration = 0u;
		_optWorkingSet.reset();
		_preview = {};
	}

	const AshEngine::TerrainWorkingSet* TerrainEditorSessionCore::GetWorkingSet() const
	{
		return _optWorkingSet ? &*_optWorkingSet : nullptr;
	}

	bool TerrainEditorSessionCore::ApplyStrokePatches(
		const AshEngine::TerrainAssetId assetId,
		const AshEngine::TerrainLayerId layerId,
		const std::vector<AshEngine::TerrainEditPatch>& refPatches,
		const AshEngine::TerrainEditPatchDirection eDirection,
		std::vector<AshEngine::TerrainComponentCoord>& refDirtyComponents,
		std::string* pError)
	{
		if (pError)
		{
			pError->clear();
		}
		if (!_optWorkingSet || assetId == 0u || assetId != _assetId || !layerId.is_valid() ||
			refPatches.empty())
		{
			if (pError)
			{
				*pError = "Terrain stroke command does not match an open authoring session.";
			}
			return false;
		}

		for (const AshEngine::TerrainEditPatch& refPatch : refPatches)
		{
			if (refPatch.asset_id != assetId || refPatch.layer_id != layerId)
			{
				if (pError)
				{
					*pError = "Terrain stroke patch identity does not match its command.";
				}
				return false;
			}
		}

		return AshEngine::apply_terrain_edit_patches(
			*_optWorkingSet,
			refPatches,
			eDirection,
			refDirtyComponents,
			pError);
	}

	bool TerrainEditorSessionCore::IsDirty() const
	{
		return _optWorkingSet && _optWorkingSet->content_generation != _persistedContentGeneration;
	}

	void TerrainEditorSessionCore::SetPreviewQueryStatus(const AshEngine::TerrainQueryStatus eStatus)
	{
		_preview.query_status = eStatus;
	}
}

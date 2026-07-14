#include "Core/TerrainEditorSessionCore.h"

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
}

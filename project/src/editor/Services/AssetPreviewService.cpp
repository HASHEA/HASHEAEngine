#include "Services/AssetPreviewService.h"

#include <utility>

namespace AshEditor
{
	void AssetPreviewService::SetPreviewAsset(EditorSelection selection)
	{
		_previewAsset = std::move(selection);
	}

	const EditorSelection& AssetPreviewService::GetPreviewAsset() const
	{
		return _previewAsset;
	}

	bool AssetPreviewService::HasPreviewAsset() const
	{
		return _previewAsset.eKind == EditorSelectionKind::Asset && _previewAsset.uId != 0;
	}

	void AssetPreviewService::Clear()
	{
		_previewAsset.Clear();
	}
}

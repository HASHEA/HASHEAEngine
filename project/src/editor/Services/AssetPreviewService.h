#pragma once

#include "Core/EditorSelection.h"

namespace AshEditor
{
	class AssetPreviewService
	{
	public:
		void SetPreviewAsset(EditorSelection selection);
		const EditorSelection& GetPreviewAsset() const;
		bool HasPreviewAsset() const;
		void Clear();

	private:
		EditorSelection _previewAsset{};
	};
}

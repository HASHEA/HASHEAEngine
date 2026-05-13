#pragma once

namespace AshEditor
{
	class AssetDatabaseService;
	class AssetPreviewService;

	struct AssetPreviewPanelDeps
	{
		AssetDatabaseService* pAssetDatabaseService = nullptr;
		AssetPreviewService* pAssetPreviewService = nullptr;
	};
}

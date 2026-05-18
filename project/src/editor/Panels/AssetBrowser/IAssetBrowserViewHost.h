#pragma once

#include <filesystem>

namespace AshEngine
{
	struct AssetInfo;
}

namespace AshEditor
{
	class IAssetBrowserViewHost
	{
	public:
		virtual ~IAssetBrowserViewHost() = default;

	public:
		virtual void SelectAsset(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void ClearAssetSelection() = 0;
		virtual void OpenAssetItem(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void OpenAssetPreview(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void NavigateToDirectory(const std::filesystem::path& refDirectoryPath) = 0;
		virtual bool CanNavigateDirectoryBack() const = 0;
		virtual bool CanNavigateDirectoryForward() const = 0;
		virtual void NavigateDirectoryBack() = 0;
		virtual void NavigateDirectoryForward() = 0;
		virtual void ResetFilters() = 0;
		virtual void BrowseToAssetLocation(const AshEngine::AssetInfo& refAsset) = 0;
	};
}

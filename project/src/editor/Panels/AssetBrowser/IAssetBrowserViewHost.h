#pragma once

#include <cstdint>
#include <filesystem>

namespace AshEngine
{
	struct AssetInfo;
}

namespace AshEditor
{
	struct AssetBrowserFrameData;

	class IAssetBrowserViewHost
	{
	public:
		virtual ~IAssetBrowserViewHost() = default;

	public:
		// Replace the current asset selection with a single primary item.
		virtual void SelectAsset(const AshEngine::AssetInfo& refAsset) = 0;
		// Keep the current multi-selection, but make the clicked item the primary target for follow-up actions.
		virtual void FocusAssetSelection(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void ToggleAssetSelection(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void SelectAssetRange(
			const AshEngine::AssetInfo& refAsset,
			const AssetBrowserFrameData& refFrameData) = 0;
		virtual bool IsAssetSelected(uint64_t uAssetId) const = 0;
		virtual void ClearAssetSelection() = 0;
		virtual void OpenAssetItem(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void OpenAssetPreview(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void OpenAssetExternally(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void RevealAssetInExplorer(const AshEngine::AssetInfo& refAsset) = 0;
		virtual void NavigateToDirectory(const std::filesystem::path& refDirectoryPath) = 0;
		virtual bool CanNavigateDirectoryBack() const = 0;
		virtual bool CanNavigateDirectoryForward() const = 0;
		virtual void NavigateDirectoryBack() = 0;
		virtual void NavigateDirectoryForward() = 0;
		virtual void ResetFilters() = 0;
		virtual void BrowseToAssetLocation(const AshEngine::AssetInfo& refAsset) = 0;
		virtual bool HandleAssetDropToDirectory(
			const std::filesystem::path& refTargetDirectoryPath,
			uint64_t uTransferId) = 0;
	};
}

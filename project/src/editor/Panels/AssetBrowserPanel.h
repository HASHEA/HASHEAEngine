#pragma once

#include "Core/EditorPanel.h"
#include "Core/IAssetBrowserActionTarget.h"
#include "Core/PanelDeps/AssetBrowserPanelDeps.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace AshEngine
{
	struct AssetInfo;
	class UIContext;
}

namespace AshEditor
{
	class EditorEventBus;

	class AssetBrowserPanel final
		: public EditorPanel
		, public IAssetBrowserActionTarget
		, public IAssetBrowserViewHost
	{
	public:
		explicit AssetBrowserPanel(AssetBrowserPanelDeps deps = {});
		~AssetBrowserPanel();

	public:
		// EditorPanel lifecycle. Called by PanelManager.
		void OnAttach() override;
		void OnDetach() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;

		// Optional event bus used for selection and shortcut-scope events.
		void BindEventBus(EditorEventBus* pEventBus);

		// Action helpers used by command bindings.
		bool CanExecuteOpenSelected() const override;
		void ExecuteOpenSelected() override;
		bool CanExecuteNavigateUp() const override;
		void ExecuteNavigateUp() override;
		bool CanExecuteCreateFolder() const override;
		void ExecuteCreateFolder() override;
		bool CanExecuteInstantiateSelected() const override;
		void ExecuteInstantiateSelected() override;
		bool CanExecuteRenameSelected() const override;
		void ExecuteRenameSelected() override;
		bool CanExecuteDeleteSelected() const override;
		void ExecuteDeleteSelected() override;
		bool CanExecuteReimportSelected() const override;
		void ExecuteReimportSelected() override;

	private:
		struct Impl;

		void ClearDeps();
		void UnsubscribeEvents();
		void DispatchContentShortcuts(const EditorFrameContext& refFrameContext, bool bContentFocused);
		void PublishContentShortcutScope(bool bContentFocused);
		const AshEngine::AssetInfo* GetSelectedAsset() const;
		std::vector<const AshEngine::AssetInfo*> GetSelectedAssets() const;
		std::vector<std::filesystem::path> GetSelectedAssetPaths(bool bRemoveNestedDescendants = false) const;
		void SelectAssetsByPaths(const std::vector<std::filesystem::path>& vecRelativePaths);
		void SelectAssetByPath(const std::filesystem::path& refRelativePath);
		bool HasAssetSelection() const;
		bool HasSingleSelectedAsset() const;
		// AssetBrowser keeps local multi-selection, but still mirrors one primary asset into SelectionService.
		void SetAssetSelection(
			std::vector<uint64_t> vecSelectedAssetIds,
			uint64_t uPrimaryAssetId,
			uint64_t uAnchorAssetId,
			bool bSyncGlobalSelection = true);
		void ResetAssetSelectionState(bool bClearGlobalSelection = true);
		void SyncPrimaryAssetSelection();
		void RequestCreateFolderModal();
		void RequestRenameSelectedModal();
		void RequestDeleteSelectedModal();
		void ClearWorkflowError();
		void DrawWorkflowModals(AshEngine::UIContext& refUi);

	private:
		void SelectAsset(const AshEngine::AssetInfo& refItem) override;
		void FocusAssetSelection(const AshEngine::AssetInfo& refItem) override;
		void ToggleAssetSelection(const AshEngine::AssetInfo& refAsset) override;
		void SelectAssetRange(
			const AshEngine::AssetInfo& refAsset,
			const AssetBrowserFrameData& refFrameData) override;
		bool IsAssetSelected(uint64_t uAssetId) const override;
		void ClearAssetSelection() override;
		void OpenAssetItem(const AshEngine::AssetInfo& refItem) override;
		void OpenAssetPreview(const AshEngine::AssetInfo& refItem) override;
		void OpenAssetExternally(const AshEngine::AssetInfo& refItem) override;
		void RevealAssetInExplorer(const AshEngine::AssetInfo& refItem) override;
		void NavigateToDirectory(const std::filesystem::path& refDirectoryPath) override;
		void NavigateToDirectoryInternal(const std::filesystem::path& refDirectoryPath, bool bRecordHistory);
		bool CanNavigateDirectoryBack() const override;
		bool CanNavigateDirectoryForward() const override;
		void NavigateDirectoryBack() override;
		void NavigateDirectoryForward() override;
		void ResetDirectoryHistory();
		void ResetFilters() override;
		void BrowseToAssetLocation(const AshEngine::AssetInfo& refItem) override;
		bool HandleAssetDropToDirectory(
			const std::filesystem::path& refTargetDirectoryPath,
			uint64_t uTransferId) override;

	private:
		std::unique_ptr<Impl> _upImpl{};
	};
}

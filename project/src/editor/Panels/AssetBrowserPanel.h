#pragma once

#include "Core/EditorPanel.h"
#include "Core/IAssetBrowserActionTarget.h"
#include "Core/PanelDeps/AssetBrowserPanelDeps.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"

#include <filesystem>
#include <memory>

namespace AshEngine
{
	struct AssetInfo;
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

	private:
		struct Impl;

		void ClearDeps();
		void UnsubscribeEvents();
		void DispatchContentShortcuts(const EditorFrameContext& refFrameContext, bool bContentFocused);
		void PublishContentShortcutScope(bool bContentFocused);

	private:
		void SelectAsset(const AshEngine::AssetInfo& refItem) override;
		void ClearAssetSelection() override;
		void OpenAssetItem(const AshEngine::AssetInfo& refItem) override;
		void OpenAssetPreview(const AshEngine::AssetInfo& refItem) override;
		void NavigateToDirectory(const std::filesystem::path& refDirectoryPath) override;
		void NavigateToDirectoryInternal(const std::filesystem::path& refDirectoryPath, bool bRecordHistory);
		bool CanNavigateDirectoryBack() const override;
		bool CanNavigateDirectoryForward() const override;
		void NavigateDirectoryBack() override;
		void NavigateDirectoryForward() override;
		void ResetDirectoryHistory();
		void ResetFilters() override;
		void BrowseToAssetLocation(const AshEngine::AssetInfo& refItem) override;

	private:
		std::unique_ptr<Impl> _upImpl{};
	};
}

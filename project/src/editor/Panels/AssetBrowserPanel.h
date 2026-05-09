#pragma once
#include "Core/EditorEventBindings.h"
#include "Core/EditorFrameContext.h"
#include "Core/EditorPanel.h"
#include "Core/IAssetBrowserActionTarget.h"
#include "Core/PanelDeps/AssetBrowserPanelDeps.h"
#include "Widgets/EditorTreeWidget.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace AshEngine
{
	struct AssetInfo;
	class UIContext;
}

namespace AshEditor
{
	class EditorEventBus;

	enum class AssetBrowserViewMode : uint8_t
	{
		List = 0,
		Icons
	};

	struct AssetBrowserVisibleItem
	{
		const AshEngine::AssetInfo* pAsset = nullptr;
		std::string strDisplayLabel{};
		std::string strLoweredLabel{};
		std::string strPathKey{};
	};

	class AssetBrowserPanel final
		: public EditorPanel
		, public IAssetBrowserActionTarget
	{
	public:
		explicit AssetBrowserPanel(AssetBrowserPanelDeps deps = {});

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
		void ClearDeps();
		void UnsubscribeEvents();
		void SelectAsset(const AshEngine::AssetInfo& refItem);
		void ClearAssetSelection();
		void ActivateAsset(const AshEngine::AssetInfo& refItem);
		void OpenAssetItem(const AshEngine::AssetInfo& refItem);
		void NavigateToDirectory(const std::filesystem::path& refDirectoryPath);
		void BrowseToAssetLocation(const AshEngine::AssetInfo& refItem);
		void HandleAssetItemInteraction(
			const EditorFrameContext& refFrameContext,
			const AshEngine::AssetInfo& refAsset,
			bool bPrimaryActivated,
			bool bDoubleClicked);
		void DrawViewModeToggle(AshEngine::UIContext& refUi, const char* pLabel, AssetBrowserViewMode eMode);
		void DrawBreadcrumbs(AshEngine::UIContext& refUi);
		void DrawAssetListView(
			const EditorFrameContext& refFrameContext,
			const std::vector<AssetBrowserVisibleItem>& vecVisibleItems,
			const AshEngine::AssetInfo* pSelectedAsset);
		void DrawAssetIconView(
			const EditorFrameContext& refFrameContext,
			const std::vector<AssetBrowserVisibleItem>& vecVisibleItems,
			const AshEngine::AssetInfo* pSelectedAsset);
		void DrawAssetItemContextMenu(const EditorFrameContext& refFrameContext, const AshEngine::AssetInfo& refAsset);
		void DrawContentContextMenu(const EditorFrameContext& refFrameContext, bool bActiveDirectoryExists, bool bFiltersActive);
		void DispatchContentShortcuts(const EditorFrameContext& refFrameContext, bool bContentFocused);
		void PublishContentShortcutScope(bool bContentFocused);

		bool HasActiveFilters() const;
		void ResetFilters();
		void SyncSettings() const;

	private:
		EditorEventBus* _pEventBus = nullptr;
		AssetBrowserPanelDeps _deps{};
		EditorEventBindings _eventBindings{};
		std::string _strSearchText{};
		std::string _strActiveDirectoryPath{};
		uint64_t _uSelectedAssetId = 0;
		bool _bActiveDirectoryExistsThisFrame = false;
		bool _bSelectedAssetVisibleThisFrame = false;
		bool _bContentShortcutScopeActive = false;
		bool _bShowDetails = true;
		int32_t _iTypeFilterIndex = 0;
		AssetBrowserViewMode _eViewMode = AssetBrowserViewMode::List;
		EditorTreeWidgetState _treeStateDirectories{};
	};
}

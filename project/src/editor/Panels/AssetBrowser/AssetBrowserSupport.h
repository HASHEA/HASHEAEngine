#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/EditorIds.h"
#include "Core/PanelDeps/AssetBrowserPanelDeps.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Gui/UICommon.h"
#include "Panels/AssetBrowser/AssetBrowserPanelState.h"
#include "Services/IEditorIconService.h"
#include "Widgets/EditorTreeWidget.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AshEditor
{
	class AssetDatabaseService;
	class IAssetBrowserViewHost;

	struct AssetTypeFilterOption
	{
		const char* pLabel = "";
		AshEngine::AssetType eType = AshEngine::AssetType::Unknown;
		bool bMatchAll = false;
	};

	struct AssetDirectoryEntry
	{
		std::filesystem::path pathRelative{};
		std::filesystem::path pathParent{};
		std::string strPathKey{};
		std::string strParentKey{};
		std::string strLabel{};
		uint32_t uChildCount = 0;
	};

	struct AssetDirectoryTreeData
	{
		std::vector<AssetDirectoryEntry> vecEntries{};
		std::unordered_set<std::string> setDirectoryKeys{};
		std::unordered_map<std::string, std::vector<size_t>> mapChildrenByParent{};
	};

	struct AssetBrowserVisibleItem
	{
		const AshEngine::AssetInfo* pAsset = nullptr;
		std::string strDisplayLabel{};
		std::string strLoweredLabel{};
		std::string strPathKey{};
	};

	struct AssetBrowserFrameData
	{
		const AshEngine::AssetInfo* pSelectedAsset = nullptr;
		AssetDirectoryTreeData directoryTreeData{};
		std::filesystem::path pathActiveDirectory{};
		std::vector<AssetBrowserVisibleItem> vecVisibleItems{};
		std::string strScopeLabel{};
		std::string strFilterSummary{};
		std::string strLastError{};
		const AssetTypeFilterOption* pTypeFilter = nullptr;
		uint32_t uFilteredCount = 0;
		uint32_t uSelectedCount = 0;
		bool bActiveDirectoryExists = false;
		bool bFiltersActive = false;
		bool bSelectedAssetVisible = false;
	};

	struct AssetBrowserViewContext
	{
		const EditorFrameContext& refFrameContext;
		const AssetBrowserPanelDeps& refDeps;
		AssetBrowserPanelState& refState;
		IAssetBrowserViewHost& refHost;
	};

	namespace AssetBrowserSupport
	{
		const std::array<AssetTypeFilterOption, 11>& GetAssetTypeFilters();
		AssetBrowserFrameData BuildFrameData(
			const AssetBrowserPanelDeps& refDeps,
			AssetBrowserPanelState& refState);
		void NormalizeSelection(
			const AssetDatabaseService& refService,
			AssetBrowserPanelState& refState);
		const AshEngine::AssetInfo* GetSelectedAsset(const AssetDatabaseService& refService, uint64_t uSelectedId);
		bool IsAssetSelected(const AssetBrowserPanelState& refState, uint64_t uSelectedId);
		bool IsSelectedAssetVisible(
			const AshEngine::AssetInfo* pSelectedAsset,
			bool bActiveDirectoryExists,
			const std::filesystem::path& pathActiveDirectory,
			const std::string& strSearchText,
			const AssetTypeFilterOption& refTypeFilter);

		bool HasActiveFilters(const AssetBrowserPanelState& refState);
		void SyncSettings(const AssetBrowserPanelDeps& refDeps, const AssetBrowserPanelState& refState);
		AssetBrowserViewMode ToAssetBrowserViewMode(int32_t iValue);

		const char* GetAssetItemContextPopupId();
		const char* GetAssetContentContextPopupId();

		void PushSelectedToolbarButtonStyle(AshEngine::UIContext& refUi);
		void PopSelectedToolbarButtonStyle(AshEngine::UIContext& refUi);
		void DrawToolbarSeparator(AshEngine::UIContext& refUi);
		void DrawToolbarLabel(AshEngine::UIContext& refUi, const char* pLabel, const AshEngine::UIColor& color);
		void DrawAssetBrowserInfoTooltip(
			AshEngine::UIContext& refUi,
			const std::filesystem::path& pathAssetRoot,
			const std::string& strScopeLabel,
			const std::string& strFilterSummary);
		void DrawAssetBrowserWarningTooltip(AshEngine::UIContext& refUi, const std::string& strWarning);

		void DrawListItemIcon(AshEngine::UIContext& refUi, AshEngine::UITextureHandle iconHandle);
		void DrawListItemLabel(
			AshEngine::UIContext& refUi,
			AshEngine::UITextureHandle iconHandle,
			std::string_view svLabel);
		void DrawItemFeedback(AshEngine::UIContext& refUi, bool bSelected, float fRounding = 4.0f);
		EditorIconId GetAssetIconId(const AshEngine::AssetInfo& refAsset);
		bool ShouldUseCompactAssetTooltip(AshEngine::AssetType eType);

		EditorTreeWidgetStyle MakeTreeStyle(AshEngine::UIContext& refUi);
		bool IsSameOrAncestorPath(
			const std::filesystem::path& pathAncestor,
			const std::filesystem::path& pathDescendant);
	}
}

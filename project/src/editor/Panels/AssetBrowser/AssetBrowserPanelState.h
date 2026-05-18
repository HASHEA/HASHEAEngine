#pragma once

#include "Widgets/EditorTreeWidget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AshEditor
{
	enum class AssetBrowserViewMode : uint8_t
	{
		List = 0,
		Icons
	};

	struct AssetBrowserPanelState
	{
		std::string strSearchText{};
		std::string strActiveDirectoryPath{};
		std::vector<std::string> vecDirectoryHistory{};
		int32_t iDirectoryHistoryIndex = -1;
		uint64_t uSelectedAssetId = 0;
		bool bActiveDirectoryExistsThisFrame = false;
		bool bSelectedAssetVisibleThisFrame = false;
		bool bContentShortcutScopeActive = false;
		int32_t iTypeFilterIndex = 0;
		AssetBrowserViewMode eViewMode = AssetBrowserViewMode::List;
		EditorTreeWidgetState treeStateDirectories{};
	};
}

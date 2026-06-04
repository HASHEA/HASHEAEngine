#pragma once

#include "Widgets/EditorTreeWidget.h"

#include <cstdint>
#include <string>
#include <filesystem>
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
		uint64_t uSelectionAnchorAssetId = 0;
		std::vector<uint64_t> vecSelectedAssetIds{};
		bool bActiveDirectoryExistsThisFrame = false;
		bool bSelectedAssetVisibleThisFrame = false;
		bool bContentShortcutScopeActive = false;
		int32_t iTypeFilterIndex = 0;
		AssetBrowserViewMode eViewMode = AssetBrowserViewMode::List;
		bool bOpenCreateFolderModal = false;
		bool bOpenRenameModal = false;
		bool bOpenDeleteModal = false;
		std::string strCreateFolderParentPath{};
		std::string strCreateFolderName{ "New Folder" };
		std::string strRenameTargetPath{};
		std::string strRenameValue{};
		std::string strDeleteTargetPath{};
		std::string strDeleteTargetLabel{};
		std::vector<std::filesystem::path> vecDeleteTargetPaths{};
		std::string strWorkflowError{};
		EditorTreeWidgetState treeStateDirectories{};
	};
}

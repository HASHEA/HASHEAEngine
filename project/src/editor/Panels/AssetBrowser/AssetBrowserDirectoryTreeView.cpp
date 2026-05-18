#include "Panels/AssetBrowser/AssetBrowserDirectoryTreeView.h"

#include "Function/Gui/UIContext.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"
#include "Services/IEditorIconService.h"

namespace AshEditor
{
	namespace
	{
		void DrawDirectoryTreeRecursive(
			EditorTreeWidget& refTreeWidget,
			AshEngine::UIContext& refUi,
			IEditorIconService* pIconService,
			const AssetDirectoryTreeData& refDirectoryTreeData,
			const AssetDirectoryEntry& refEntry,
			const std::filesystem::path& pathActiveDirectory,
			IAssetBrowserViewHost& refHost,
			bool bIsLastSibling)
		{
			const bool bSelected = refEntry.pathRelative == pathActiveDirectory;
			const std::unordered_map<std::string, std::vector<size_t>>::const_iterator itChildren =
				refDirectoryTreeData.mapChildrenByParent.find(refEntry.strPathKey);
			const bool bHasChildDirectories =
				itChildren != refDirectoryTreeData.mapChildrenByParent.end() &&
				!itChildren->second.empty();

			const std::string strBaseLabel = refEntry.strLabel + " (" + std::to_string(refEntry.uChildCount) + ")";
			const std::string strStableId = refEntry.pathRelative.empty()
				? std::string("__asset_browser_root__")
				: refEntry.strPathKey;
			EditorTreeItemDesc descItem{};
			descItem.svUniqueId = strStableId;
			descItem.svLabel = strBaseLabel;
			descItem.pIcon = pIconService ? pIconService->GetIcon(EditorIconId::FolderClosed, refUi) : nullptr;
			descItem.pIconWhenOpen = pIconService ? pIconService->GetIcon(EditorIconId::FolderOpen, refUi) : nullptr;
			descItem.bIsSelected = bSelected;
			descItem.bHasChildren = bHasChildDirectories;
			descItem.bIsDefaultOpen =
				refEntry.pathRelative.empty() ||
				AssetBrowserSupport::IsSameOrAncestorPath(refEntry.pathRelative, pathActiveDirectory);
			descItem.bIsLastSibling = bIsLastSibling;

			const EditorTreeItemResult itemResult = refTreeWidget.DrawItem(descItem);
			if (itemResult.bClicked)
			{
				refHost.NavigateToDirectory(refEntry.pathRelative);
			}

			if (!itemResult.bOpened)
			{
				return;
			}

			if (bHasChildDirectories)
			{
				refTreeWidget.PushLevel(!bIsLastSibling);
				const std::vector<size_t>& vecChildIndices = itChildren->second;
				for (size_t uChildIndex = 0; uChildIndex < vecChildIndices.size(); ++uChildIndex)
				{
					DrawDirectoryTreeRecursive(
						refTreeWidget,
						refUi,
						pIconService,
						refDirectoryTreeData,
						refDirectoryTreeData.vecEntries[vecChildIndices[uChildIndex]],
						pathActiveDirectory,
						refHost,
						uChildIndex + 1 == vecChildIndices.size());
				}
				refTreeWidget.PopLevel();
			}

			refUi.tree_pop();
		}
	}

	void AssetBrowserDirectoryTreeView::Draw(
		const AssetBrowserViewContext& refViewContext,
		const AssetBrowserFrameData& refFrameData) const
	{
		AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
		if (refFrameData.directoryTreeData.vecEntries.empty())
		{
			return;
		}

		EditorTreeWidget treeWidget(
			refUi,
			refViewContext.refState.treeStateDirectories,
			AssetBrowserSupport::MakeTreeStyle());
		treeWidget.ResetDragStateIfInactive();

		DrawDirectoryTreeRecursive(
			treeWidget,
			refUi,
			refViewContext.refDeps.pIconService,
			refFrameData.directoryTreeData,
			refFrameData.directoryTreeData.vecEntries.front(),
			refFrameData.pathActiveDirectory,
			refViewContext.refHost,
			true);
	}
}

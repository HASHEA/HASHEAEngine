#include "Panels/AssetBrowser/AssetBrowserContextMenus.h"

#include "Core/AssetPresentationUtils.h"
#include "Core/EditorIds.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Gui/UIContext.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"
#include "Services/CommandService.h"
#include "Widgets/EditorActionWidgets.h"

namespace AshEditor
{
	void AssetBrowserContextMenus::DrawAssetItemMenu(
		const AssetBrowserViewContext& refViewContext,
		const AshEngine::AssetInfo& refAsset) const
	{
		AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
		if (!refUi.begin_popup_context_item(AssetBrowserSupport::GetAssetItemContextPopupId()))
		{
			return;
		}

		if (refViewContext.refHost.IsAssetSelected(refAsset.id))
		{
			refViewContext.refHost.FocusAssetSelection(refAsset);
		}
		else
		{
			refViewContext.refHost.SelectAsset(refAsset);
		}
		const bool bCanBrowse = refAsset.is_directory || !refAsset.parent_path.empty();

		if (refUi.menu_item("Select"))
		{
			refViewContext.refHost.SelectAsset(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item(refAsset.is_directory ? "Open Folder" : "Open", nullptr, false, true))
		{
			refViewContext.refHost.OpenAssetItem(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item(refAsset.is_directory ? "Open In Explorer" : "Open Externally", nullptr, false, true))
		{
			refViewContext.refHost.OpenAssetExternally(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Browse Location", nullptr, false, bCanBrowse))
		{
			refViewContext.refHost.BrowseToAssetLocation(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Reveal In Explorer", nullptr, false, true))
		{
			refViewContext.refHost.RevealAssetInExplorer(refAsset);
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Preview", nullptr, false, !refAsset.is_directory))
		{
			refViewContext.refHost.OpenAssetPreview(refAsset);
			refUi.close_current_popup();
		}

		refUi.separator();
		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsInstantiateSelected,
			"Instantiate",
			"asset_browser.item_context",
			IsSceneInstantiableAssetType(refAsset.type)))
		{
			refUi.close_current_popup();
		}
		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsRenameSelected,
			"Rename",
			"asset_browser.item_context",
			true))
		{
			refUi.close_current_popup();
		}
		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsReimportSelected,
			"Reimport",
			"asset_browser.item_context",
			true))
		{
			refUi.close_current_popup();
		}
		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsDeleteSelected,
			"Delete",
			"asset_browser.item_context",
			true))
		{
			refUi.close_current_popup();
		}

		refUi.separator();
		if (refUi.menu_item("Clear Selection"))
		{
			refViewContext.refHost.ClearAssetSelection();
			refUi.close_current_popup();
		}
		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsRefresh,
			"Refresh",
			"asset_browser.item_context",
			true))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}

	void AssetBrowserContextMenus::DrawContentMenu(
		const AssetBrowserViewContext& refViewContext,
		const AssetBrowserFrameData& refFrameData) const
	{
		AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
		if (!refUi.begin_popup(AssetBrowserSupport::GetAssetContentContextPopupId()))
		{
			return;
		}

		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsCreateFolder,
			"New Folder",
			"asset_browser.content_context",
			true))
		{
			refUi.close_current_popup();
		}
		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsNavigateUp,
			"Up",
			"asset_browser.content_context",
			true))
		{
			refUi.close_current_popup();
		}
		refUi.separator();

		if (refUi.menu_item("List View", nullptr, refViewContext.refState.eViewMode == AssetBrowserViewMode::List))
		{
			refViewContext.refState.eViewMode = AssetBrowserViewMode::List;
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Icon View", nullptr, refViewContext.refState.eViewMode == AssetBrowserViewMode::Icons))
		{
			refViewContext.refState.eViewMode = AssetBrowserViewMode::Icons;
			refUi.close_current_popup();
		}

		refUi.separator();
		if (refUi.menu_item("Reset Filters", nullptr, false, refFrameData.bFiltersActive))
		{
			refViewContext.refHost.ResetFilters();
			refUi.close_current_popup();
		}
		if (refUi.menu_item("Clear Selection"))
		{
			refViewContext.refHost.ClearAssetSelection();
			refUi.close_current_popup();
		}
		if (refViewContext.refDeps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*refViewContext.refDeps.pCommandService,
			EditorActionIds::AssetsRefresh,
			"Refresh",
			"asset_browser.content_context",
			true))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}
}

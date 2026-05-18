#include "Panels/AssetBrowser/AssetBrowserToolbarView.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"
#include "Services/AssetDatabaseService.h"
#include "Services/CommandService.h"
#include "Widgets/EditorActionWidgets.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <vector>

namespace AshEditor
{
	namespace
	{
		void DrawViewModeToggle(
			AshEngine::UIContext& refUi,
			AssetBrowserPanelState& refState,
			const char* pLabel,
			AssetBrowserViewMode eMode)
		{
			const bool bSelected = refState.eViewMode == eMode;
			if (bSelected)
			{
				AssetBrowserSupport::PushSelectedToolbarButtonStyle(refUi);
			}

			if (refUi.small_button(pLabel))
			{
				refState.eViewMode = eMode;
			}

			if (bSelected)
			{
				AssetBrowserSupport::PopSelectedToolbarButtonStyle(refUi);
			}
		}
	}

	void AssetBrowserToolbarView::Draw(
		const AssetBrowserViewContext& refViewContext,
		const AssetBrowserFrameData& refFrameData) const
	{
		AshEngine::UIContext& refUi = *refViewContext.refFrameContext.pUiContext;
		AssetBrowserPanelState& refState = refViewContext.refState;

		const std::array<AssetTypeFilterOption, 11>& arrTypeFilters = AssetBrowserSupport::GetAssetTypeFilters();
		std::vector<const char*> vecTypeLabels{};
		vecTypeLabels.reserve(arrTypeFilters.size());
		for (const AssetTypeFilterOption& refTypeFilter : arrTypeFilters)
		{
			vecTypeLabels.push_back(refTypeFilter.pLabel);
		}

		refUi.set_next_item_width(220.0f);
		refUi.input_text("Search", refState.strSearchText);
		refUi.same_line();
		refUi.set_next_item_width(124.0f);
		refUi.combo("Type", refState.iTypeFilterIndex, vecTypeLabels);

		refUi.same_line();
		refUi.begin_disabled(!refFrameData.bFiltersActive);
		if (refUi.small_button("Reset"))
		{
			refViewContext.refHost.ResetFilters();
		}
		refUi.end_disabled();

		AssetBrowserSupport::DrawToolbarSeparator(refUi);
		refUi.begin_disabled(!refViewContext.refHost.CanNavigateDirectoryBack());
		if (refUi.small_button("<"))
		{
			refViewContext.refHost.NavigateDirectoryBack();
		}
		refUi.end_disabled();

		refUi.same_line();
		refUi.begin_disabled(!refViewContext.refHost.CanNavigateDirectoryForward());
		if (refUi.small_button(">"))
		{
			refViewContext.refHost.NavigateDirectoryForward();
		}
		refUi.end_disabled();

		refUi.same_line();
		if (refViewContext.refDeps.pCommandService)
		{
			DrawEditorActionSmallButton(
				refUi,
				*refViewContext.refDeps.pCommandService,
				EditorActionIds::AssetsNavigateUp,
				"Up",
				"asset_browser.toolbar");
		}
		else
		{
			refUi.begin_disabled(true);
			refUi.small_button("Up");
			refUi.end_disabled();
		}

		AssetBrowserSupport::DrawToolbarSeparator(refUi);
		DrawViewModeToggle(refUi, refState, "List", AssetBrowserViewMode::List);
		refUi.same_line();
		DrawViewModeToggle(refUi, refState, "Icons", AssetBrowserViewMode::Icons);
		refUi.same_line();
		if (refViewContext.refDeps.pCommandService)
		{
			DrawEditorActionButton(
				refUi,
				*refViewContext.refDeps.pCommandService,
				EditorActionIds::AssetsRefresh,
				"Refresh",
				"asset_browser.toolbar");
		}
		else
		{
			refUi.begin_disabled(true);
			refUi.button("Refresh");
			refUi.end_disabled();
		}

		AssetBrowserSupport::DrawToolbarSeparator(refUi);
		AssetBrowserSupport::DrawToolbarLabel(
			refUi,
			"Info",
			GetEditorMutedTextColor(refUi));
		AssetBrowserSupport::DrawAssetBrowserInfoTooltip(
			refUi,
			refViewContext.refDeps.pAssetDatabaseService
				? refViewContext.refDeps.pAssetDatabaseService->GetAssetRoot()
				: std::filesystem::path{},
			refFrameData.strScopeLabel,
			refFrameData.strFilterSummary);

		if (!refFrameData.strLastError.empty())
		{
			AssetBrowserSupport::DrawToolbarSeparator(refUi);
			AssetBrowserSupport::DrawToolbarLabel(
				refUi,
				"Warning",
				GetEditorWarningTextColor(refUi));
			AssetBrowserSupport::DrawAssetBrowserWarningTooltip(refUi, refFrameData.strLastError);
		}
	}
}

#include "Panels/AssetBrowser/AssetBrowserBreadcrumbsView.h"

#include "Function/Gui/UIContext.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"

namespace AshEditor
{
	void AssetBrowserBreadcrumbsView::Draw(
		AshEngine::UIContext& refUi,
		const AssetBrowserPanelState& refState,
		IAssetBrowserViewHost& refHost) const
	{
		refUi.text_colored({ 0.67f, 0.70f, 0.76f, 1.0f }, "Location");
		refUi.same_line();

		const bool bAllAssetsSelected = refState.strActiveDirectoryPath.empty();
		if (bAllAssetsSelected)
		{
			AssetBrowserSupport::PushSelectedToolbarButtonStyle(refUi);
		}
		if (refUi.small_button("All Assets"))
		{
			refHost.NavigateToDirectory({});
		}
		if (bAllAssetsSelected)
		{
			AssetBrowserSupport::PopSelectedToolbarButtonStyle(refUi);
		}

		if (refState.strActiveDirectoryPath.empty())
		{
			return;
		}

		std::filesystem::path pathCurrent(refState.strActiveDirectoryPath);
		std::filesystem::path pathBreadcrumb{};
		for (const std::filesystem::path& refPart : pathCurrent)
		{
			pathBreadcrumb /= refPart;
			refUi.same_line();
			refUi.text_unformatted("/");
			refUi.same_line();

			const std::string strLabel = refPart.generic_string();
			const std::string strPathId = pathBreadcrumb.generic_string();
			const bool bIsCurrent = strPathId == refState.strActiveDirectoryPath;
			refUi.push_id(strPathId.c_str());
			if (bIsCurrent)
			{
				AssetBrowserSupport::PushSelectedToolbarButtonStyle(refUi);
			}
			if (refUi.small_button(strLabel.c_str()))
			{
				refHost.NavigateToDirectory(pathBreadcrumb);
			}
			if (bIsCurrent)
			{
				AssetBrowserSupport::PopSelectedToolbarButtonStyle(refUi);
			}
			refUi.pop_id();
		}
	}
}

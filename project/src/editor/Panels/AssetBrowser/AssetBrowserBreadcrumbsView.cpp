#include "Panels/AssetBrowser/AssetBrowserBreadcrumbsView.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/AssetBrowser/IAssetBrowserViewHost.h"
#include "Widgets/EditorThemeColors.h"

namespace AshEditor
{
	namespace
	{
		void HandleBreadcrumbDropTarget(
			AshEngine::UIContext& refUi,
			IAssetBrowserViewHost& refHost,
			const std::filesystem::path& pathTargetDirectory)
		{
			if (!refUi.begin_drag_drop_target())
			{
				return;
			}

			const AshEngine::UIDragDropPayload payload =
				refUi.accept_drag_drop_payload(EditorDragPayloadTypes::Asset);
			if (payload.is_valid() &&
				payload.data_size == static_cast<int>(sizeof(uint64_t)))
			{
				const uint64_t uTransferId = *static_cast<const uint64_t*>(payload.data);
				refHost.HandleAssetDropToDirectory(pathTargetDirectory, uTransferId);
			}

			refUi.end_drag_drop_target();
		}
	}

	void AssetBrowserBreadcrumbsView::Draw(
		AshEngine::UIContext& refUi,
		const AssetBrowserPanelState& refState,
		IAssetBrowserViewHost& refHost) const
	{
		refUi.text_colored(GetEditorMutedTextColor(refUi), "Location");
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
		HandleBreadcrumbDropTarget(refUi, refHost, {});
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
			HandleBreadcrumbDropTarget(refUi, refHost, pathBreadcrumb);
			if (bIsCurrent)
			{
				AssetBrowserSupport::PopSelectedToolbarButtonStyle(refUi);
			}
			refUi.pop_id();
		}
	}
}

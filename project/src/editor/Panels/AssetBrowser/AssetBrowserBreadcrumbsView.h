#pragma once

#include "Panels/AssetBrowser/AssetBrowserSupport.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class AssetBrowserBreadcrumbsView
	{
	public:
		void Draw(
			AshEngine::UIContext& refUi,
			const AssetBrowserPanelState& refState,
			IAssetBrowserViewHost& refHost) const;
	};
}

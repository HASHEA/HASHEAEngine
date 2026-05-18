#pragma once

#include "Panels/AssetBrowser/AssetBrowserSupport.h"

namespace AshEngine
{
	struct AssetInfo;
}

namespace AshEditor
{
	class AssetBrowserContextMenus
	{
	public:
		void DrawAssetItemMenu(
			const AssetBrowserViewContext& refViewContext,
			const AshEngine::AssetInfo& refAsset) const;
		void DrawContentMenu(
			const AssetBrowserViewContext& refViewContext,
			const AssetBrowserFrameData& refFrameData) const;
	};
}

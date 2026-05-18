#pragma once

#include "Panels/AssetBrowser/AssetBrowserSupport.h"

namespace AshEditor
{
	class AssetBrowserContextMenus;

	struct AssetBrowserContentDrawResult
	{
		bool bOpenContentMenu = false;
		bool bClearContentSelection = false;
		bool bContentFocused = false;
	};

	class AssetBrowserContentView
	{
	public:
		AssetBrowserContentDrawResult Draw(
			const AssetBrowserViewContext& refViewContext,
			const AssetBrowserFrameData& refFrameData,
			const AssetBrowserContextMenus& refContextMenus) const;
	};
}

#pragma once

#include "Panels/AssetBrowser/AssetBrowserSupport.h"

namespace AshEditor
{
	class AssetBrowserToolbarView
	{
	public:
		void Draw(
			const AssetBrowserViewContext& refViewContext,
			const AssetBrowserFrameData& refFrameData) const;
	};
}

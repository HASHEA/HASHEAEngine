#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/ViewportPanelDeps.h"

#include <string>

namespace AshEditor
{
	struct EditorViewportInstance;

	namespace ViewportPanelToolbar
	{
		void Draw(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport);
	}
}

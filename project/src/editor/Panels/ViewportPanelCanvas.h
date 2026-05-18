#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/ViewportPanelDeps.h"
#include "Function/Gui/UICommon.h"
#include "Panels/ViewportPanelInteraction.h"

#include <string>

namespace AshEditor
{
	struct EditorViewportInstance;

	struct ViewportPanelCanvasDrawResult
	{
		bool bHasViewportContent = false;
		bool bContentHovered = false;
		AshEngine::UIRect rectContent{};
	};

	namespace ViewportPanelCanvas
	{
		ViewportPanelCanvasDrawResult Draw(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			EditorViewportInstance& refViewport);
		void DrawDecorations(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport,
			const ViewportPanelCanvasDrawResult& refDrawResult,
			const ViewportPanelSceneBoxSelectionState& refSceneBoxSelectionState);
	}
}

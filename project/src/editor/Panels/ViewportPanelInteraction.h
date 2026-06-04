#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/ViewportPanelDeps.h"
#include "Function/Gui/UICommon.h"
#include "Panels/ViewportPanelState.h"

#include <string>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	struct EditorViewportInstance;

	namespace ViewportPanelInteraction
	{
		void HandleViewportInput(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport,
			const AshEngine::UIRect& rectContent,
			bool bContentHovered,
			ViewportPanelSceneSelectionState& refSceneSelectionState);
		void DrawSceneBoxSelectionOverlay(
			AshEngine::UIContext& refUi,
			const AshEngine::UIRect& rectContent,
			const ViewportPanelSceneSelectionState& refSceneSelectionState);
		void HandleDragDropTarget(
			AshEngine::UIContext& refUi,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectViewportContent,
			bool bHasViewportContent);
	}
}

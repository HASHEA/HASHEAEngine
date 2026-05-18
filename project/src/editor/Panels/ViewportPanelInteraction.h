#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/PanelDeps/ViewportPanelDeps.h"
#include "Function/Gui/UICommon.h"

#include <string>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	struct EditorViewportInstance;

	struct ViewportPanelSceneBoxSelectionState
	{
		bool bTracking = false;
		bool bDragging = false;
		AshEngine::UIVec2 vecStartScreen{};
		AshEngine::UIVec2 vecCurrentScreen{};
		AshEngine::UIModifierFlags uStartModifiers = AshEngine::UIModifierFlagBits::None;
	};

	namespace ViewportPanelInteraction
	{
		void HandleViewportInput(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport,
			const AshEngine::UIRect& rectContent,
			bool bContentHovered,
			ViewportPanelSceneBoxSelectionState& refSceneBoxSelectionState);
		void DrawSceneBoxSelectionOverlay(
			AshEngine::UIContext& refUi,
			const AshEngine::UIRect& rectContent,
			const ViewportPanelSceneBoxSelectionState& refSceneBoxSelectionState);
		void HandleDragDropTarget(
			AshEngine::UIContext& refUi,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectViewportContent,
			bool bHasViewportContent);
	}
}

#pragma once

#include "Function/Gui/UICommon.h"
#include "Function/Render/ScenePresentationHandles.h"

#include <string>

namespace AshEditor
{
	struct ViewportPanelTerrainInteractionState
	{
		bool bOwnsMouseLeftPress = false;
	};

	struct ViewportPanelSceneSelectionState
	{
		bool bTracking = false;
		bool bDragging = false;
		AshEngine::UIVec2 vecStartScreen{};
		AshEngine::UIVec2 vecCurrentScreen{};
		AshEngine::UIModifierFlags uStartModifiers = AshEngine::UIModifierFlagBits::None;
		bool bPendingPick = false;
		std::string strPendingPickViewportId{};
		AshEngine::SceneViewBindingHandle pendingPickBinding{};
		AshEngine::UIRect rectPendingPickContent{};
		AshEngine::UIVec2 vecPendingPickScreen{};
		AshEngine::UIModifierFlags uPendingPickModifiers = AshEngine::UIModifierFlagBits::None;
		double dPendingPickRequestTimeSeconds = 0.0;
	};
}

#pragma once

#include "Function/Render/ScenePresentationHandles.h"
#include "Function/Gui/UICommon.h"

#include <cstdint>
#include <string>

namespace AshEditor
{
	struct EditorViewportState
	{
		uint32_t uWidth = 0;
		uint32_t uHeight = 0;
		uint32_t uRequestedWidth = 0;
		uint32_t uRequestedHeight = 0;
		bool bFocused = false;
		bool bHovered = false;
		bool bContentHovered = false;
		AshEngine::UIRect rectContent{};
	};

	struct EditorViewportInstance
	{
		std::string strId{};
		std::string strDisplayName{};
		EditorViewportState state{};
		AshEngine::UISurfaceHandle surface{};
	};
}

#pragma once

#include "Function/Gui/UICommon.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	AshEngine::UIColor GetEditorTextColor(AshEngine::UIContext& refUi, float fAlpha = 1.0f);
	AshEngine::UIColor GetEditorMutedTextColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorHeadingTextColor(AshEngine::UIContext& refUi);
}

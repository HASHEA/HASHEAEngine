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
	AshEngine::UIColor GetEditorAccentTextColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorSubtleTextColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorSuccessTextColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorWarningTextColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorErrorTextColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorGuideLineColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorDropAccentColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorRowHoverFillColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorRowHoverOutlineColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorRowSelectedFillColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorRowSelectedOutlineColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorOverlayBackgroundColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorOverlayBorderColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorDropZoneFillColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorDropZoneHoverColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorDropZoneActiveColor(AshEngine::UIContext& refUi);
	AshEngine::UIColor GetEditorDropZoneBorderColor(AshEngine::UIContext& refUi);
	void PushEditorSelectedButtonStyle(AshEngine::UIContext& refUi);
	void PopEditorSelectedButtonStyle(AshEngine::UIContext& refUi);
}

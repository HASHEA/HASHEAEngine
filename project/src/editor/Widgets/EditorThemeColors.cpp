#include "Widgets/EditorThemeColors.h"

#include "Function/Gui/UIContext.h"

namespace AshEditor
{
	AshEngine::UIColor GetEditorTextColor(AshEngine::UIContext& refUi, const float fAlpha)
	{
		AshEngine::UIColor color = refUi.get_style_color(AshEngine::UIStyleColorKind::Text);
		color.a *= fAlpha;
		return color;
	}

	AshEngine::UIColor GetEditorMutedTextColor(AshEngine::UIContext& refUi)
	{
		return GetEditorTextColor(refUi, 0.86f);
	}

	AshEngine::UIColor GetEditorHeadingTextColor(AshEngine::UIContext& refUi)
	{
		return GetEditorTextColor(refUi);
	}
}

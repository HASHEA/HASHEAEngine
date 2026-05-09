#include "Widgets/EditorButtonWidgets.h"

#include "Function/Gui/UIContext.h"

namespace AshEditor
{
	const EditorButtonVisuals& GetEditorPrimaryButtonVisuals()
	{
		static const EditorButtonVisuals sPrimaryButtonVisuals{};
		return sPrimaryButtonVisuals;
	}

	void PushEditorButtonVisuals(AshEngine::UIContext& refUi, const EditorButtonVisuals& refVisuals)
	{
		refUi.push_style_color(AshEngine::UIStyleColorKind::Button, refVisuals.colorButton);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, refVisuals.colorButtonHovered);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, refVisuals.colorButtonActive);
	}

	void PopEditorButtonVisuals(AshEngine::UIContext& refUi)
	{
		refUi.pop_style_color(3);
	}

	bool DrawEditorToggleButton(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		bool& bValue,
		const EditorButtonVisuals& refEnabledVisuals)
	{
		const bool bWasEnabled = bValue;
		if (bWasEnabled)
		{
			PushEditorButtonVisuals(refUi, refEnabledVisuals);
		}

		const bool bClicked = refUi.small_button(pLabel);
		if (bClicked)
		{
			bValue = !bValue;
		}

		if (bWasEnabled)
		{
			PopEditorButtonVisuals(refUi);
		}

		return bClicked;
	}
}

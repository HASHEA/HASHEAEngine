#include "Widgets/EditorButtonWidgets.h"

#include "Function/Gui/UIContext.h"
#include "Widgets/EditorThemeColors.h"

namespace AshEditor
{
	namespace
	{
		bool IsColorUnset(const AshEngine::UIColor& refColor)
		{
			return refColor.a <= 0.0f;
		}

		void ApplyMissingButtonVisuals(AshEngine::UIContext& refUi, EditorButtonVisuals& refVisuals)
		{
			const EditorButtonVisuals primaryVisuals = GetEditorPrimaryButtonVisuals(refUi);
			if (IsColorUnset(refVisuals.colorButton))
			{
				refVisuals.colorButton = primaryVisuals.colorButton;
			}
			if (IsColorUnset(refVisuals.colorButtonHovered))
			{
				refVisuals.colorButtonHovered = primaryVisuals.colorButtonHovered;
			}
			if (IsColorUnset(refVisuals.colorButtonActive))
			{
				refVisuals.colorButtonActive = primaryVisuals.colorButtonActive;
			}
		}
	}

	EditorButtonVisuals GetEditorPrimaryButtonVisuals(AshEngine::UIContext& refUi)
	{
		return {
			refUi.get_style_color(AshEngine::UIStyleColorKind::Header),
			refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderHovered),
			refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderActive)
		};
	}

	void PushEditorButtonVisuals(AshEngine::UIContext& refUi, const EditorButtonVisuals& refVisuals)
	{
		EditorButtonVisuals visuals = refVisuals;
		ApplyMissingButtonVisuals(refUi, visuals);
		refUi.push_style_color(AshEngine::UIStyleColorKind::Button, visuals.colorButton);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, visuals.colorButtonHovered);
		refUi.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, visuals.colorButtonActive);
	}

	void PushEditorPrimaryButtonVisuals(AshEngine::UIContext& refUi)
	{
		PushEditorSelectedButtonStyle(refUi);
	}

	void PopEditorButtonVisuals(AshEngine::UIContext& refUi)
	{
		refUi.pop_style_color(3);
	}

	bool DrawEditorToggleButton(AshEngine::UIContext& refUi, const char* pLabel, bool& bValue)
	{
		const bool bWasEnabled = bValue;
		if (bWasEnabled)
		{
			PushEditorPrimaryButtonVisuals(refUi);
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

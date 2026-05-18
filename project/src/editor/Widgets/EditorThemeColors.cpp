#include "Widgets/EditorThemeColors.h"

#include "Function/Gui/UIContext.h"

namespace AshEditor
{
	namespace
	{
		AshEngine::UIColor WithAlpha(AshEngine::UIColor color, const float fAlpha)
		{
			color.a *= fAlpha;
			return color;
		}
	}

	AshEngine::UIColor GetEditorTextColor(AshEngine::UIContext& refUi, const float fAlpha)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::Text), fAlpha);
	}

	AshEngine::UIColor GetEditorMutedTextColor(AshEngine::UIContext& refUi)
	{
		return refUi.get_style_color(AshEngine::UIStyleColorKind::TextDisabled);
	}

	AshEngine::UIColor GetEditorHeadingTextColor(AshEngine::UIContext& refUi)
	{
		return GetEditorTextColor(refUi);
	}

	AshEngine::UIColor GetEditorAccentTextColor(AshEngine::UIContext& refUi)
	{
		return refUi.get_style_color(AshEngine::UIStyleColorKind::Text);
	}

	AshEngine::UIColor GetEditorSubtleTextColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::TextDisabled), 0.88f);
	}

	AshEngine::UIColor GetEditorSuccessTextColor(AshEngine::UIContext& refUi)
	{
		return refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderActive);
	}

	AshEngine::UIColor GetEditorWarningTextColor(AshEngine::UIContext& refUi)
	{
		return refUi.get_style_color(AshEngine::UIStyleColorKind::SeparatorActive);
	}

	AshEngine::UIColor GetEditorErrorTextColor(AshEngine::UIContext& refUi)
	{
		return refUi.get_style_color(AshEngine::UIStyleColorKind::ButtonActive);
	}

	AshEngine::UIColor GetEditorGuideLineColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::Separator), 0.55f);
	}

	AshEngine::UIColor GetEditorDropAccentColor(AshEngine::UIContext& refUi)
	{
		return refUi.get_style_color(AshEngine::UIStyleColorKind::DragDropTarget);
	}

	AshEngine::UIColor GetEditorRowHoverFillColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderHovered), 0.18f);
	}

	AshEngine::UIColor GetEditorRowHoverOutlineColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderHovered), 0.42f);
	}

	AshEngine::UIColor GetEditorRowSelectedFillColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderActive), 0.30f);
	}

	AshEngine::UIColor GetEditorRowSelectedOutlineColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderActive), 0.88f);
	}

	AshEngine::UIColor GetEditorOverlayBackgroundColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::ChildBg), 0.72f);
	}

	AshEngine::UIColor GetEditorOverlayBorderColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::Border), 0.36f);
	}

	AshEngine::UIColor GetEditorDropZoneFillColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::Button), 0.42f);
	}

	AshEngine::UIColor GetEditorDropZoneHoverColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::ButtonHovered), 0.56f);
	}

	AshEngine::UIColor GetEditorDropZoneActiveColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::ButtonActive), 0.72f);
	}

	AshEngine::UIColor GetEditorDropZoneBorderColor(AshEngine::UIContext& refUi)
	{
		return WithAlpha(refUi.get_style_color(AshEngine::UIStyleColorKind::Border), 0.72f);
	}

	void PushEditorSelectedButtonStyle(AshEngine::UIContext& refUi)
	{
		refUi.push_style_color(
			AshEngine::UIStyleColorKind::Button,
			refUi.get_style_color(AshEngine::UIStyleColorKind::Header));
		refUi.push_style_color(
			AshEngine::UIStyleColorKind::ButtonHovered,
			refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderHovered));
		refUi.push_style_color(
			AshEngine::UIStyleColorKind::ButtonActive,
			refUi.get_style_color(AshEngine::UIStyleColorKind::HeaderActive));
	}

	void PopEditorSelectedButtonStyle(AshEngine::UIContext& refUi)
	{
		refUi.pop_style_color(3);
	}
}

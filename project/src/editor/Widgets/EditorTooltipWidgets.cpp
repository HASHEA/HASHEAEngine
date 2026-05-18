#include "Widgets/EditorTooltipWidgets.h"

#include "Function/Gui/UICommon.h"
#include "Function/Gui/UIContext.h"
#include "Widgets/EditorThemeColors.h"

#include <string>

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UITableFlags kTooltipTableFlags =
			AshEngine::UITableFlagBits::SizingStretchProp |
			AshEngine::UITableFlagBits::BordersInner;
	}

	bool BeginEditorTooltipTable(
		AshEngine::UIContext& refUi,
		const char* pTableId,
		float fLabelWidth)
	{
		if (!refUi.begin_table(pTableId, 2, kTooltipTableFlags))
		{
			return false;
		}

		refUi.table_setup_column("Label", AshEngine::UITableColumnFlagBits::WidthFixed, fLabelWidth);
		refUi.table_setup_column("Value", AshEngine::UITableColumnFlagBits::WidthStretch);
		return true;
	}

	void DrawEditorTooltipTitle(
		AshEngine::UIContext& refUi,
		std::string_view svTitle,
		std::string_view svSubtitle)
	{
		if (!svTitle.empty())
		{
			const std::string strTitle(svTitle);
			refUi.push_font(AshEngine::UIFontRole::Strong);
			refUi.text_colored(GetEditorHeadingTextColor(refUi), "%s", strTitle.c_str());
			refUi.pop_font();
		}
		if (!svSubtitle.empty())
		{
			const std::string strSubtitle(svSubtitle);
			refUi.text_colored_scaled(0.86f, GetEditorMutedTextColor(refUi), "%s", strSubtitle.c_str());
		}
		if (!svTitle.empty() || !svSubtitle.empty())
		{
			refUi.separator();
		}
	}

	void DrawEditorTooltipCompactTitle(
		AshEngine::UIContext& refUi,
		std::string_view svTitle,
		std::string_view svSubtitle)
	{
		if (!svTitle.empty())
		{
			const std::string strTitle(svTitle);
			refUi.push_font(AshEngine::UIFontRole::Strong);
			refUi.text_colored(GetEditorHeadingTextColor(refUi), "%s", strTitle.c_str());
			refUi.pop_font();
		}
		if (!svSubtitle.empty())
		{
			if (!svTitle.empty())
			{
				refUi.same_line(0.0f, 8.0f);
			}
			const std::string strSubtitle(svSubtitle);
			refUi.text_colored_scaled(0.84f, GetEditorMutedTextColor(refUi), "%s", strSubtitle.c_str());
		}
	}

	void DrawEditorTooltipRow(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		std::string_view svValue)
	{
		refUi.table_next_row();
		refUi.table_next_column();
		refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", pLabel ? pLabel : "-");
		refUi.table_next_column();
		const std::string strValue(svValue.empty() ? std::string_view("-") : svValue);
		refUi.text_wrapped("%s", strValue.c_str());
	}

	void DrawEditorTooltipCompactRow(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		std::string_view svValue)
	{
		refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", pLabel ? pLabel : "-");
		refUi.same_line(0.0f, 6.0f);
		const std::string strValue(svValue.empty() ? std::string_view("-") : svValue);
		refUi.text_wrapped("%s", strValue.c_str());
	}

	void DrawEditorTooltipDescription(
		AshEngine::UIContext& refUi,
		std::string_view svText)
	{
		const std::string strText(svText.empty() ? std::string_view("-") : svText);
		refUi.text_wrapped_scaled(0.92f, "%s", strText.c_str());
	}

	void DrawEditorTooltipCaption(
		AshEngine::UIContext& refUi,
		std::string_view svText)
	{
		const std::string strText(svText.empty() ? std::string_view("-") : svText);
		refUi.text_colored_scaled(0.82f, GetEditorMutedTextColor(refUi), "%s", strText.c_str());
	}
}

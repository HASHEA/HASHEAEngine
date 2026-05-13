#pragma once

#include <string_view>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	bool BeginEditorTooltipTable(
		AshEngine::UIContext& refUi,
		const char* pTableId,
		float fLabelWidth = 96.0f);

	void DrawEditorTooltipTitle(
		AshEngine::UIContext& refUi,
		std::string_view svTitle,
		std::string_view svSubtitle = {});

	void DrawEditorTooltipCompactTitle(
		AshEngine::UIContext& refUi,
		std::string_view svTitle,
		std::string_view svSubtitle = {});

	void DrawEditorTooltipRow(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		std::string_view svValue);

	void DrawEditorTooltipCompactRow(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		std::string_view svValue);

	void DrawEditorTooltipDescription(
		AshEngine::UIContext& refUi,
		std::string_view svText);

	void DrawEditorTooltipCaption(
		AshEngine::UIContext& refUi,
		std::string_view svText);
}

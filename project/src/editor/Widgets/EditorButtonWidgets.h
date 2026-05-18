#pragma once

#include "Function/Gui/UICommon.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	struct EditorButtonVisuals
	{
		AshEngine::UIColor colorButton{ 0.0f, 0.0f, 0.0f, 0.0f };
		AshEngine::UIColor colorButtonHovered{ 0.0f, 0.0f, 0.0f, 0.0f };
		AshEngine::UIColor colorButtonActive{ 0.0f, 0.0f, 0.0f, 0.0f };
	};

	EditorButtonVisuals GetEditorPrimaryButtonVisuals(AshEngine::UIContext& refUi);
	void PushEditorButtonVisuals(AshEngine::UIContext& refUi, const EditorButtonVisuals& refVisuals);
	void PushEditorPrimaryButtonVisuals(AshEngine::UIContext& refUi);
	void PopEditorButtonVisuals(AshEngine::UIContext& refUi);
	bool DrawEditorToggleButton(AshEngine::UIContext& refUi, const char* pLabel, bool& bValue);
	bool DrawEditorToggleButton(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		bool& bValue,
		const EditorButtonVisuals& refEnabledVisuals);
}

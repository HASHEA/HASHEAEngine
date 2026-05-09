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
		AshEngine::UIColor colorButton{ 0.42f, 0.49f, 0.57f, 1.0f };
		AshEngine::UIColor colorButtonHovered{ 0.46f, 0.53f, 0.62f, 1.0f };
		AshEngine::UIColor colorButtonActive{ 0.38f, 0.45f, 0.53f, 1.0f };
	};

	const EditorButtonVisuals& GetEditorPrimaryButtonVisuals();
	void PushEditorButtonVisuals(AshEngine::UIContext& refUi, const EditorButtonVisuals& refVisuals = GetEditorPrimaryButtonVisuals());
	void PopEditorButtonVisuals(AshEngine::UIContext& refUi);
	bool DrawEditorToggleButton(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		bool& bValue,
		const EditorButtonVisuals& refEnabledVisuals = GetEditorPrimaryButtonVisuals());
}

#pragma once

#include "Function/Gui/UICommon.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class EditorSessionStateService;

	struct EditorStatusBarContext
	{
		AshEngine::UIContext& refUi;
		const EditorSessionStateService& refSessionState;
	};

	class EditorStatusBarController final
	{
	public:
		float GetPreferredHeight(const AshEngine::UIContext& refUi) const;
		void Draw(EditorStatusBarContext& refContext) const;
	};
}

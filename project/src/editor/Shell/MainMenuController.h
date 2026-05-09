#pragma once

#include "Core/IActionInvoker.h"
#include "Core/IThemeApplier.h"
#include "Function/Gui/UICommon.h"

#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class CommandService;
	class EditorPanel;
	class EditorSessionStateService;
	class PanelManager;

	struct MainMenuControllerContext
	{
		AshEngine::UIContext& refUi;
		CommandService& refCommandService;
		const EditorSessionStateService& refSessionState;
		const std::vector<std::unique_ptr<EditorPanel>>& refPanels;
		IActionInvoker* pActionInvoker = nullptr;
		PanelManager* pPanelManager = nullptr;
		IThemeApplier* pThemeApplier = nullptr;
	};

	class MainMenuController final
	{
	public:
		void Draw(MainMenuControllerContext& refContext) const;

	private:
		bool DrawActionMenuItem(MainMenuControllerContext& refContext, const char* pActionId, bool bEnabled = true) const;
		void DrawThemeMenu(MainMenuControllerContext& refContext) const;
	};
}

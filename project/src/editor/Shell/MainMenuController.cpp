#include "Shell/MainMenuController.h"

#include "Core/EditorIds.h"
#include "Core/EditorPanel.h"
#include "Function/Gui/UIContext.h"
#include "Services/CommandService.h"
#include "Services/EditorSessionStateService.h"
#include "Services/EditorSettingsService.h"
#include "Shell/PanelManager.h"
#include "Widgets/EditorActionWidgets.h"

#include <array>

namespace AshEditor
{
	void MainMenuController::Draw(MainMenuControllerContext& refContext) const
	{
		if (!refContext.refUi.begin_main_menu_bar())
		{
			return;
		}

		if (refContext.refUi.begin_menu("File"))
		{
			DrawActionMenuItem(refContext, EditorActionIds::FileNewScene);
			DrawActionMenuItem(refContext, EditorActionIds::FileReloadScene);
			DrawActionMenuItem(refContext, EditorActionIds::FileSaveScene);
			refContext.refUi.separator();
			refContext.refUi.text_colored(
				{ 0.70f, 0.70f, 0.70f, 1.0f },
				"Current Scene: %s",
				refContext.refSessionState.GetActiveScene().strSceneName.c_str());
			refContext.refUi.end_menu();
		}

		if (refContext.refUi.begin_menu("Edit"))
		{
			DrawActionMenuItem(refContext, EditorActionIds::EditUndo);
			DrawActionMenuItem(refContext, EditorActionIds::EditRedo);
			const EditorUndoHistoryChangedEvent& refUndoHistory = refContext.refSessionState.GetUndoHistory();
			if (refUndoHistory.bHasOpenTransaction)
			{
				refContext.refUi.separator();
				refContext.refUi.text_colored(
					{ 0.82f, 0.74f, 0.44f, 1.0f },
					"Transaction: %s",
					refUndoHistory.strOpenTransactionLabel.empty() ? "<Unnamed>" : refUndoHistory.strOpenTransactionLabel.c_str());
			}
			refContext.refUi.end_menu();
		}

		if (refContext.refUi.begin_menu("Scene"))
		{
			DrawActionMenuItem(refContext, EditorActionIds::SceneCreateRoot);
			DrawActionMenuItem(refContext, EditorActionIds::SceneCreateChild);
			refContext.refUi.separator();
			DrawActionMenuItem(refContext, EditorActionIds::SelectionRename);
			DrawActionMenuItem(refContext, EditorActionIds::SelectionReparent);
			DrawActionMenuItem(refContext, EditorActionIds::SelectionDelete);
			refContext.refUi.end_menu();
		}

		if (refContext.refUi.begin_menu("Window"))
		{
			DrawActionMenuItem(refContext, EditorActionIds::WindowResetLayout);
			DrawActionMenuItem(refContext, EditorActionIds::WindowCommandPalette);
			refContext.refUi.separator();
			DrawThemeMenu(refContext);
			refContext.refUi.separator();

			for (const std::unique_ptr<EditorPanel>& refPanelOwner : refContext.refPanels)
			{
				EditorPanel* pPanel = refPanelOwner.get();
				if (!pPanel)
				{
					continue;
				}

				bool bOpen = pPanel->IsOpen();
				refContext.refUi.menu_item(pPanel->GetTitle().c_str(), nullptr, &bOpen);
				if (bOpen != pPanel->IsOpen() && refContext.pPanelManager)
				{
					refContext.pPanelManager->SetPanelOpen(pPanel->GetId(), bOpen);
				}
			}

			refContext.refUi.end_menu();
		}

		if (refContext.refUi.begin_menu("Assets"))
		{
			DrawActionMenuItem(refContext, EditorActionIds::AssetsRefresh);
			refContext.refUi.end_menu();
		}

		refContext.refUi.end_main_menu_bar();
	}

	bool MainMenuController::DrawActionMenuItem(
		MainMenuControllerContext& refContext,
		const char* pActionId,
		const bool bEnabled) const
	{
		return DrawEditorActionMenuItem(
			refContext.refUi,
			refContext.refCommandService,
			pActionId,
			nullptr,
			"main_menu",
			refContext.pActionInvoker,
			bEnabled);
	}

	void MainMenuController::DrawThemeMenu(MainMenuControllerContext& refContext) const
	{
		if (!refContext.refUi.begin_menu("Theme"))
		{
			return;
		}

		const AshEngine::UIThemePreset currentPreset = refContext.refUi.get_theme_preset();
		const std::array<AshEngine::UIThemePreset, 2> arrPresets{
			AshEngine::UIThemePreset::SlateStudio,
			AshEngine::UIThemePreset::ClassicDark
		};
		for (const AshEngine::UIThemePreset preset : arrPresets)
		{
			const bool bSelected = preset == currentPreset;
			if (refContext.refUi.menu_item(GetEditorUiThemePresetLabel(preset), nullptr, bSelected, true))
			{
				if (refContext.pThemeApplier)
				{
					refContext.pThemeApplier->ApplyThemePreset(preset);
				}
			}
		}

		refContext.refUi.end_menu();
	}
}

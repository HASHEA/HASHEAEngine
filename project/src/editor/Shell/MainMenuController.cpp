#include "Shell/MainMenuController.h"

#include "Core/EditorIds.h"
#include "Core/EditorPanel.h"
#include "Function/Gui/UIContext.h"
#include "Services/CommandService.h"
#include "Services/EditorSessionStateService.h"
#include "Services/EditorSettingsService.h"
#include "Shell/PanelManager.h"
#include "Widgets/EditorActionWidgets.h"
#include "Widgets/EditorThemeColors.h"

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
			DrawActionMenuItem(refContext, EditorActionIds::FileOpenScene);
			DrawRecentScenesMenu(refContext);
			DrawActionMenuItem(refContext, EditorActionIds::FileReloadScene);
			DrawActionMenuItem(refContext, EditorActionIds::FileSaveScene);
			refContext.refUi.separator();
			refContext.refUi.text_colored(
				GetEditorMutedTextColor(refContext.refUi),
				"Current Scene: %s",
				refContext.refSessionState.GetActiveScene().strSceneName.c_str());
			refContext.refUi.end_menu();
		}

		if (refContext.refUi.begin_menu("Edit"))
		{
			DrawActionMenuItem(refContext, EditorActionIds::EditUndo);
			DrawActionMenuItem(refContext, EditorActionIds::EditRedo);
			refContext.refUi.separator();
			DrawActionMenuItem(refContext, EditorActionIds::EditCopy);
			DrawActionMenuItem(refContext, EditorActionIds::EditPaste);
			const EditorUndoHistoryChangedEvent& refUndoHistory = refContext.refSessionState.GetUndoHistory();
			if (refUndoHistory.bHasOpenTransaction)
			{
				refContext.refUi.separator();
				refContext.refUi.text_colored(
					GetEditorWarningTextColor(refContext.refUi),
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
			DrawActionMenuItem(refContext, EditorActionIds::SelectionDuplicate);
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

	void MainMenuController::DrawRecentScenesMenu(MainMenuControllerContext& refContext) const
	{
		if (!refContext.refUi.begin_menu("Recent Scenes", refContext.pSceneFileActionHandler != nullptr))
		{
			return;
		}

		const std::vector<std::filesystem::path> vecRecentScenePaths =
			refContext.refSettingsService.GetRecentScenePaths();
		if (vecRecentScenePaths.empty())
		{
			refContext.refUi.menu_item("No Recent Scenes", nullptr, false, false);
			refContext.refUi.end_menu();
			return;
		}

		for (const std::filesystem::path& pathScene : vecRecentScenePaths)
		{
			const std::string strDisplayName = pathScene.filename().string();
			const std::string strMenuLabel =
				(strDisplayName.empty() ? pathScene.generic_string() : strDisplayName) +
				"##" +
				pathScene.generic_string();
			if (refContext.refUi.menu_item(strMenuLabel.c_str(), nullptr, false, true))
			{
				refContext.pSceneFileActionHandler->OpenSceneFromPath(pathScene, "main_menu_recent_scene");
			}
		}

		refContext.refUi.end_menu();
	}

	void MainMenuController::DrawThemeMenu(MainMenuControllerContext& refContext) const
	{
		if (!refContext.refUi.begin_menu("Theme"))
		{
			return;
		}

		const std::string strCurrentThemeId = refContext.refUi.get_theme_id();
		const std::vector<AshEngine::UIThemeDescriptor> vecThemes = refContext.refUi.list_themes();
		if (vecThemes.empty())
		{
			refContext.refUi.menu_item("No theme files found", nullptr, false, false);
			refContext.refUi.end_menu();
			return;
		}

		for (const AshEngine::UIThemeDescriptor& refTheme : vecThemes)
		{
			const bool bSelected = refTheme.strId == strCurrentThemeId;
			if (refContext.refUi.menu_item(refTheme.strLabel.c_str(), nullptr, bSelected, true))
			{
				if (refContext.pThemeApplier)
				{
					refContext.pThemeApplier->ApplyTheme(refTheme.strId, refTheme.strLabel);
				}
			}
		}

		refContext.refUi.end_menu();
	}
}

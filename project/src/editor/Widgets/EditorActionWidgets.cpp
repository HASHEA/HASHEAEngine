#include "Widgets/EditorActionWidgets.h"

#include "Core/IActionInvoker.h"
#include "Services/CommandService.h"

#include "Function/Gui/UIContext.h"
#include "Widgets/EditorTooltipWidgets.h"

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UITooltipConfig kEditorActionTooltipConfig{
			{ 360.0f, 0.0f },
			{},
			{ 420.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};

		void DrawActionTooltip(AshEngine::UIContext& refUi, const EditorAction& refAction)
		{
			if (!refUi.is_item_hovered())
			{
				return;
			}

			refUi.begin_tooltip(kEditorActionTooltipConfig);
			DrawEditorTooltipTitle(refUi, refAction.strLabel, refAction.strId);
			if (BeginEditorTooltipTable(refUi, "EditorActionTooltipTable"))
			{
				if (!refAction.strShortcut.empty())
				{
					DrawEditorTooltipRow(refUi, "Shortcut", refAction.strShortcut);
				}
				else
				{
					DrawEditorTooltipRow(refUi, "Shortcut", "None");
				}
				refUi.end_table();
			}
			if (!refAction.strDescription.empty())
			{
				DrawEditorTooltipDescription(refUi, refAction.strDescription);
			}
			refUi.end_tooltip();
		}

		bool InvokeEditorAction(
			const CommandService& refCommandService,
			const char* pActionId,
			const char* pSource,
			IActionInvoker* pActionInvoker)
		{
			if (!pActionId)
			{
				return false;
			}

			if (pActionInvoker)
			{
				return pActionInvoker->InvokeAction(pActionId, pSource ? pSource : "unknown");
			}

			return refCommandService.Invoke(pActionId, pSource ? pSource : "unknown");
		}

		bool DrawEditorActionMenuItemInternal(
			AshEngine::UIContext& refUi,
			const CommandService& refCommandService,
			const char* pActionId,
			const char* pLabelOverride,
			const char* pSource,
			IActionInvoker* pActionInvoker,
			const bool bEnabled)
		{
			if (!pActionId)
			{
				return false;
			}

			const EditorAction* pAction = refCommandService.FindAction(pActionId);
			if (!pAction)
			{
				return false;
			}

			const bool bActionEnabled = bEnabled && refCommandService.CanExecute(pAction->strId);
			const char* pShortcut = pAction->strShortcut.empty() ? nullptr : pAction->strShortcut.c_str();
			const char* pLabel = (pLabelOverride && pLabelOverride[0] != '\0')
				? pLabelOverride
				: pAction->strLabel.c_str();

			if (!refUi.menu_item(pLabel, pShortcut, false, bActionEnabled))
			{
				DrawActionTooltip(refUi, *pAction);
				return false;
			}

			DrawActionTooltip(refUi, *pAction);
			return InvokeEditorAction(refCommandService, pActionId, pSource, pActionInvoker);
		}

		bool DrawEditorActionButtonInternal(
			AshEngine::UIContext& refUi,
			const CommandService& refCommandService,
			const char* pActionId,
			const char* pLabelOverride,
			const char* pSource,
			IActionInvoker* pActionInvoker,
			bool bEnabled,
			const bool bSmallButton)
		{
			if (!pActionId)
			{
				return false;
			}

			const EditorAction* pAction = refCommandService.FindAction(pActionId);
			if (!pAction)
			{
				return false;
			}

			const bool bActionEnabled = bEnabled && refCommandService.CanExecute(pAction->strId);
			const char* pLabel = (pLabelOverride && pLabelOverride[0] != '\0')
				? pLabelOverride
				: pAction->strLabel.c_str();

			refUi.push_id(pActionId);
			refUi.begin_disabled(!bActionEnabled);
			const bool bClicked = bSmallButton ? refUi.small_button(pLabel) : refUi.button(pLabel);
			refUi.end_disabled();
			DrawActionTooltip(refUi, *pAction);
			refUi.pop_id();

			if (!bClicked)
			{
				return false;
			}

			return InvokeEditorAction(refCommandService, pActionId, pSource, pActionInvoker);
		}
	}

	bool DrawEditorActionMenuItem(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pSource,
		const bool bEnabled)
	{
		return DrawEditorActionMenuItem(
			refUi,
			refCommandService,
			pActionId,
			nullptr,
			pSource,
			nullptr,
			bEnabled);
	}

	bool DrawEditorActionMenuItem(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		const bool bEnabled)
	{
		return DrawEditorActionMenuItem(
			refUi,
			refCommandService,
			pActionId,
			pLabelOverride,
			pSource,
			nullptr,
			bEnabled);
	}

	bool DrawEditorActionMenuItem(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		IActionInvoker* pActionInvoker,
		const bool bEnabled)
	{
		return DrawEditorActionMenuItemInternal(
			refUi,
			refCommandService,
			pActionId,
			pLabelOverride,
			pSource,
			pActionInvoker,
			bEnabled);
	}

	bool DrawEditorActionButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		IActionInvoker* pActionInvoker,
		const bool bEnabled)
	{
		return DrawEditorActionButtonInternal(
			refUi,
			refCommandService,
			pActionId,
			pLabelOverride,
			pSource,
			pActionInvoker,
			bEnabled,
			false);
	}

	bool DrawEditorActionButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		const bool bEnabled)
	{
		return DrawEditorActionButton(
			refUi,
			refCommandService,
			pActionId,
			pLabelOverride,
			pSource,
			nullptr,
			bEnabled);
	}

	bool DrawEditorActionSmallButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		IActionInvoker* pActionInvoker,
		const bool bEnabled)
	{
		return DrawEditorActionButtonInternal(
			refUi,
			refCommandService,
			pActionId,
			pLabelOverride,
			pSource,
			pActionInvoker,
			bEnabled,
			true);
	}

	bool DrawEditorActionSmallButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		const bool bEnabled)
	{
		return DrawEditorActionSmallButton(
			refUi,
			refCommandService,
			pActionId,
			pLabelOverride,
			pSource,
			nullptr,
			bEnabled);
	}
}

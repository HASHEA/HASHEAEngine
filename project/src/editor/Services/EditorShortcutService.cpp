#include "Services/EditorShortcutService.h"

#include "Function/Gui/UIContext.h"
#include "Services/CommandService.h"

namespace AshEditor
{
	namespace
	{
		const char* GetShortcutSource(EditorActionScope eScope)
		{
			switch (eScope)
			{
			case EditorActionScope::AssetBrowserContent:
				return "shortcut.asset_browser_content";
			case EditorActionScope::Global:
			default:
				return "shortcut.global";
			}
		}
	}

	bool EditorShortcutService::DispatchScope(
		const CommandService& refCommandService,
		EditorActionScope eScope,
		const AshEngine::UIContext& refUiContext) const
	{
		// Shortcut dispatch stays centralized here so every scope shares the same text-input gating and invoke source semantics.
		if (!refUiContext.is_frame_active())
		{
			return false;
		}

		const bool bIsTextInputActive = refUiContext.wants_text_input();
		const char* pInvocationSource = GetShortcutSource(eScope);
		bool bInvokedAny = false;
		for (const EditorAction* pAction : refCommandService.CollectActionsWithScope(eScope))
		{
			if (!pAction)
			{
				continue;
			}

			for (const EditorShortcutBinding& refBinding : pAction->vecShortcutBindings)
			{
				if (refBinding.uChord == 0u)
				{
					continue;
				}
				if (bIsTextInputActive && !refBinding.bAllowWhenTextInput)
				{
					continue;
				}
				if (!refUiContext.is_key_chord_pressed(refBinding.uChord))
				{
					continue;
				}

				bInvokedAny = refCommandService.Invoke(pAction->strId, pInvocationSource) || bInvokedAny;
				break;
			}
		}

		return bInvokedAny;
	}
}

#include "Shell/EditorStatusBarController.h"

#include "Function/Gui/UIContext.h"
#include "Services/EditorSessionStateService.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kStatusBarWindowName = "Editor Status Bar";
		constexpr AshEngine::UIColor kStatusMutedTextColor{ 0.63f, 0.67f, 0.73f, 1.0f };
		constexpr AshEngine::UIColor kStatusAccentTextColor{ 0.77f, 0.82f, 0.90f, 1.0f };
		constexpr AshEngine::UIColor kStatusSuccessTextColor{ 0.58f, 0.82f, 0.66f, 1.0f };
		constexpr AshEngine::UIColor kStatusWarningTextColor{ 0.93f, 0.78f, 0.45f, 1.0f };
		constexpr AshEngine::UIColor kStatusErrorTextColor{ 0.92f, 0.54f, 0.54f, 1.0f };

		void DrawInlineSeparator(AshEngine::UIContext& refUi)
		{
			refUi.same_line(0.0f, 8.0f);
			refUi.text_colored(kStatusMutedTextColor, "|");
		}

		const char* GetActionStatusLabel(const EditorActionInvokedEvent& refEvent)
		{
			if (refEvent.strActionId.empty())
			{
				return "Ready";
			}
			if (refEvent.bExecuted)
			{
				return "Executed";
			}
			if (!refEvent.bRegistered)
			{
				return "Missing";
			}
			if (!refEvent.bEnabled)
			{
				return "Blocked";
			}
			return "Skipped";
		}

		AshEngine::UIColor GetActionStatusColor(const EditorActionInvokedEvent& refEvent)
		{
			if (refEvent.strActionId.empty())
			{
				return kStatusMutedTextColor;
			}
			if (refEvent.bExecuted)
			{
				return kStatusSuccessTextColor;
			}
			if (!refEvent.bRegistered)
			{
				return kStatusErrorTextColor;
			}
			if (!refEvent.bEnabled)
			{
				return kStatusWarningTextColor;
			}
			return kStatusMutedTextColor;
		}

		std::string MakeActionDisplayName(const EditorActionInvokedEvent& refEvent)
		{
			if (!refEvent.strActionLabel.empty())
			{
				return refEvent.strActionLabel;
			}
			if (!refEvent.strActionId.empty())
			{
				return refEvent.strActionId;
			}
			return "Ready";
		}

		std::string MakeActionSourceLabel(const std::string_view svSource)
		{
			if (svSource == "main_menu")
			{
				return "Menu";
			}
			if (svSource == "shortcut.global")
			{
				return "Global Shortcut";
			}
			if (svSource == "shortcut.asset_browser_content")
			{
				return "Asset Shortcut";
			}
			if (svSource.empty() || svSource == "unknown")
			{
				return "Unknown";
			}
			return std::string(svSource);
		}

		const char* GetSelectionKindLabel(const EditorSelection& refSelection)
		{
			switch (refSelection.eKind)
			{
			case EditorSelectionKind::Entity:
				return "Entity";
			case EditorSelectionKind::Asset:
				return "Asset";
			case EditorSelectionKind::None:
			default:
				return "None";
			}
		}

		std::string MakeSelectionDisplayName(const EditorSelection& refSelection)
		{
			if (!refSelection.strLabel.empty())
			{
				return refSelection.strLabel;
			}
			if (!refSelection.strPath.empty())
			{
				return refSelection.strPath;
			}
			return refSelection.IsEmpty() ? "None" : "<Unnamed>";
		}

		const char* GetShortcutScopeLabel(const EditorShortcutScopeChangedEvent& refEvent)
		{
			switch (refEvent.eScope)
			{
			case EditorShortcutScope::AssetBrowserContent:
				return "Asset Browser";
			case EditorShortcutScope::Global:
			default:
				return "Global";
			}
		}

		const char* MakeDocumentOperationName(const EditorDocumentOperationEvent& refEvent)
		{
			switch (refEvent.eKind)
			{
			case EditorDocumentOperationKind::NewScene:
				return "New Scene";
			case EditorDocumentOperationKind::LoadScene:
				return "Load Scene";
			case EditorDocumentOperationKind::ReloadScene:
				return "Reload Scene";
			case EditorDocumentOperationKind::SaveScene:
				return "Save Scene";
			case EditorDocumentOperationKind::None:
			default:
				return "Document";
			}
		}

		const char* GetDocumentOperationResultLabel(const EditorDocumentOperationEvent& refEvent)
		{
			switch (refEvent.eResult)
			{
			case EditorDocumentOperationResult::Succeeded:
				return "Succeeded";
			case EditorDocumentOperationResult::Failed:
				return "Failed";
			case EditorDocumentOperationResult::Skipped:
				return "Skipped";
			case EditorDocumentOperationResult::FallbackActivated:
				return "Fallback";
			case EditorDocumentOperationResult::None:
			default:
				return "Idle";
			}
		}

		AshEngine::UIColor GetDocumentOperationResultColor(const EditorDocumentOperationEvent& refEvent)
		{
			switch (refEvent.eResult)
			{
			case EditorDocumentOperationResult::Succeeded:
				return kStatusSuccessTextColor;
			case EditorDocumentOperationResult::Failed:
				return kStatusErrorTextColor;
			case EditorDocumentOperationResult::Skipped:
			case EditorDocumentOperationResult::FallbackActivated:
				return kStatusWarningTextColor;
			case EditorDocumentOperationResult::None:
			default:
				return kStatusMutedTextColor;
			}
		}

		std::string MakeDocumentTargetLabel(const EditorDocumentOperationEvent& refEvent)
		{
			if (!refEvent.strDocumentName.empty())
			{
				return refEvent.strDocumentName;
			}
			if (!refEvent.strDocumentPath.empty())
			{
				return refEvent.strDocumentPath;
			}
			return "<Untitled Scene>";
		}
	}

	float EditorStatusBarController::GetPreferredHeight(const AshEngine::UIContext& refUi) const
	{
		const AshEngine::UIVec2 vecFramePadding = refUi.get_style_frame_padding();
		return std::max(24.0f, refUi.get_font_size() + vecFramePadding.y * 2.0f + 6.0f);
	}

	void EditorStatusBarController::Draw(EditorStatusBarContext& refContext) const
	{
		AshEngine::UIContext& refUi = refContext.refUi;
		if (!refUi.is_frame_active())
		{
			return;
		}

		const AshEngine::UIRect rectMainViewport = refUi.get_main_viewport_rect();
		const float fStatusBarHeight = GetPreferredHeight(refUi);
		refUi.set_next_window_viewport(refUi.get_main_viewport_id());
		refUi.set_next_window_position(
			{ rectMainViewport.x, rectMainViewport.y + rectMainViewport.height - fStatusBarHeight },
			AshEngine::UIConditionFlagBits::Always);
		refUi.set_next_window_size(
			{ rectMainViewport.width, fStatusBarHeight },
			AshEngine::UIConditionFlagBits::Always);
		refUi.push_style_var(AshEngine::UIStyleVarKind::WindowPadding, { 10.0f, 4.0f });
		refUi.push_style_var(AshEngine::UIStyleVarKind::WindowRounding, 0.0f);
		refUi.push_style_var(AshEngine::UIStyleVarKind::WindowBorderSize, 1.0f);
		const bool bVisible = refUi.begin_window(
			kStatusBarWindowName,
			nullptr,
			AshEngine::UIWindowFlagBits::NoTitleBar |
			AshEngine::UIWindowFlagBits::NoResize |
			AshEngine::UIWindowFlagBits::NoMove |
			AshEngine::UIWindowFlagBits::NoScrollbar |
			AshEngine::UIWindowFlagBits::NoScrollWithMouse |
			AshEngine::UIWindowFlagBits::NoCollapse |
			AshEngine::UIWindowFlagBits::NoSavedSettings |
			AshEngine::UIWindowFlagBits::NoDocking |
			AshEngine::UIWindowFlagBits::NoBringToFrontOnFocus |
			AshEngine::UIWindowFlagBits::NoNavFocus);
		refUi.pop_style_var(3);

		if (!bVisible)
		{
			refUi.end_window();
			return;
		}

		const EditorActiveSceneChangedEvent& refActiveScene = refContext.refSessionState.GetActiveScene();
		const EditorSelection& refSelection = refContext.refSessionState.GetSelection();
		const EditorActiveDocumentDirtyStateChangedEvent& refDirtyState = refContext.refSessionState.GetActiveDocumentDirtyState();
		const EditorDocumentOperationEvent& refDocumentOperation = refContext.refSessionState.GetLastDocumentOperation();
		const EditorShortcutScopeChangedEvent& refShortcutScope = refContext.refSessionState.GetShortcutScope();
		const EditorTransactionStateChangedEvent& refTransactionState = refContext.refSessionState.GetTransactionState();
		const EditorActionInvokedEvent& refLastAction = refContext.refSessionState.GetLastActionInvocation();

		const std::string strSceneName = refActiveScene.strSceneName.empty() ? "<Untitled Scene>" : refActiveScene.strSceneName;
		const std::string strSelectionName = MakeSelectionDisplayName(refSelection);
		const std::string strActionName = MakeActionDisplayName(refLastAction);
		const std::string strActionSource = MakeActionSourceLabel(refLastAction.strSource);

		refUi.text_colored(kStatusMutedTextColor, "Scene");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(kStatusAccentTextColor, "%s", strSceneName.c_str());
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(
			refDirtyState.bDirty ? kStatusWarningTextColor : kStatusSuccessTextColor,
			"%s",
			refDirtyState.bDirty ? "Modified" : "Saved");

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(kStatusMutedTextColor, "Selection");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(refSelection.IsEmpty() ? kStatusMutedTextColor : kStatusAccentTextColor, "%s", strSelectionName.c_str());
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(kStatusMutedTextColor, "(%s)", GetSelectionKindLabel(refSelection));

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(kStatusMutedTextColor, "Scope");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(
			refShortcutScope.eScope == EditorShortcutScope::Global ? kStatusMutedTextColor : kStatusAccentTextColor,
			"%s",
			GetShortcutScopeLabel(refShortcutScope));

		if (refDocumentOperation.eKind != EditorDocumentOperationKind::None)
		{
			const std::string strDocumentTarget = MakeDocumentTargetLabel(refDocumentOperation);
			DrawInlineSeparator(refUi);
			refUi.same_line(0.0f, 8.0f);
			refUi.text_colored(kStatusMutedTextColor, "%s", MakeDocumentOperationName(refDocumentOperation));
			refUi.same_line(0.0f, 6.0f);
			refUi.text_colored(
				GetDocumentOperationResultColor(refDocumentOperation),
				"%s",
				GetDocumentOperationResultLabel(refDocumentOperation));
			refUi.same_line(0.0f, 6.0f);
			refUi.text_colored(kStatusAccentTextColor, "%s", strDocumentTarget.c_str());
		}

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(kStatusMutedTextColor, "Transaction");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(
			refTransactionState.bHasOpenTransaction ? kStatusWarningTextColor : kStatusMutedTextColor,
			"%s",
			refTransactionState.bHasOpenTransaction
				? (refTransactionState.strLabel.empty() ? "<Unnamed>" : refTransactionState.strLabel.c_str())
				: "Idle");

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(kStatusMutedTextColor, "Last Action");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(GetActionStatusColor(refLastAction), "%s", GetActionStatusLabel(refLastAction));
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(kStatusAccentTextColor, "%s", strActionName.c_str());
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(kStatusMutedTextColor, "(%s)", strActionSource.c_str());

		refUi.end_window();
	}
}

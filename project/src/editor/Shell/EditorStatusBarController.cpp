#include "Shell/EditorStatusBarController.h"

#include "Function/Gui/UIContext.h"
#include "Services/EditorSessionStateService.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kStatusBarWindowName = "Editor Status Bar";

		void DrawInlineSeparator(AshEngine::UIContext& refUi)
		{
			refUi.same_line(0.0f, 8.0f);
			refUi.text_colored(GetEditorMutedTextColor(refUi), "|");
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

		AshEngine::UIColor GetActionStatusColor(AshEngine::UIContext& refUi, const EditorActionInvokedEvent& refEvent)
		{
			if (refEvent.strActionId.empty())
			{
				return GetEditorMutedTextColor(refUi);
			}
			if (refEvent.bExecuted)
			{
				return GetEditorSuccessTextColor(refUi);
			}
			if (!refEvent.bRegistered)
			{
				return GetEditorErrorTextColor(refUi);
			}
			if (!refEvent.bEnabled)
			{
				return GetEditorWarningTextColor(refUi);
			}
			return GetEditorMutedTextColor(refUi);
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

		const char* GetSelectionKindLabel(EditorSelectionKind eKind)
		{
			switch (eKind)
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

		std::string MakeSelectionKindSummary(
			const EditorSelection& refSelection,
			const std::vector<EditorSelection>& vecSelections)
		{
			if (vecSelections.size() <= 1)
			{
				return GetSelectionKindLabel(refSelection.eKind);
			}

			size_t uEntityCount = 0;
			size_t uAssetCount = 0;
			for (const EditorSelection& refCurrentSelection : vecSelections)
			{
				if (refCurrentSelection.eKind == EditorSelectionKind::Entity)
				{
					++uEntityCount;
				}
				else if (refCurrentSelection.eKind == EditorSelectionKind::Asset)
				{
					++uAssetCount;
				}
			}

			if (uEntityCount == vecSelections.size())
			{
				return "Entities";
			}
			if (uAssetCount == vecSelections.size())
			{
				return "Assets";
			}
			return "Mixed";
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

		std::string MakeSelectionDisplayName(
			const EditorSelection& refSelection,
			const std::vector<EditorSelection>& vecSelections)
		{
			if (vecSelections.size() <= 1)
			{
				return MakeSelectionDisplayName(refSelection);
			}
			return std::to_string(vecSelections.size()) + " selected";
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

		AshEngine::UIColor GetDocumentOperationResultColor(AshEngine::UIContext& refUi, const EditorDocumentOperationEvent& refEvent)
		{
			switch (refEvent.eResult)
			{
			case EditorDocumentOperationResult::Succeeded:
				return GetEditorSuccessTextColor(refUi);
			case EditorDocumentOperationResult::Failed:
				return GetEditorErrorTextColor(refUi);
			case EditorDocumentOperationResult::Skipped:
			case EditorDocumentOperationResult::FallbackActivated:
				return GetEditorWarningTextColor(refUi);
			case EditorDocumentOperationResult::None:
			default:
				return GetEditorMutedTextColor(refUi);
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
		const std::vector<EditorSelection>& vecSelections = refContext.refSessionState.GetSelections();
		const EditorActiveDocumentDirtyStateChangedEvent& refDirtyState = refContext.refSessionState.GetActiveDocumentDirtyState();
		const EditorDocumentOperationEvent& refDocumentOperation = refContext.refSessionState.GetLastDocumentOperation();
		const EditorShortcutScopeChangedEvent& refShortcutScope = refContext.refSessionState.GetShortcutScope();
		const EditorTransactionStateChangedEvent& refTransactionState = refContext.refSessionState.GetTransactionState();
		const EditorActionInvokedEvent& refLastAction = refContext.refSessionState.GetLastActionInvocation();

		const std::string strSceneName = refActiveScene.strSceneName.empty() ? "<Untitled Scene>" : refActiveScene.strSceneName;
		const std::string strSelectionName = MakeSelectionDisplayName(refSelection, vecSelections);
		const std::string strSelectionKind = MakeSelectionKindSummary(refSelection, vecSelections);
		const std::string strActionName = MakeActionDisplayName(refLastAction);
		const std::string strActionSource = MakeActionSourceLabel(refLastAction.strSource);

		refUi.text_colored(GetEditorMutedTextColor(refUi), "Scene");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(GetEditorAccentTextColor(refUi), "%s", strSceneName.c_str());
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(
			refDirtyState.bDirty ? GetEditorWarningTextColor(refUi) : GetEditorSuccessTextColor(refUi),
			"%s",
			refDirtyState.bDirty ? "Modified" : "Saved");

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(GetEditorMutedTextColor(refUi), "Selection");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(
			refSelection.IsEmpty() ? GetEditorMutedTextColor(refUi) : GetEditorAccentTextColor(refUi),
			"%s",
			strSelectionName.c_str());
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(GetEditorMutedTextColor(refUi), "(%s)", strSelectionKind.c_str());

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(GetEditorMutedTextColor(refUi), "Scope");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(
			refShortcutScope.eScope == EditorShortcutScope::Global ? GetEditorMutedTextColor(refUi) : GetEditorAccentTextColor(refUi),
			"%s",
			GetShortcutScopeLabel(refShortcutScope));

		if (refDocumentOperation.eKind != EditorDocumentOperationKind::None)
		{
			const std::string strDocumentTarget = MakeDocumentTargetLabel(refDocumentOperation);
			DrawInlineSeparator(refUi);
			refUi.same_line(0.0f, 8.0f);
			refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", MakeDocumentOperationName(refDocumentOperation));
			refUi.same_line(0.0f, 6.0f);
			refUi.text_colored(
				GetDocumentOperationResultColor(refUi, refDocumentOperation),
				"%s",
				GetDocumentOperationResultLabel(refDocumentOperation));
			refUi.same_line(0.0f, 6.0f);
			refUi.text_colored(GetEditorAccentTextColor(refUi), "%s", strDocumentTarget.c_str());
		}

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(GetEditorMutedTextColor(refUi), "Transaction");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(
			refTransactionState.bHasOpenTransaction ? GetEditorWarningTextColor(refUi) : GetEditorMutedTextColor(refUi),
			"%s",
			refTransactionState.bHasOpenTransaction
				? (refTransactionState.strLabel.empty() ? "<Unnamed>" : refTransactionState.strLabel.c_str())
				: "Idle");

		DrawInlineSeparator(refUi);
		refUi.same_line(0.0f, 8.0f);
		refUi.text_colored(GetEditorMutedTextColor(refUi), "Last Action");
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(GetActionStatusColor(refUi, refLastAction), "%s", GetActionStatusLabel(refLastAction));
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(GetEditorAccentTextColor(refUi), "%s", strActionName.c_str());
		refUi.same_line(0.0f, 6.0f);
		refUi.text_colored(GetEditorMutedTextColor(refUi), "(%s)", strActionSource.c_str());

		refUi.end_window();
	}
}

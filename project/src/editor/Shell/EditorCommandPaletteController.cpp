#include "Shell/EditorCommandPaletteController.h"

#include "Base/hlog.h"
#include "Function/Gui/UICommon.h"
#include "Function/Gui/UIContext.h"
#include "Services/CommandService.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kCommandPalettePopupId = "Command Palette";
		constexpr float kCommandPaletteWidth = 680.0f;
		constexpr float kCommandPaletteHeight = 420.0f;
		constexpr float kCommandPaletteListHeight = 260.0f;

		char ToLowerAscii(char c)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		bool ContainsCaseInsensitive(std::string_view svHaystack, std::string_view svNeedle)
		{
			if (svNeedle.empty())
			{
				return true;
			}

			const size_t uNeedleLength = svNeedle.size();
			if (uNeedleLength > svHaystack.size())
			{
				return false;
			}

			for (size_t uStart = 0; uStart + uNeedleLength <= svHaystack.size(); ++uStart)
			{
				bool bMatch = true;
				for (size_t uOffset = 0; uOffset < uNeedleLength; ++uOffset)
				{
					if (ToLowerAscii(svHaystack[uStart + uOffset]) != ToLowerAscii(svNeedle[uOffset]))
					{
						bMatch = false;
						break;
					}
				}
				if (bMatch)
				{
					return true;
				}
			}
			return false;
		}

		bool MatchesFilter(const EditorAction& refAction, std::string_view svFilter)
		{
			if (svFilter.empty())
			{
				return true;
			}

			return
				ContainsCaseInsensitive(refAction.strLabel, svFilter) ||
				ContainsCaseInsensitive(refAction.strId, svFilter) ||
				ContainsCaseInsensitive(refAction.strDescription, svFilter) ||
				ContainsCaseInsensitive(refAction.strShortcut, svFilter);
		}
	}

	void EditorCommandPaletteController::RequestOpen()
	{
		_bOpenRequested = true;
		_strFilter.clear();
		_iSelectedIndex = 0;
	}

	void EditorCommandPaletteController::Draw(EditorCommandPaletteContext& refContext)
	{
		AshEngine::UIContext& refUi = refContext.refUi;

		if (_bOpenRequested)
		{
			refUi.open_popup(kCommandPalettePopupId);
			refUi.set_next_window_size(
				{ kCommandPaletteWidth, kCommandPaletteHeight },
				AshEngine::UIConditionFlagBits::Always);
			_bOpenRequested = false;
		}

		if (!refUi.begin_popup_modal(kCommandPalettePopupId))
		{
			return;
		}

		if (refUi.is_key_chord_pressed(AshEngine::make_key_chord(AshEngine::UIKey::Escape)))
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}

		refUi.text_unformatted("Search commands:");
		const bool bFilterEdited = refUi.input_text("##command_palette_filter", _strFilter);
		if (bFilterEdited)
		{
			_iSelectedIndex = 0;
		}

		struct CommandPaletteEntry
		{
			const EditorAction* pAction = nullptr;
			std::string_view svCategory{};
		};

		std::vector<CommandPaletteEntry> vecVisibleActions{};
		{
			const std::vector<const EditorAction*> vecAllActions = refContext.refCommandService.CollectActionsForCommandPalette();
			vecVisibleActions.reserve(vecAllActions.size());
			for (const EditorAction* pAction : vecAllActions)
			{
				if (!pAction)
				{
					continue;
				}
				if (MatchesFilter(*pAction, _strFilter))
				{
					vecVisibleActions.push_back(CommandPaletteEntry{
						pAction,
						CommandService::GetActionCategory(pAction->strId)
					});
				}
			}
		}

		std::sort(
			vecVisibleActions.begin(),
			vecVisibleActions.end(),
			[](const CommandPaletteEntry& refLhs, const CommandPaletteEntry& refRhs)
			{
				if (!refLhs.pAction || !refRhs.pAction)
				{
					return refLhs.pAction != nullptr && refRhs.pAction == nullptr;
				}

				if (refLhs.svCategory != refRhs.svCategory)
				{
					return refLhs.svCategory < refRhs.svCategory;
				}

				if (refLhs.pAction->strLabel != refRhs.pAction->strLabel)
				{
					return refLhs.pAction->strLabel < refRhs.pAction->strLabel;
				}

				return refLhs.pAction->strId < refRhs.pAction->strId;
			});

		if (_iSelectedIndex < 0)
		{
			_iSelectedIndex = 0;
		}
		if (!vecVisibleActions.empty() && _iSelectedIndex >= static_cast<int32_t>(vecVisibleActions.size()))
		{
			_iSelectedIndex = static_cast<int32_t>(vecVisibleActions.size()) - 1;
		}

		if (refUi.is_key_chord_pressed(AshEngine::make_key_chord(AshEngine::UIKey::UpArrow)))
		{
			_iSelectedIndex = std::max<int32_t>(0, _iSelectedIndex - 1);
		}
		if (refUi.is_key_chord_pressed(AshEngine::make_key_chord(AshEngine::UIKey::DownArrow)))
		{
			_iSelectedIndex = std::min<int32_t>(
				vecVisibleActions.empty() ? 0 : static_cast<int32_t>(vecVisibleActions.size()) - 1,
				_iSelectedIndex + 1);
		}

		refUi.separator();

		if (!refUi.begin_child("CommandPaletteList", { 0.0f, kCommandPaletteListHeight }, AshEngine::UIChildFlagBits::Border))
		{
			refUi.end_child();
			refUi.end_popup();
			return;
		}

		const bool bPressedEnter = refUi.is_key_chord_pressed(AshEngine::make_key_chord(AshEngine::UIKey::Enter));

		const EditorAction* pPendingExecuteAction = nullptr;
		for (int32_t iActionIndex = 0; iActionIndex < static_cast<int32_t>(vecVisibleActions.size()); ++iActionIndex)
		{
			const EditorAction* pAction = vecVisibleActions[static_cast<size_t>(iActionIndex)].pAction;
			if (!pAction)
			{
				continue;
			}

			const bool bSelected = iActionIndex == _iSelectedIndex;
			const bool bEnabled = refContext.refCommandService.CanExecute(pAction->strId);

			refUi.push_id(pAction->strId.c_str());
			refUi.begin_disabled(!bEnabled);
			const std::string strLabel = pAction->strShortcut.empty()
				? pAction->strLabel
				: pAction->strLabel + "  [" + pAction->strShortcut + "]";
			const bool bActivated = refUi.selectable(
				strLabel.c_str(),
				bSelected,
				AshEngine::UISelectableFlagBits::None);
			refUi.end_disabled();
			refUi.pop_id();

			if (bActivated)
			{
				_iSelectedIndex = iActionIndex;
				if (refUi.is_mouse_double_clicked(AshEngine::UIMouseButton::Left) && bEnabled)
				{
					pPendingExecuteAction = pAction;
				}
			}

			if (bSelected && bPressedEnter && bEnabled)
			{
				pPendingExecuteAction = pAction;
			}
		}

		refUi.end_child();

		refUi.separator();

		const EditorAction* pSelectedAction = nullptr;
		std::string_view svSelectedCategory{};
		if (!vecVisibleActions.empty() &&
			_iSelectedIndex >= 0 &&
			_iSelectedIndex < static_cast<int32_t>(vecVisibleActions.size()) &&
			vecVisibleActions[static_cast<size_t>(_iSelectedIndex)].pAction)
		{
			pSelectedAction = vecVisibleActions[static_cast<size_t>(_iSelectedIndex)].pAction;
			svSelectedCategory = vecVisibleActions[static_cast<size_t>(_iSelectedIndex)].svCategory;
		}
		if (pSelectedAction)
		{
			refUi.text("Action: %s", pSelectedAction->strLabel.empty() ? pSelectedAction->strId.c_str() : pSelectedAction->strLabel.c_str());
			if (!svSelectedCategory.empty())
			{
				refUi.text("Category: %.*s", static_cast<int>(svSelectedCategory.size()), svSelectedCategory.data());
			}
			refUi.text("Id: %s", pSelectedAction->strId.c_str());
			if (!pSelectedAction->strDescription.empty())
			{
				refUi.text_wrapped("%s", pSelectedAction->strDescription.c_str());
			}
		}
		else if (!_strFilter.empty())
		{
			refUi.text_unformatted("No matching actions.");
		}
		else
		{
			refUi.text_unformatted("No actions registered.");
		}

		bool bExecuteClicked = false;
		refUi.begin_disabled(!pSelectedAction || !refContext.refCommandService.CanExecute(pSelectedAction->strId));
		if (refUi.button("Execute"))
		{
			bExecuteClicked = true;
		}
		refUi.end_disabled();
		refUi.same_line();
		if (refUi.button("Close"))
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}

		if (bExecuteClicked && pSelectedAction)
		{
			pPendingExecuteAction = pSelectedAction;
		}

		if (pPendingExecuteAction)
		{
			const bool bExecuted = refContext.refCommandService.Invoke(pPendingExecuteAction->strId, "command_palette");
			if (!bExecuted)
			{
				HLogWarning("Command palette action '{}' did not execute.", pPendingExecuteAction->strId);
			}
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}

		refUi.end_popup();
	}
}

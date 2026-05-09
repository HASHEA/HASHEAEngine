#include "Services/CommandService.h"

#include "Core/IEditorActionHandler.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include <string_view>
#include <utility>

namespace AshEditor
{
	namespace
	{
		std::string BuildShortcutText(const std::vector<EditorShortcutBinding>& refBindings)
		{
			std::string strResult{};
			for (const EditorShortcutBinding& refBinding : refBindings)
			{
				if (refBinding.strDisplayText.empty())
				{
					continue;
				}

				if (!strResult.empty())
				{
					strResult += " / ";
				}
				strResult += refBinding.strDisplayText;
			}
			return strResult;
		}
	}

	void CommandService::SetEventBus(EditorEventBus* pEventBus)
	{
		_pEventBus = pEventBus;
	}

	void CommandService::RegisterAction(EditorAction action)
	{
		if (action.strShortcut.empty())
		{
			action.strShortcut = BuildShortcutText(action.vecShortcutBindings);
		}

		for (EditorAction& refExistingAction : _vecActions)
		{
			if (refExistingAction.strId == action.strId)
			{
				refExistingAction = std::move(action);
				return;
			}
		}

		_vecActions.push_back(std::move(action));
	}

	void CommandService::RegisterAction(
		std::string strId,
		std::string strLabel,
		std::string strShortcut,
		std::function<void()> callback)
	{
		RegisterAction(EditorAction{
			std::move(strId),
			std::move(strLabel),
			{},
			std::move(strShortcut),
			{},
			EditorActionScope::Global,
			{},
			std::move(callback),
			nullptr,
			true
		});
	}

	void CommandService::RegisterAction(std::string strId, std::string strLabel, std::function<void()> callback)
	{
		RegisterAction(std::move(strId), std::move(strLabel), {}, std::move(callback));
	}

	bool CommandService::Invoke(const std::string& strId, std::string_view svSource) const
	{
		const EditorAction* pAction = FindAction(strId);
		bool bEnabled = false;
		bool bExecuted = false;

		if (pAction)
		{
			if (pAction->pHandler)
			{
				bEnabled = pAction->pHandler->CanExecuteAction(strId);
				if (bEnabled)
				{
					pAction->pHandler->ExecuteAction(strId);
					bExecuted = true;
				}
			}
			else
			{
				bEnabled = !pAction->fnCanExecute || pAction->fnCanExecute();
				if (bEnabled && pAction->fnCallback)
				{
					pAction->fnCallback();
					bExecuted = true;
				}
			}
		}

		PublishActionInvoked(strId, pAction, svSource, bEnabled, bExecuted);
		return bExecuted;
	}

	bool CommandService::CanExecute(const std::string& strId) const
	{
		const EditorAction* pAction = FindAction(strId);
		if (!pAction)
		{
			return false;
		}

		if (pAction->pHandler)
		{
			return pAction->pHandler->CanExecuteAction(strId);
		}

		return !pAction->fnCanExecute || pAction->fnCanExecute();
	}

	bool CommandService::HasAction(const std::string& strId) const
	{
		return FindAction(strId) != nullptr;
	}

	const EditorAction* CommandService::FindAction(const std::string& strId) const
	{
		for (const EditorAction& refAction : _vecActions)
		{
			if (refAction.strId == strId)
			{
				return &refAction;
			}
		}
		return nullptr;
	}

	std::vector<const EditorAction*> CommandService::CollectActionsWithPrefix(std::string_view svPrefix) const
	{
		std::vector<const EditorAction*> vecActions{};
		for (const EditorAction& refAction : _vecActions)
		{
			if (refAction.strId.compare(0, svPrefix.size(), svPrefix.data(), svPrefix.size()) == 0)
			{
				vecActions.push_back(&refAction);
			}
		}
		return vecActions;
	}

	std::vector<const EditorAction*> CommandService::CollectActionsWithScope(EditorActionScope eScope) const
	{
		std::vector<const EditorAction*> vecActions{};
		for (const EditorAction& refAction : _vecActions)
		{
			if (refAction.eScope == eScope)
			{
				vecActions.push_back(&refAction);
			}
		}
		return vecActions;
	}

	std::vector<const EditorAction*> CommandService::CollectActionsForCommandPalette() const
	{
		std::vector<const EditorAction*> vecActions{};
		vecActions.reserve(_vecActions.size());
		for (const EditorAction& refAction : _vecActions)
		{
			if (refAction.bShowInCommandPalette)
			{
				vecActions.push_back(&refAction);
			}
		}
		return vecActions;
	}

	const std::vector<EditorAction>& CommandService::GetActions() const
	{
		return _vecActions;
	}

	std::string_view CommandService::GetActionCategory(std::string_view svId)
	{
		const size_t separator = svId.find('.');
		return separator == std::string_view::npos ? svId : svId.substr(0, separator);
	}

	void CommandService::PublishActionInvoked(
		std::string_view svActionId,
		const EditorAction* pAction,
		std::string_view svSource,
		bool bEnabled,
		bool bExecuted) const
	{
		if (!_pEventBus)
		{
			return;
		}

		EditorActionInvokedEvent event{};
		event.strActionId = std::string(svActionId);
		event.strActionLabel = pAction ? pAction->strLabel : std::string{};
		event.strSource = std::string(svSource);
		event.bRegistered = pAction != nullptr;
		event.bEnabled = bEnabled;
		event.bExecuted = bExecuted;
		_pEventBus->Publish(event);
	}
}

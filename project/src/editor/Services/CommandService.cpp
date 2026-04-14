#include "Services/CommandService.h"
#include <utility>

namespace AshEditor
{
	void CommandService::register_action(std::string id, std::string label, std::function<void()> callback)
	{
		for (EditorAction& action : m_actions)
		{
			if (action.id == id)
			{
				action.label = std::move(label);
				action.callback = std::move(callback);
				return;
			}
		}

		m_actions.push_back({ std::move(id), std::move(label), std::move(callback) });
	}

	bool CommandService::invoke(const std::string& id) const
	{
		const EditorAction* action = find_action(id);
		if (!action || !action->callback)
		{
			return false;
		}

		action->callback();
		return true;
	}

	const EditorAction* CommandService::find_action(const std::string& id) const
	{
		for (const EditorAction& action : m_actions)
		{
			if (action.id == id)
			{
				return &action;
			}
		}
		return nullptr;
	}

	const std::vector<EditorAction>& CommandService::get_actions() const
	{
		return m_actions;
	}
}

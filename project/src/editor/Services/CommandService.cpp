#include "Services/CommandService.h"
#include <string_view>
#include <utility>

namespace AshEditor
{
	void CommandService::register_action(std::string id, std::string label, std::string shortcut, std::function<void()> callback)
	{
		for (EditorAction& action : m_actions)
		{
			if (action.id == id)
			{
				action.label = std::move(label);
				action.shortcut = std::move(shortcut);
				action.callback = std::move(callback);
				return;
			}
		}

		m_actions.push_back({ std::move(id), std::move(label), std::move(shortcut), std::move(callback) });
	}

	void CommandService::register_action(std::string id, std::string label, std::function<void()> callback)
	{
		register_action(std::move(id), std::move(label), {}, std::move(callback));
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

	bool CommandService::has_action(const std::string& id) const
	{
		return find_action(id) != nullptr;
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

	std::vector<const EditorAction*> CommandService::collect_actions_with_prefix(std::string_view prefix) const
	{
		std::vector<const EditorAction*> actions{};
		for (const EditorAction& action : m_actions)
		{
			if (action.id.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0)
			{
				actions.push_back(&action);
			}
		}
		return actions;
	}

	const std::vector<EditorAction>& CommandService::get_actions() const
	{
		return m_actions;
	}

	std::string_view CommandService::get_action_category(std::string_view id)
	{
		const size_t separator = id.find('.');
		return separator == std::string_view::npos ? id : id.substr(0, separator);
	}
}

#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	struct EditorAction
	{
		std::string id{};
		std::string label{};
		std::string shortcut{};
		std::function<void()> callback{};
	};

	class CommandService
	{
	public:
		void register_action(std::string id, std::string label, std::string shortcut, std::function<void()> callback);
		void register_action(std::string id, std::string label, std::function<void()> callback);
		bool invoke(const std::string& id) const;
		bool has_action(const std::string& id) const;
		const EditorAction* find_action(const std::string& id) const;
		std::vector<const EditorAction*> collect_actions_with_prefix(std::string_view prefix) const;
		const std::vector<EditorAction>& get_actions() const;

		static std::string_view get_action_category(std::string_view id);

	private:
		std::vector<EditorAction> m_actions{};
	};
}

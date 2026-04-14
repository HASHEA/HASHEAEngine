#pragma once
#include <functional>
#include <string>
#include <vector>

namespace AshEditor
{
	struct EditorAction
	{
		std::string id{};
		std::string label{};
		std::function<void()> callback{};
	};

	class CommandService
	{
	public:
		void register_action(std::string id, std::string label, std::function<void()> callback);
		bool invoke(const std::string& id) const;
		const EditorAction* find_action(const std::string& id) const;
		const std::vector<EditorAction>& get_actions() const;

	private:
		std::vector<EditorAction> m_actions{};
	};
}

#pragma once
#include "Core/EditorPanel.h"
#include <string>
#include <vector>

namespace AshEditor
{
	class ConsolePanel final : public EditorPanel
	{
	public:
		ConsolePanel();

	public:
		void add_message(std::string message);
		const std::vector<std::string>& get_messages() const;

		void on_attach(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		std::vector<std::string> m_messages{};
	};
}

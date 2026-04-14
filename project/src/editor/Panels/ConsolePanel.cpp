#include "Panels/ConsolePanel.h"
#include "Base/hlog.h"
#include "imgui.h"
#include <utility>

namespace AshEditor
{
	ConsolePanel::ConsolePanel()
		: EditorPanel("console", "Console")
	{
	}

	void ConsolePanel::add_message(std::string message)
	{
		m_messages.emplace_back(std::move(message));
	}

	const std::vector<std::string>& ConsolePanel::get_messages() const
	{
		return m_messages;
	}

	void ConsolePanel::on_attach(EditorContext& context)
	{
		(void)context;
		HLogInfo("ConsolePanel attached.");
	}

	void ConsolePanel::on_gui(EditorContext& context)
	{
		(void)context;
		if (!begin_panel_window())
		{
			end_panel_window();
			return;
		}

		ImGui::Text("Messages: %u", static_cast<uint32_t>(m_messages.size()));
		ImGui::Separator();
		for (const std::string& message : m_messages)
		{
			ImGui::TextWrapped("%s", message.c_str());
		}

		end_panel_window();
	}
}

#include "Core/EditorPanel.h"
#include "imgui.h"
#include <utility>

namespace AshEditor
{
	EditorPanel::EditorPanel(std::string id, std::string title)
		: m_id(std::move(id))
		, m_title(std::move(title))
	{
	}

	const std::string& EditorPanel::get_id() const
	{
		return m_id;
	}

	const std::string& EditorPanel::get_title() const
	{
		return m_title;
	}

	bool EditorPanel::is_open() const
	{
		return m_open;
	}

	void EditorPanel::set_open(bool open)
	{
		m_open = open;
	}

	void EditorPanel::on_attach(EditorContext& context)
	{
		(void)context;
	}

	void EditorPanel::on_detach(EditorContext& context)
	{
		(void)context;
	}

	void EditorPanel::on_update(EditorContext& context)
	{
		(void)context;
	}

	void EditorPanel::on_gui(EditorContext& context)
	{
		(void)context;
	}

	bool EditorPanel::begin_panel_window()
	{
		bool open = m_open;
		const bool visible = ImGui::Begin(m_title.c_str(), &open);
		m_open = open;
		return visible;
	}

	void EditorPanel::end_panel_window()
	{
		ImGui::End();
	}
}

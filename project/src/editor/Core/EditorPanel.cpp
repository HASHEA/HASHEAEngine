#include "Core/EditorPanel.h"
#include "Function/Gui/UIContext.h"
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

	bool EditorPanel::begin_panel_window(EditorContext& context, AshEngine::UIWindowFlags flags)
	{
		m_windowActiveThisFrame = false;
		if (!context.ui_context || !context.ui_context->is_frame_active())
		{
			return false;
		}

		context.ui_context->set_next_window_force_dock_tab_bar(true);

		bool open = m_open;
		const bool visible = context.ui_context->begin_window(m_title.c_str(), &open, flags);
		m_open = open;
		m_windowActiveThisFrame = true;
		return visible;
	}

	void EditorPanel::end_panel_window(EditorContext& context)
	{
		if (m_windowActiveThisFrame && context.ui_context)
		{
			context.ui_context->end_window();
		}
		m_windowActiveThisFrame = false;
	}
}

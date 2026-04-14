#pragma once
#include "Core/EditorContext.h"
#include <string>

namespace AshEditor
{
	class EditorPanel
	{
	public:
		EditorPanel(std::string id, std::string title);
		virtual ~EditorPanel() = default;

	public:
		const std::string& get_id() const;
		const std::string& get_title() const;
		bool is_open() const;
		void set_open(bool open);

		virtual void on_attach(EditorContext& context);
		virtual void on_detach(EditorContext& context);
		virtual void on_update(EditorContext& context);
		virtual void on_gui(EditorContext& context);

	protected:
		bool begin_panel_window();
		void end_panel_window();

	private:
		std::string m_id{};
		std::string m_title{};
		bool m_open = true;
	};
}

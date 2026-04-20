#pragma once
#include "Core/EditorPanel.h"
#include <string>

namespace AshEditor
{
	class ViewportPanel final : public EditorPanel
	{
	public:
		ViewportPanel(std::string viewport_id = "scene", std::string panel_id = "viewport", std::string title = "Viewport");

	public:
		void on_attach(EditorContext& context) override;
		void on_update(EditorContext& context) override;
		void on_gui(EditorContext& context) override;

	private:
		void draw_toolbar(EditorContext& context, EditorViewportInstance& viewport);
		void draw_status(EditorContext& context, const EditorViewportInstance& viewport) const;
		EditorViewportInstance* resolve_viewport(EditorContext& context) const;
		const EditorViewportInstance* resolve_viewport(const EditorContext& context) const;

	private:
		std::string m_viewportId{};
	};
}

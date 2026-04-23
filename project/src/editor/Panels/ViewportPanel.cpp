#include "Panels/ViewportPanel.h"
#include "Base/hlog.h"
#include "Function/Gui/UIContext.h"
#include "Services/EditorViewportService.h"
#include "imgui.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
		const char* viewport_kind_label(EditorViewportKind kind)
		{
			switch (kind)
			{
			case EditorViewportKind::Scene:
				return "Scene";
			case EditorViewportKind::Game:
				return "Game";
			default:
				return "Aux";
			}
		}

		bool should_trace_viewport_panel()
		{
			static uint32_t s_logged_frames = 0;
			++s_logged_frames;
			return s_logged_frames <= 2;
		}

		auto make_overlay_lines(
			const EditorViewportInstance& viewport,
			const EditorViewportPresentation& presentation,
			const EditorViewportRenderState* render_state,
			bool is_primary) -> std::vector<std::string>
		{
			std::vector<std::string> lines{};
			std::string header = viewport_kind_label(presentation.kind);
			if (is_primary)
			{
				header += " | Primary";
			}
			if (presentation.accepts_input)
			{
				header += " | Input";
			}
			if (presentation.preserve_aspect)
			{
				header += " | Aspect";
			}
			lines.push_back(std::move(header));

			lines.push_back(
				"Output " +
				std::to_string(viewport.state.width) +
				"x" +
				std::to_string(viewport.state.height) +
				"  Req " +
				std::to_string(viewport.state.requested_width) +
				"x" +
				std::to_string(viewport.state.requested_height));

			if (presentation.show_stats)
			{
				lines.push_back(
					"Focused " +
					std::string(viewport.state.focused ? "yes" : "no") +
					"  Hovered " +
					std::string(viewport.state.hovered ? "yes" : "no") +
					"  PendingSync " +
					std::string(render_state && render_state->pending_sync ? "yes" : "no"));
			}

			return lines;
		}
	}

	ViewportPanel::ViewportPanel(std::string viewport_id, std::string panel_id, std::string title)
		: EditorPanel(std::move(panel_id), std::move(title))
		, m_viewportId(std::move(viewport_id))
	{
	}

	EditorViewportInstance* ViewportPanel::resolve_viewport(EditorContext& context) const
	{
		return context.viewport_service ? context.viewport_service->find_viewport(m_viewportId) : nullptr;
	}

	const EditorViewportInstance* ViewportPanel::resolve_viewport(const EditorContext& context) const
	{
		return context.viewport_service ? context.viewport_service->find_viewport(m_viewportId) : nullptr;
	}

	void ViewportPanel::on_attach(EditorContext& context)
	{
		if (context.viewport_service)
		{
			context.viewport_service->ensure_viewport(m_viewportId, get_title());
		}
		HLogInfo("ViewportPanel attached.");
	}

	void ViewportPanel::on_update(EditorContext& context)
	{
		EditorViewportInstance* viewport = resolve_viewport(context);
		if (!viewport)
		{
			return;
		}

		const EditorViewportRenderState* render_state =
			context.viewport_service ? context.viewport_service->get_render_state(m_viewportId) : nullptr;
		if (render_state)
		{
			viewport->state.width = render_state->output_width;
			viewport->state.height = render_state->output_height;
			return;
		}

		viewport->state.width = 0u;
		viewport->state.height = 0u;
	}

	void ViewportPanel::draw_toolbar(EditorContext& context, EditorViewportInstance& viewport)
	{
		if (!context.ui_context || !context.viewport_service)
		{
			return;
		}

		EditorViewportPresentation* presentation = context.viewport_service->get_presentation(m_viewportId);
		if (!presentation || !presentation->show_toolbar)
		{
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		ui.text_unformatted(viewport_kind_label(presentation->kind));
		ui.same_line();
		const bool is_primary = context.viewport_service->is_primary_viewport(viewport.id);
		if (ui.small_button(is_primary ? "Primary" : "Set Primary"))
		{
			context.viewport_service->set_primary_viewport(m_viewportId);
		}

		ui.same_line();
		draw_toggle_button(ui, "Aspect", presentation->preserve_aspect);
		ui.same_line();
		draw_toggle_button(ui, "Input", presentation->accepts_input);
		ui.same_line();
		draw_toggle_button(ui, "Stats", presentation->show_stats);
		ui.same_line();
		draw_toggle_button(ui, "Overlay", presentation->show_overlays);
	}

	void ViewportPanel::draw_toggle_button(AshEngine::UIContext& ui, const char* label, bool& value) const
	{
		const bool was_enabled = value;
		if (was_enabled)
		{
			ui.push_style_color(AshEngine::UIStyleColorKind::Button, { 0.42f, 0.49f, 0.57f, 1.0f });
			ui.push_style_color(AshEngine::UIStyleColorKind::ButtonHovered, { 0.46f, 0.53f, 0.62f, 1.0f });
			ui.push_style_color(AshEngine::UIStyleColorKind::ButtonActive, { 0.38f, 0.45f, 0.53f, 1.0f });
		}

		if (ui.small_button(label))
		{
			value = !value;
		}

		if (was_enabled)
		{
			ui.pop_style_color(3);
		}
	}

	void ViewportPanel::draw_overlay(EditorContext& context, const EditorViewportInstance& viewport) const
	{
		if (!context.ui_context || !context.viewport_service)
		{
			return;
		}

		const EditorViewportPresentation* presentation = context.viewport_service->get_presentation(m_viewportId);
		const EditorViewportRenderState* render_state = context.viewport_service->get_render_state(m_viewportId);
		if (!presentation || (!presentation->show_overlays && !presentation->show_stats))
		{
			return;
		}

		const std::vector<std::string> lines = make_overlay_lines(
			viewport,
			*presentation,
			render_state,
			context.viewport_service->is_primary_viewport(viewport.id));
		if (lines.empty())
		{
			return;
		}

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		if (!draw_list)
		{
			return;
		}

		const ImVec2 item_min = ImGui::GetItemRectMin();
		const float padding = 10.0f;
		const float line_spacing = 4.0f;
		float max_width = 0.0f;
		float total_height = padding * 2.0f;
		for (const std::string& line : lines)
		{
			const ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
			max_width = std::max(max_width, line_size.x);
			total_height += line_size.y;
		}
		total_height += std::max(0.0f, static_cast<float>(lines.size() - 1)) * line_spacing;

		const ImVec2 panel_min(item_min.x + 12.0f, item_min.y + 12.0f);
		const ImVec2 panel_max(panel_min.x + max_width + padding * 2.0f, panel_min.y + total_height);
		draw_list->AddRectFilled(
			panel_min,
			panel_max,
			ImGui::GetColorU32(ImVec4(0.12f, 0.14f, 0.18f, 0.72f)),
			6.0f);
		draw_list->AddRect(
			panel_min,
			panel_max,
			ImGui::GetColorU32(ImVec4(0.64f, 0.72f, 0.84f, 0.32f)),
			6.0f);

		float text_y = panel_min.y + padding;
		for (const std::string& line : lines)
		{
			draw_list->AddText(
				ImVec2(panel_min.x + padding, text_y),
				ImGui::GetColorU32(ImGuiCol_Text),
				line.c_str());
			text_y += ImGui::CalcTextSize(line.c_str()).y + line_spacing;
		}
	}

	void ViewportPanel::on_gui(EditorContext& context)
	{
		EditorViewportInstance* viewport = resolve_viewport(context);
		const bool trace_this_frame = should_trace_viewport_panel();
		if (trace_this_frame)
		{
			HLogInfo(
				"ViewportPanel::on_gui begin. surface={}, ui_ready={}, requested={}x{}.",
				viewport ? viewport->surface.value : 0u,
				context.gui_renderer_ready,
				viewport ? viewport->state.requested_width : 0u,
				viewport ? viewport->state.requested_height : 0u);
		}

		const bool window_visible = begin_panel_window(context, AshEngine::UIWindowFlagBits::MenuBar);
		if (context.viewport_service)
		{
			context.viewport_service->set_panel_open(m_viewportId, is_open());
		}

		if (!window_visible)
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui skipped because Begin returned false.");
			}
			end_panel_window(context);
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		if (viewport)
		{
			viewport->state.focused = ui.is_window_focused();
			viewport->state.hovered = ui.is_window_hovered();
			if (context.viewport_service)
			{
				if (viewport->state.focused)
				{
					context.viewport_service->set_primary_viewport(m_viewportId);
				}
			}
		}

		if (ui.begin_menu_bar())
		{
			if (viewport)
			{
				draw_toolbar(context, *viewport);
			}
			ui.end_menu_bar();
		}

		const AshEngine::UIVec2 available = ui.get_content_region_avail();
		if (viewport)
		{
			if (context.viewport_service)
			{
				context.viewport_service->update_requested_size(
					m_viewportId,
					available.x > 1.0f ? static_cast<uint32_t>(available.x) : 0u,
					available.y > 1.0f ? static_cast<uint32_t>(available.y) : 0u);
			}
		}

		if (viewport && viewport->surface.is_valid() && context.ui_context && available.x > 2.0f && available.y > 2.0f)
		{
			bool preserve_aspect = false;
			if (const EditorViewportPresentation* presentation =
				context.viewport_service ? context.viewport_service->get_presentation(m_viewportId) : nullptr)
			{
				preserve_aspect = presentation->preserve_aspect;
			}
			if (viewport->state.width == 0u || viewport->state.height == 0u)
			{
				if (trace_this_frame)
				{
					HLogWarning("ViewportPanel::on_gui is waiting for a synchronized scene surface.");
				}
				ui.text_wrapped("Scene surface is not available yet.");
			}
			else
			{
				if (trace_this_frame)
				{
					HLogInfo(
						"ViewportPanel::on_gui drawing scene surface {} with available size {}x{}.",
						viewport->surface.value,
						available.x,
						available.y);
				}
				context.ui_context->draw_surface_fill_available(viewport->surface, preserve_aspect);
				draw_overlay(context, *viewport);
				if (trace_this_frame)
				{
					HLogInfo("ViewportPanel::on_gui finished drawing scene surface.");
				}
			}
		}
		else if (!viewport || !viewport->surface.is_valid())
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui has no scene surface to display.");
			}
			ui.text_wrapped("Scene surface is not available yet.");
		}
		else
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui is waiting for UIContext or sufficient panel size.");
			}
			ui.text_wrapped("Viewport preview is waiting for the engine UIContext.");
		}

		end_panel_window(context);
		if (trace_this_frame)
		{
			HLogInfo("ViewportPanel::on_gui end.");
		}
	}
}

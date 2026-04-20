#include "Panels/ViewportPanel.h"
#include "Base/hlog.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/Renderer.h"
#include "Services/EditorViewportService.h"
#include <utility>

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

		bool is_sampling_back_buffer_unsafe(const std::shared_ptr<AshEngine::RenderTarget>& render_target)
		{
			AshEngine::Renderer* renderer = AshEngine::Application::get_renderer();
			if (!renderer || !render_target)
			{
				return false;
			}

			const std::shared_ptr<AshEngine::RenderTarget> back_buffer = renderer->get_back_buffer();
			return back_buffer && back_buffer.get() == render_target.get();
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
		if (viewport && viewport->render_target)
		{
			viewport->state.width = viewport->render_target->get_width();
			viewport->state.height = viewport->render_target->get_height();
			return;
		}

		if (viewport)
		{
			viewport->state.width = 0;
			viewport->state.height = 0;
		}
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
		bool preserve_aspect = presentation->preserve_aspect;
		if (ui.checkbox("Aspect", preserve_aspect))
		{
			presentation->preserve_aspect = preserve_aspect;
		}

		ui.same_line();
		bool accepts_input = presentation->accepts_input;
		if (ui.checkbox("Input", accepts_input))
		{
			presentation->accepts_input = accepts_input;
		}

		ui.same_line();
		bool show_stats = presentation->show_stats;
		if (ui.checkbox("Stats", show_stats))
		{
			presentation->show_stats = show_stats;
		}

		ui.same_line();
		bool show_overlays = presentation->show_overlays;
		if (ui.checkbox("Overlay", show_overlays))
		{
			presentation->show_overlays = show_overlays;
		}
	}

	void ViewportPanel::draw_status(EditorContext& context, const EditorViewportInstance& viewport) const
	{
		if (!context.ui_context || !context.viewport_service)
		{
			return;
		}

		const EditorViewportPresentation* presentation = context.viewport_service->get_presentation(m_viewportId);
		const EditorViewportRenderState* render_state = context.viewport_service->get_render_state(m_viewportId);
		if (!presentation)
		{
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		ui.text(
			"Mode: %s | Primary: %s | Input: %s | Overlay: %s",
			viewport_kind_label(presentation->kind),
			context.viewport_service->is_primary_viewport(viewport.id) ? "yes" : "no",
			presentation->accepts_input ? "enabled" : "disabled",
			presentation->show_overlays ? "visible" : "hidden");
		ui.text(
			"Allocated RT: %ux%u | Requested: %ux%u | Dirty: %s",
			viewport.state.width,
			viewport.state.height,
			viewport.state.requested_width,
			viewport.state.requested_height,
			render_state && render_state->pending_rebuild ? "yes" : "no");
		if (presentation->show_stats)
		{
			ui.text(
				"Focused: %s | Hovered: %s | PreserveAspect: %s",
				viewport.state.focused ? "true" : "false",
				viewport.state.hovered ? "true" : "false",
				presentation->preserve_aspect ? "true" : "false");
		}
	}

	void ViewportPanel::on_gui(EditorContext& context)
	{
		EditorViewportInstance* viewport = resolve_viewport(context);
		const bool trace_this_frame = should_trace_viewport_panel();
		if (trace_this_frame)
		{
			HLogInfo(
				"ViewportPanel::on_gui begin. rt={}, ui_ready={}, requested={}x{}.",
				static_cast<const void*>(viewport ? viewport->render_target.get() : nullptr),
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

		if (viewport)
		{
			draw_status(context, *viewport);
			ui.separator();
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

		if (viewport && is_sampling_back_buffer_unsafe(viewport->render_target))
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui detected unsafe back buffer sampling path.");
			}
			ui.text_wrapped("Viewport preview is temporarily disabled on the swapchain back buffer.");
			ui.text_wrapped("DX12 will crash if the editor samples the same back buffer that the UI overlay pass is writing to.");
			ui.text_wrapped("The next safe step is wiring a dedicated off-screen viewport render target.");
		}
		else if (viewport && viewport->render_target && context.ui_context && available.x > 2.0f && available.y > 2.0f)
		{
			bool preserve_aspect = false;
			if (const EditorViewportPresentation* presentation =
				context.viewport_service ? context.viewport_service->get_presentation(m_viewportId) : nullptr)
			{
				preserve_aspect = presentation->preserve_aspect;
			}
			if (trace_this_frame)
			{
				HLogInfo(
					"ViewportPanel::on_gui sampling render target {} with available size {}x{}.",
					static_cast<const void*>(viewport->render_target.get()),
					available.x,
					available.y);
			}
			context.ui_context->draw_render_target_fill_available(viewport->render_target, preserve_aspect);
			if (trace_this_frame)
			{
				HLogInfo("ViewportPanel::on_gui finished sampling render target.");
			}
		}
		else if (!viewport || !viewport->render_target)
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui has no render target to display.");
			}
			ui.text_wrapped("Render target is not available yet.");
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

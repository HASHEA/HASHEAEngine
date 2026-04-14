#include "Panels/ViewportPanel.h"
#include "Base/hlog.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/Renderer.h"
#include "imgui.h"

namespace AshEditor
{
	namespace
	{
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

	ViewportPanel::ViewportPanel()
		: EditorPanel("viewport", "Viewport")
	{
	}

	void ViewportPanel::set_render_target(const std::shared_ptr<AshEngine::RenderTarget>& render_target)
	{
		m_render_target = render_target;
	}

	const std::shared_ptr<AshEngine::RenderTarget>& ViewportPanel::get_render_target() const
	{
		return m_render_target;
	}

	void ViewportPanel::on_attach(EditorContext& context)
	{
		(void)context;
		HLogInfo("ViewportPanel attached.");
	}

	void ViewportPanel::on_update(EditorContext& context)
	{
		if (m_render_target)
		{
			context.viewport.width = m_render_target->get_width();
			context.viewport.height = m_render_target->get_height();
			return;
		}

		context.viewport.width = 0;
		context.viewport.height = 0;
	}

	void ViewportPanel::on_gui(EditorContext& context)
	{
		const bool trace_this_frame = should_trace_viewport_panel();
		if (trace_this_frame)
		{
			HLogInfo(
				"ViewportPanel::on_gui begin. rt={}, ui_ready={}, requested={}x{}.",
				static_cast<const void*>(m_render_target.get()),
				context.gui_renderer_ready,
				context.viewport.requested_width,
				context.viewport.requested_height);
		}

		if (!begin_panel_window())
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui skipped because Begin returned false.");
			}
			end_panel_window();
			return;
		}

		context.viewport.focused = ImGui::IsWindowFocused();
		context.viewport.hovered = ImGui::IsWindowHovered();

		ImGui::Text("BackBuffer: %ux%u", context.viewport.width, context.viewport.height);
		ImGui::Text("Requested: %ux%u", context.viewport.requested_width, context.viewport.requested_height);
		ImGui::Text("Focused: %s", context.viewport.focused ? "true" : "false");
		ImGui::Text("Hovered: %s", context.viewport.hovered ? "true" : "false");
		ImGui::Text("Engine UIContext: %s", context.gui_renderer_ready ? "ready" : "unavailable");
		ImGui::Separator();

		const ImVec2 available = ImGui::GetContentRegionAvail();
		context.viewport.requested_width = available.x > 1.0f ? static_cast<uint32_t>(available.x) : 0u;
		context.viewport.requested_height = available.y > 1.0f ? static_cast<uint32_t>(available.y) : 0u;

		if (is_sampling_back_buffer_unsafe(m_render_target))
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui detected unsafe back buffer sampling path.");
			}
			ImGui::TextWrapped("Viewport preview is temporarily disabled on the swapchain back buffer.");
			ImGui::TextWrapped("DX12 will crash if the editor samples the same back buffer that the UI overlay pass is writing to.");
			ImGui::TextWrapped("The next safe step is wiring a dedicated off-screen viewport render target.");
		}
		else if (m_render_target && context.ui_context && available.x > 2.0f && available.y > 2.0f)
		{
			if (trace_this_frame)
			{
				HLogInfo(
					"ViewportPanel::on_gui sampling render target {} with available size {}x{}.",
					static_cast<const void*>(m_render_target.get()),
					available.x,
					available.y);
			}
			context.ui_context->draw_render_target_fill_available(m_render_target, true);
			if (trace_this_frame)
			{
				HLogInfo("ViewportPanel::on_gui finished sampling render target.");
			}
		}
		else if (!m_render_target)
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui has no render target to display.");
			}
			ImGui::TextUnformatted("Render target is not available yet.");
		}
		else
		{
			if (trace_this_frame)
			{
				HLogWarning("ViewportPanel::on_gui is waiting for UIContext or sufficient panel size.");
			}
			ImGui::TextUnformatted("Viewport preview is waiting for the engine UIContext.");
		}

		end_panel_window();
		if (trace_this_frame)
		{
			HLogInfo("ViewportPanel::on_gui end.");
		}
	}
}

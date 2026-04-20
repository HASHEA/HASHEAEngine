#include "Editor.h"
#include "Base/hlog.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/Renderer.h"

namespace AshEditor
{
	namespace
	{
		bool should_trace_editor_frame(uint32_t frame_index)
		{
			return frame_index <= 2;
		}

		void ensure_viewport_render_target(
			AshEngine::Renderer& renderer,
			EditorViewportService& viewport_service,
			EditorViewportInstance& viewport,
			const std::shared_ptr<AshEngine::RenderTarget>& back_buffer)
		{
			const EditorViewportRenderRequest request = viewport_service.get_render_request(viewport.id, back_buffer);
			if (!request.rebuild_required)
			{
				return;
			}

			AshEngine::RenderTargetDesc desc{};
			desc.width = static_cast<uint16_t>(request.width);
			desc.height = static_cast<uint16_t>(request.height);
			desc.format = request.format;
			desc.shader_resource = true;
			desc.unordered_access = false;
			desc.name = viewport.display_name.empty() ? "EditorViewportRenderTarget" : viewport.display_name.c_str();
			desc.use_optimized_clear_value = true;
			desc.optimized_clear_color = { 0.02f, 0.04f, 0.07f, 1.0f };
			std::shared_ptr<AshEngine::RenderTarget> render_target = renderer.create_render_target(desc);

			if (!render_target)
			{
				HLogError(
					"Editor failed to create viewport render target '{}' {}x{}.",
					viewport.id,
					request.width,
					request.height);
				return;
			}

			const bool had_render_target = viewport.render_target != nullptr;
			viewport_service.notify_render_target_updated(viewport.id, render_target);

			HLogInfo(
				"Editor viewport render target {}: id='{}', size={}x{}, format={}, ptr={}.",
				had_render_target ? "rebuilt" : "created",
				viewport.id,
				request.width,
				request.height,
				static_cast<uint32_t>(request.format),
				static_cast<const void*>(render_target.get()));
		}
	}

	Editor::Editor(const AshEngine::EngineInitConfig& config) : AshEngine::Application(config)
	{
		HLogInfo("Ash Editor Start !");
		bootstrap_editor();
	}

	Editor::~Editor()
	{
		shutdown_editor();
		m_codexLogoDemo.shutdown();
	}

	void Editor::bootstrap_editor()
	{
		m_editorApplication = std::make_unique<EditorApplication>();
		if (!m_editorApplication->initialize())
		{
			HLogError("Editor application bootstrap failed.");
		}
	}

	void Editor::shutdown_editor()
	{
		if (m_editorApplication)
		{
			m_editorApplication->shutdown();
			m_editorApplication.reset();
		}
	}

	void Editor::ensure_viewport_render_targets()
	{
		if (!m_editorApplication)
		{
			return;
		}

		AshEngine::Renderer* renderer = AshEngine::Application::get_renderer();
		if (!renderer)
		{
			return;
		}

		const std::shared_ptr<AshEngine::RenderTarget> back_buffer = renderer->get_back_buffer();
		EditorViewportService& viewport_service = m_editorApplication->get_viewport_service();
		for (EditorViewportInstance* viewport : viewport_service.get_viewports())
		{
			if (!viewport)
			{
				continue;
			}

			ensure_viewport_render_target(*renderer, viewport_service, *viewport, back_buffer);
		}
	}

	auto Editor::_on_update() -> void 
	{
		++m_updateFrameIndex;
		if (should_trace_editor_frame(m_updateFrameIndex))
		{
			HLogInfo("Editor::_on_update frame {} begin.", m_updateFrameIndex);
		}

		AshEngine::Application::_on_update();
		ensure_viewport_render_targets();
		if (m_editorApplication)
		{
			m_editorApplication->update();
		}

		if (should_trace_editor_frame(m_updateFrameIndex))
		{
			HLogInfo(
				"Editor::_on_update frame {} end. viewport_rt={}.",
				m_updateFrameIndex,
				static_cast<const void*>(
					m_editorApplication && m_editorApplication->get_primary_viewport()
						? m_editorApplication->get_primary_viewport()->render_target.get()
						: nullptr));
		}
	}

	auto Editor::_on_gui() -> void 
	{
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo("Editor::_on_gui frame {} begin.", m_renderFrameIndex);
		}

		if (m_editorApplication)
		{
			m_editorApplication->draw_gui();
		}

		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo("Editor::_on_gui frame {} end.", m_renderFrameIndex);
		}
	}

	auto Editor::_on_render_debug() -> void 
	{
		AshEngine::Application::_on_render_debug();

	}

	auto Editor::_on_render() -> void 
	{
		++m_renderFrameIndex;
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo("Editor::_on_render frame {} begin.", m_renderFrameIndex);
		}

		auto* renderer = AshEngine::Application::get_renderer();
		if (!renderer || !renderer->begin_frame())
		{
			HLogError("Editor failed to begin renderer frame.");
			return;
		}
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo("Editor::_on_render frame {} after begin_frame.", m_renderFrameIndex);
		}

		_on_render_debug();
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo("Editor::_on_render frame {} after render_debug.", m_renderFrameIndex);
		}

		EditorViewportInstance* primary_viewport = m_editorApplication ? m_editorApplication->get_primary_viewport() : nullptr;
		if (m_editorApplication)
		{
			for (EditorViewportInstance* viewport : m_editorApplication->get_viewport_service().get_viewports())
			{
				if (viewport && viewport->render_target && !m_codexLogoDemo.render(viewport->render_target))
				{
					HLogError("Editor frame render path failed for viewport '{}'.", viewport->id);
				}
			}
		}
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo(
				"Editor::_on_render frame {} after viewport render. viewport_rt={}.",
				m_renderFrameIndex,
				static_cast<const void*>(primary_viewport ? primary_viewport->render_target.get() : nullptr));
		}

		if (m_editorApplication)
		{
			_on_gui();
		}
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo("Editor::_on_render frame {} after gui.", m_renderFrameIndex);
		}

		renderer->end_frame();
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo("Editor::_on_render frame {} after end_frame.", m_renderFrameIndex);
		}
	}

	auto Editor::_present() -> void 
	{
		++m_presentFrameIndex;
		if (should_trace_editor_frame(m_presentFrameIndex))
		{
			HLogInfo("Editor::_present frame {} begin.", m_presentFrameIndex);
		}

		if (auto* renderer = AshEngine::Application::get_renderer())
		{
			renderer->present();
		}

		if (should_trace_editor_frame(m_presentFrameIndex))
		{
			HLogInfo("Editor::_present frame {} end.", m_presentFrameIndex);
		}
	}

}

auto create_application() -> AshEngine::Application*
{
	AshEngine::EngineInitConfig config{};
	config.initWidth = 1920;
	config.initHeight = 1080;
	config.title = "Ash Engine Editor";
	config.bVsync = false;
	config.swapchainBufferCount = 3;
	return new AshEditor::Editor(config);
}

auto destroy_application(AshEngine::Application* app) -> void
{
	delete app;
	app = nullptr;
}

#include "Editor.h"
#include "Base/hlog.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/Renderer.h"
#include <algorithm>
#include <limits>

namespace AshEditor
{
	namespace
	{
		bool should_trace_editor_frame(uint32_t frame_index)
		{
			return frame_index <= 2;
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
		m_viewportRenderTarget.reset();
	}

	void Editor::ensure_viewport_render_target()
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

		const EditorViewportState& viewport_state = m_editorApplication->get_viewport_state();
		uint32_t desired_width = viewport_state.requested_width;
		uint32_t desired_height = viewport_state.requested_height;

		const std::shared_ptr<AshEngine::RenderTarget> back_buffer = renderer->get_back_buffer();
		if ((desired_width == 0 || desired_height == 0) && back_buffer)
		{
			desired_width = back_buffer->get_width();
			desired_height = back_buffer->get_height();
		}

		desired_width = std::max<uint32_t>(1u, std::min<uint32_t>(desired_width, std::numeric_limits<uint16_t>::max()));
		desired_height = std::max<uint32_t>(1u, std::min<uint32_t>(desired_height, std::numeric_limits<uint16_t>::max()));

		const AshEngine::RenderTextureFormat target_format =
			back_buffer ? back_buffer->get_format() : AshEngine::RenderTextureFormat::BGRA8_SRGB;

		if (m_viewportRenderTarget &&
			m_viewportRenderTarget->get_width() == desired_width &&
			m_viewportRenderTarget->get_height() == desired_height &&
			m_viewportRenderTarget->get_format() == target_format)
		{
			return;
		}

		AshEngine::RenderTargetDesc viewport_target_desc{};
		viewport_target_desc.width = static_cast<uint16_t>(desired_width);
		viewport_target_desc.height = static_cast<uint16_t>(desired_height);
		viewport_target_desc.format = target_format;
		viewport_target_desc.shader_resource = true;
		viewport_target_desc.unordered_access = false;
		viewport_target_desc.name = "EditorViewportRenderTarget";
		viewport_target_desc.use_optimized_clear_value = true;
		viewport_target_desc.optimized_clear_color = { 0.02f, 0.04f, 0.07f, 1.0f };

		m_viewportRenderTarget = renderer->create_render_target(viewport_target_desc);

		if (!m_viewportRenderTarget)
		{
			HLogError("Editor failed to create viewport render target {}x{}.", desired_width, desired_height);
			return;
		}

		HLogInfo(
			"Editor viewport render target created: {}x{}, format={}, ptr={}.",
			desired_width,
			desired_height,
			static_cast<uint32_t>(target_format),
			static_cast<const void*>(m_viewportRenderTarget.get()));
	}

	auto Editor::_on_update() -> void 
	{
		++m_updateFrameIndex;
		if (should_trace_editor_frame(m_updateFrameIndex))
		{
			HLogInfo("Editor::_on_update frame {} begin.", m_updateFrameIndex);
		}

		AshEngine::Application::_on_update();
		ensure_viewport_render_target();
		if (m_editorApplication)
		{
			m_editorApplication->update(m_viewportRenderTarget);
		}

		if (should_trace_editor_frame(m_updateFrameIndex))
		{
			HLogInfo(
				"Editor::_on_update frame {} end. viewport_rt={}.",
				m_updateFrameIndex,
				static_cast<const void*>(m_viewportRenderTarget.get()));
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

		if (m_viewportRenderTarget && !m_codexLogoDemo.render(m_viewportRenderTarget))
		{
			HLogError("Editor frame render path failed.");
		}
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			HLogInfo(
				"Editor::_on_render frame {} after viewport render. viewport_rt={}.",
				m_renderFrameIndex,
				static_cast<const void*>(m_viewportRenderTarget.get()));
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

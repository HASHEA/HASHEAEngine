#include "Editor.h"
#include "Base/hlog.h"
#include "Function/Application.h"
#include "Function/Render/Renderer.h"
#include <array>

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
			m_editorApplication->get_viewport_service().destroy_scene_presentations(
				AshEngine::Application::get_scene_presentation());
			m_editorApplication->shutdown();
			m_editorApplication.reset();
		}
	}

	void Editor::sync_render_asset_manager(AshEngine::Renderer& renderer)
	{
		if (!m_editorApplication)
		{
			return;
		}

		AshEngine::AssetDatabase& asset_database = m_editorApplication->get_asset_database_service().get_database();
		get_render_asset_manager().initialize(&asset_database, &renderer);
	}

	void Editor::sync_scene_viewports()
	{
		if (!m_editorApplication)
		{
			return;
		}

		AshEngine::Renderer* renderer = AshEngine::Application::get_renderer();
		AshEngine::ScenePresentationSubsystem* scene_presentation = AshEngine::Application::get_scene_presentation();
		if (!renderer || !scene_presentation)
		{
			return;
		}

		sync_render_asset_manager(*renderer);

		AshEngine::Scene& active_scene = m_editorApplication->get_scene_service().get_active_scene();
		EditorViewportService& viewport_service = m_editorApplication->get_viewport_service();
		if (!viewport_service.sync_scene_presentations(*scene_presentation, active_scene))
		{
			HLogError("Editor failed to synchronize scene viewport presentation bindings.");
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
		if (m_editorApplication)
		{
			m_editorApplication->update();
			sync_scene_viewports();
		}

		if (should_trace_editor_frame(m_updateFrameIndex))
		{
			const EditorViewportInstance* primary_viewport =
				m_editorApplication ? m_editorApplication->get_primary_viewport() : nullptr;
			HLogInfo(
				"Editor::_on_update frame {} end. viewport_output={}, viewport_surface={}.",
				m_updateFrameIndex,
				primary_viewport ? primary_viewport->output.value : 0u,
				primary_viewport ? primary_viewport->surface.value : 0u);
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

		AshEngine::Application::_on_render();
		if (should_trace_editor_frame(m_renderFrameIndex))
		{
			const EditorViewportInstance* primary_viewport =
				m_editorApplication ? m_editorApplication->get_primary_viewport() : nullptr;
			HLogInfo(
				"Editor::_on_render frame {} end. viewport_output={}, viewport_surface={}.",
				m_renderFrameIndex,
				primary_viewport ? primary_viewport->output.value : 0u,
				primary_viewport ? primary_viewport->surface.value : 0u);
		}
	}

	auto Editor::_present() -> void 
	{
		++m_presentFrameIndex;
		if (should_trace_editor_frame(m_presentFrameIndex))
		{
			HLogInfo("Editor::_present frame {} begin.", m_presentFrameIndex);
		}

		AshEngine::Application::_present();

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

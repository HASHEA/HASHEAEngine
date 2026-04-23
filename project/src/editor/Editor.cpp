#include "Editor.h"
#include "Base/hlog.h"
#include "Services/EditorSettingsService.h"
#include <array>
#include <string>

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
			m_editorApplication->shutdown();
			m_editorApplication.reset();
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
			m_editorApplication->sync_runtime_scene_presentations();
		}

		if (should_trace_editor_frame(m_updateFrameIndex))
		{
			const EditorViewportInstance* primary_viewport =
				m_editorApplication ? m_editorApplication->get_primary_viewport() : nullptr;
			HLogInfo(
				"Editor::_on_update frame {} end. viewport_surface={}, requested={}x{}, synced={}x{}.",
				m_updateFrameIndex,
				primary_viewport ? primary_viewport->surface.value : 0u,
				primary_viewport ? primary_viewport->state.requested_width : 0u,
				primary_viewport ? primary_viewport->state.requested_height : 0u,
				primary_viewport ? primary_viewport->state.width : 0u,
				primary_viewport ? primary_viewport->state.height : 0u);
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
				"Editor::_on_render frame {} end. viewport_surface={}, synced={}x{}.",
				m_renderFrameIndex,
				primary_viewport ? primary_viewport->surface.value : 0u,
				primary_viewport ? primary_viewport->state.width : 0u,
				primary_viewport ? primary_viewport->state.height : 0u);
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
	AshEditor::EditorSettingsService settings_service{};
	const std::filesystem::path workspace_root = AshEditor::discover_editor_workspace_root();
	settings_service.initialize(workspace_root);
	const std::filesystem::path layout_ini_path = settings_service.get_layout_ini_path();
	config.uiIniPath = layout_ini_path.empty() ? std::string{} : layout_ini_path.string();
	config.uiThemePreset = AshEditor::parse_editor_ui_theme_preset(settings_service.get_settings().ui_theme_preset);
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

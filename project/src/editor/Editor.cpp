#include "Editor.h"
#include "Base/hlog.h"
#include "Function/Render/Renderer.h"
namespace AshEditor
{
	Editor::Editor(const AshEngine::EngineInitConfig& config) : AshEngine::Application(config)
	{
		HLogInfo("Ash Editor Start !");
	}
	Editor::~Editor()
	{
		m_codexLogoDemo.shutdown();
	}
	auto Editor::_on_update() -> void 
	{
		AshEngine::Application::_on_update();
	}
	auto Editor::_on_gui() -> void 
	{
		AshEngine::Application::_on_gui();
	}
	auto Editor::_on_render_debug() -> void 
	{
		AshEngine::Application::_on_render_debug();

	}
	auto Editor::_on_render() -> void 
	{
		auto* renderer = AshEngine::Application::get_renderer();
		if (!renderer || !renderer->begin_frame())
		{
			HLogError("Editor failed to begin renderer frame.");
			return;
		}
		_on_render_debug();
		if (!m_codexLogoDemo.render())
		{
			HLogError("Editor frame render path failed.");
		}
		renderer->end_frame();
	}
	auto Editor::_present() -> void 
	{
		if (auto* renderer = AshEngine::Application::get_renderer())
		{
			renderer->present();
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

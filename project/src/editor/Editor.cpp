#include "Editor.h"
#include "Base/hlog.h"
namespace AshEditor
{
	Editor::Editor(const AshEngine::EngineInitConfig& config) : AshEngine::Application(config)
	{
		HLogInfo("Ash Editor Start !");
	}
	Editor::~Editor()
	{
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
		AshEngine::Application::_on_render();
	}
	auto Editor::_present() -> void 
	{
		AshEngine::Application::_present();
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
#include "Editor.h"
#include "Base/hlog.h"
namespace AshEditor
{
	Editor::Editor()
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
	return new AshEditor::Editor();
}

auto destroy_application(AshEngine::Application* app) -> void
{
	delete app;
	app = nullptr;
}
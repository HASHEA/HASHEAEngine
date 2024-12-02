#include "Editor.h"
namespace AshEditor
{
	Editor::Editor()
	{
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
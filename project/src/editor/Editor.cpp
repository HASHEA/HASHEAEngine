#include "Editor.h"

#include "App/EditorApplication.h"
#include "Base/hlog.h"

namespace AshEditor
{
	Editor::Editor(const AshEngine::EngineInitConfig& refConfig)
		: AshEngine::Application(refConfig)
	{
		HLogInfo("Ash Editor Start !");
		BootstrapEditor();
	}

	Editor::~Editor()
	{
		ShutdownEditor();
	}

	void Editor::BootstrapEditor()
	{
		_upEditorApplication = std::make_unique<EditorApplication>();
		if (!_upEditorApplication->Initialize())
		{
			HLogError("Editor application bootstrap failed.");
		}
	}

	void Editor::ShutdownEditor()
	{
		if (_upEditorApplication)
		{
			_upEditorApplication->Shutdown();
			_upEditorApplication.reset();
		}
	}

	void Editor::_on_update()
	{
		AshEngine::Application::_on_update();
		if (_upEditorApplication)
		{
			_upEditorApplication->Update();
			_upEditorApplication->SyncRuntimeScenePresentations();
		}
	}

	void Editor::_on_gui()
	{
		if (_upEditorApplication)
		{
			_upEditorApplication->DrawGui();
		}
	}

	void Editor::_on_render_debug()
	{
		AshEngine::Application::_on_render_debug();
	}

	void Editor::_on_render()
	{
		AshEngine::Application::_on_render();
	}

	void Editor::_present()
	{
		AshEngine::Application::_present();
	}
}

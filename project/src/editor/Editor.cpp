#include "Editor.h"

#include "App/EditorApplication.h"
#include "Base/hlog.h"

namespace AshEditor
{
	Editor::Editor(const AshEngine::EngineInitConfig& refConfig)
		: AshEngine::Application(refConfig)
	{
	}

	Editor::~Editor()
	{
		ShutdownEditor();
	}

	void Editor::_on_startup()
	{
		AshEngine::Application::_on_startup();
		HLogInfo("Ash Editor Start !");
		BootstrapEditor();
	}

	void Editor::_on_shutdown()
	{
		ShutdownEditor();
		AshEngine::Application::_on_shutdown();
	}

	void Editor::BootstrapEditor()
	{
		_bootstrapAttempted = true;
		if (_upEditorApplication)
		{
			_bootstrapFailed = false;
			return;
		}

		_upEditorApplication = std::make_unique<EditorApplication>();
		if (!_upEditorApplication->Initialize())
		{
			_bootstrapFailed = true;
			HLogError("Editor application bootstrap failed.");
			ShutdownEditor();
			request_exit();
		}
		else
		{
			_bootstrapFailed = false;
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

	auto Editor::_get_automation_readiness() const -> AshEngine::ApplicationReadiness
	{
		if (_bootstrapFailed || (_upEditorApplication && _upEditorApplication->HasAutomationFailure()))
		{
			return AshEngine::ApplicationReadiness::Failed;
		}
		if (_bootstrapAttempted && _upEditorApplication && _upEditorApplication->IsAutomationReady())
		{
			return AshEngine::ApplicationReadiness::Ready;
		}
		return AshEngine::ApplicationReadiness::Pending;
	}
}

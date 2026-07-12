#include "App/EditorApplication.h"

#include "App/EditorApplicationImpl.h"

namespace AshEditor
{
	EditorApplication::EditorApplication()
		: _upImpl(std::make_unique<EditorApplicationImpl>())
	{
	}

	EditorApplication::~EditorApplication() = default;

	bool EditorApplication::Initialize()
	{
		return _upImpl && _upImpl->Initialize();
	}

	void EditorApplication::Shutdown()
	{
		if (_upImpl)
		{
			_upImpl->Shutdown();
		}
	}

	void EditorApplication::Update()
	{
		if (_upImpl)
		{
			_upImpl->Update();
		}
	}

	void EditorApplication::DrawGui()
	{
		if (_upImpl)
		{
			_upImpl->DrawGui();
		}
	}

	void EditorApplication::SyncRuntimeScenePresentations()
	{
		if (_upImpl)
		{
			_upImpl->SyncRuntimeScenePresentations();
		}
	}

	bool EditorApplication::IsAutomationReady() const
	{
		return _upImpl && _upImpl->IsAutomationReady();
	}

	bool EditorApplication::HasAutomationFailure() const
	{
		return !_upImpl || _upImpl->HasAutomationFailure();
	}

	EditorViewportInstance* EditorApplication::GetPrimaryViewport()
	{
		return _upImpl ? _upImpl->GetPrimaryViewport() : nullptr;
	}

	const EditorViewportInstance* EditorApplication::GetPrimaryViewport() const
	{
		return _upImpl ? _upImpl->GetPrimaryViewport() : nullptr;
	}

	EditorViewportService& EditorApplication::GetViewportService()
	{
		return _upImpl->GetViewportService();
	}

	const EditorViewportService& EditorApplication::GetViewportService() const
	{
		return _upImpl->GetViewportService();
	}
}

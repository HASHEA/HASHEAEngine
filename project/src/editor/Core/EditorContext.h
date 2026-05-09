#pragma once

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class SelectionService;
	class SceneService;

	struct EditorContext
	{
		SelectionService* pSelectionService = nullptr;
		SceneService* pSceneService = nullptr;
		AshEngine::UIContext* pUiContext = nullptr;
		bool bGuiRendererReady = false;
	};
}

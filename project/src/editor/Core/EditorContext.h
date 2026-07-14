#pragma once

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class SelectionService;
	class SceneService;
	class TerrainEditorService;

	struct EditorContext
	{
		SelectionService* pSelectionService = nullptr;
		SceneService* pSceneService = nullptr;
		TerrainEditorService* pTerrainEditorService = nullptr;
		AshEngine::UIContext* pUiContext = nullptr;
		bool bGuiRendererReady = false;
	};
}

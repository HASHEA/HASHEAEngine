#pragma once

namespace AshEditor
{
	class AssetDatabaseService;
	class IEditorCommandExecutor;
	class SceneService;
	class SelectionService;

	struct InspectorPanelDeps
	{
		SelectionService* pSelectionService = nullptr;
		SceneService* pSceneService = nullptr;
		AssetDatabaseService* pAssetDatabaseService = nullptr;
		IEditorCommandExecutor* pCommandExecutor = nullptr;
	};
}

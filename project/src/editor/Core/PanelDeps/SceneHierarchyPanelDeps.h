#pragma once

namespace AshEditor
{
	class CommandService;
	class DragDropTransferService;
	class IEditorCommandExecutor;
	class IEditorIconService;
	class SceneService;
	class SelectionService;

	struct SceneHierarchyPanelDeps
	{
		SelectionService* pSelectionService = nullptr;
		SceneService* pSceneService = nullptr;
		CommandService* pCommandService = nullptr;
		IEditorCommandExecutor* pCommandExecutor = nullptr;
		IEditorIconService* pIconService = nullptr;
		DragDropTransferService* pDragDropTransferService = nullptr;
	};
}

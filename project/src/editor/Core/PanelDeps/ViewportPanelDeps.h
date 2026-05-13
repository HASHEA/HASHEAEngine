#pragma once

namespace AshEditor
{
	class DragDropTransferService;
	class EditorViewportCameraService;
	class EditorViewportService;
	class SceneService;
	class SelectionService;
	struct EditorGizmoState;

	struct ViewportPanelDeps
	{
		EditorViewportService* pViewportService = nullptr;
		EditorViewportCameraService* pViewportCameraService = nullptr;
		SceneService* pSceneService = nullptr;
		EditorGizmoState* pGizmoState = nullptr;
		SelectionService* pSelectionService = nullptr;
		DragDropTransferService* pDragDropTransferService = nullptr;
	};
}

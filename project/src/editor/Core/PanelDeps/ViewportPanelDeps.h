#pragma once

namespace AshEditor
{
	class AssetDatabaseService;
	class DragDropTransferService;
	class EditorSettingsService;
	class EditorGizmoService;
	class EditorViewportCameraService;
	class EditorViewportService;
	class IEditorCommandExecutor;
	class SceneService;
	class SelectionService;
	class TerrainEditorService;
	struct EditorGizmoState;

	struct ViewportPanelDeps
	{
		AssetDatabaseService* pAssetDatabaseService = nullptr;
		EditorViewportService* pViewportService = nullptr;
		EditorViewportCameraService* pViewportCameraService = nullptr;
		EditorSettingsService* pSettingsService = nullptr;
		EditorGizmoService* pGizmoService = nullptr;
		SceneService* pSceneService = nullptr;
		EditorGizmoState* pGizmoState = nullptr;
		SelectionService* pSelectionService = nullptr;
		TerrainEditorService* pTerrainEditorService = nullptr;
		DragDropTransferService* pDragDropTransferService = nullptr;
		IEditorCommandExecutor* pCommandExecutor = nullptr;
	};
}

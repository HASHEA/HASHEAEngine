#pragma once

namespace AshEditor
{
	class AssetDatabaseService;
	class AssetPreviewService;
	class CommandService;
	class DragDropTransferService;
	class EditorEventBus;
	class EditorSettingsService;
	class EditorShortcutService;
	class EditorGizmoService;
	class EditorViewportCameraService;
	class EditorViewportService;
	class IAssetBrowserActionTarget;
	class IEditorCommandExecutor;
	class IEditorIconService;
	class ISceneHierarchyActionTarget;
	class PanelManager;
	class SceneService;
	class SelectionService;
	struct EditorGizmoState;

	struct PanelBootstrapContext
	{
		SelectionService* pSelectionService = nullptr;
		SceneService* pSceneService = nullptr;
		AssetDatabaseService* pAssetDatabaseService = nullptr;
		AssetPreviewService* pAssetPreviewService = nullptr;
		CommandService* pCommandService = nullptr;
		IEditorCommandExecutor* pCommandExecutor = nullptr;
		EditorSettingsService* pSettingsService = nullptr;
		IEditorIconService* pIconService = nullptr;
		EditorShortcutService* pShortcutService = nullptr;
		EditorViewportService* pViewportService = nullptr;
		EditorViewportCameraService* pViewportCameraService = nullptr;
		DragDropTransferService* pDragDropTransferService = nullptr;
		EditorGizmoService* pGizmoService = nullptr;
		EditorGizmoState* pGizmoState = nullptr;
	};

	struct PanelBootstrapResult
	{
		IAssetBrowserActionTarget* pAssetBrowserActionTarget = nullptr;
		ISceneHierarchyActionTarget* pSceneHierarchyActionTarget = nullptr;
	};

	class PanelBootstrapper final
	{
	public:
		static PanelBootstrapResult CreateDefaultPanels(
			PanelManager& refPanelManager,
			const PanelBootstrapContext& refContext,
			EditorEventBus& refEventBus);
	};
}

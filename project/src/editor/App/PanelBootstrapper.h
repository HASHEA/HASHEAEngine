#pragma once

namespace AshEditor
{
	class AssetDatabaseService;
	class CommandService;
	class DragDropTransferService;
	class EditorEventBus;
	class EditorSettingsService;
	class EditorShortcutService;
	class EditorViewportService;
	class IAssetBrowserActionTarget;
	class IEditorCommandExecutor;
	class IEditorIconService;
	class ISceneHierarchyActionTarget;
	class PanelManager;
	class SceneService;
	class SelectionService;

	struct PanelBootstrapContext
	{
		SelectionService* pSelectionService = nullptr;
		SceneService* pSceneService = nullptr;
		AssetDatabaseService* pAssetDatabaseService = nullptr;
		CommandService* pCommandService = nullptr;
		IEditorCommandExecutor* pCommandExecutor = nullptr;
		EditorSettingsService* pSettingsService = nullptr;
		IEditorIconService* pIconService = nullptr;
		EditorShortcutService* pShortcutService = nullptr;
		EditorViewportService* pViewportService = nullptr;
		DragDropTransferService* pDragDropTransferService = nullptr;
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

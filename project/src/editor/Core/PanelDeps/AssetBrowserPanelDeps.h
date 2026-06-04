#pragma once

namespace AshEditor
{
	class AssetDatabaseService;
	class CommandService;
	class DragDropTransferService;
	class AssetPreviewService;
	class EditorSettingsService;
	class EditorShortcutService;
	class IEditorCommandExecutor;
	class IEditorIconService;
	class PanelManager;
	class SelectionService;

	struct AssetBrowserPanelDeps
	{
		AssetDatabaseService* pAssetDatabaseService = nullptr;
		CommandService* pCommandService = nullptr;
		IEditorIconService* pIconService = nullptr;
		SelectionService* pSelectionService = nullptr;
		EditorSettingsService* pSettingsService = nullptr;
		EditorShortcutService* pShortcutService = nullptr;
		DragDropTransferService* pDragDropTransferService = nullptr;
		AssetPreviewService* pAssetPreviewService = nullptr;
		IEditorCommandExecutor* pCommandExecutor = nullptr;
		PanelManager* pPanelManager = nullptr;
	};
}

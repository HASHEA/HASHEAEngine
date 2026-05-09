#pragma once

namespace AshEditor
{
	class AssetDatabaseService;
	class CommandService;
	class EditorSettingsService;
	class EditorShortcutService;
	class IEditorIconService;
	class SelectionService;

	struct AssetBrowserPanelDeps
	{
		AssetDatabaseService* pAssetDatabaseService = nullptr;
		CommandService* pCommandService = nullptr;
		IEditorIconService* pIconService = nullptr;
		SelectionService* pSelectionService = nullptr;
		EditorSettingsService* pSettingsService = nullptr;
		EditorShortcutService* pShortcutService = nullptr;
	};
}

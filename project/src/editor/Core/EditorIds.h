#pragma once

namespace AshEditor
{
	namespace EditorActionIds
	{
		inline constexpr char FileNewScene[] = "file.new_scene";
		inline constexpr char FileOpenScene[] = "file.open_scene";
		inline constexpr char FileReloadScene[] = "file.reload_scene";
		inline constexpr char FileSaveScene[] = "file.save_scene";
		inline constexpr char AssetsRefresh[] = "assets.refresh";
		inline constexpr char AssetsOpenSelected[] = "assets.open_selected";
		inline constexpr char AssetsNavigateUp[] = "assets.navigate_up";
		inline constexpr char AssetsCreateFolder[] = "assets.create_folder";
		inline constexpr char AssetsInstantiateSelected[] = "assets.instantiate_selected";
		inline constexpr char AssetsRenameSelected[] = "assets.rename_selected";
		inline constexpr char AssetsDeleteSelected[] = "assets.delete_selected";
		inline constexpr char AssetsReimportSelected[] = "assets.reimport_selected";
		inline constexpr char WindowResetLayout[] = "window.reset_layout";
		inline constexpr char WindowCommandPalette[] = "window.command_palette";
		inline constexpr char EditUndo[] = "edit.undo";
		inline constexpr char EditRedo[] = "edit.redo";
		inline constexpr char EditCopy[] = "edit.copy";
		inline constexpr char EditPaste[] = "edit.paste";
		inline constexpr char SceneCreateRoot[] = "scene.create_root";
		inline constexpr char SceneCreateChild[] = "scene.create_child";
		inline constexpr char SelectionRename[] = "selection.rename";
		inline constexpr char SelectionReparent[] = "selection.reparent";
		inline constexpr char SelectionDuplicate[] = "selection.duplicate";
		inline constexpr char SelectionDelete[] = "selection.delete";
	}

	namespace EditorViewportIds
	{
		inline constexpr char Scene[] = "scene";
		inline constexpr char Game[] = "game";
		inline constexpr char Default[] = "viewport";
	}

	namespace EditorPanelIds
	{
		inline constexpr char Viewport[] = "viewport";
		inline constexpr char SceneViewport[] = "scene_viewport";
		inline constexpr char GameViewport[] = "game_viewport";
		inline constexpr char SceneHierarchy[] = "scene_hierarchy";
		inline constexpr char Inspector[] = "inspector";
		inline constexpr char AssetPreview[] = "asset_preview";
		inline constexpr char Console[] = "console";
		inline constexpr char AssetBrowser[] = "asset_browser";
		inline constexpr char TerrainMode[] = "terrain_mode";
		inline constexpr char PropertyEditorDemo[] = "property_editor_demo";
		inline constexpr char NodeCanvasDemo[] = "node_canvas_demo";
	}

	namespace EditorWindowTitles
	{
		inline constexpr char Scene[] = "Scene";
		inline constexpr char Game[] = "Game";
		inline constexpr char SceneHierarchy[] = "Scene Hierarchy";
		inline constexpr char Inspector[] = "Inspector";
		inline constexpr char AssetPreview[] = "Asset Preview";
		inline constexpr char Console[] = "Console";
		inline constexpr char AssetBrowser[] = "Asset Browser";
		inline constexpr char TerrainMode[] = "Terrain";
		inline constexpr char PropertyEditorDemo[] = "Property Editor Demo";
		inline constexpr char NodeCanvasDemo[] = "Node Canvas Demo";
	}

	namespace EditorDragPayloadTypes
	{
		inline constexpr char SceneEntity[] = "ASH_EDITOR_SCENE_ENTITY";
		inline constexpr char Asset[] = "ASH_EDITOR_ASSET";
	}

	inline constexpr char kUntitledSceneName[] = "Untitled Scene";
}

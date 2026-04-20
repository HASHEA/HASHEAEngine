#pragma once
#include "Core/EditorContext.h"
#include "Panels/AssetBrowserPanel.h"
#include "Panels/ConsolePanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ViewportPanel.h"
#include "Services/AssetDatabaseService.h"
#include "Services/CommandService.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshEditor
{
	class EditorPanel;

	class EditorApplication
	{
	public:
		EditorApplication() = default;
		~EditorApplication() = default;

		bool initialize();
		void shutdown();

		void update();
		void draw_gui();
		EditorViewportInstance* get_primary_viewport();
		const EditorViewportInstance* get_primary_viewport() const;
		EditorViewportService& get_viewport_service();
		const EditorViewportService& get_viewport_service() const;

	private:
		void bootstrap_context();
		void bootstrap_panels();
		void shutdown_panels();
		void attach_panel(EditorPanel& panel);
		void register_actions();
		void activate_new_scene(const std::string& name);
		bool load_scene_into_editor(const std::filesystem::path& path);
		bool reload_active_scene();
		void update_last_scene_path_setting(const std::filesystem::path& path);
		void update_editor_context();
		void reset_editor_state_after_scene_change();
		void select_default_entity();
		void log_message(const std::string& message);
		void draw_workspace_host();
		void draw_main_menu_bar();
		void build_default_dock_layout(AshEngine::UIDockNodeId dockspace_id, const AshEngine::UIVec2& size);
		void load_viewport_layout_state();
		void save_viewport_layout_state() const;
		void apply_viewport_panel_open_state();
		std::filesystem::path get_viewport_layout_state_path() const;

	private:
		EditorSettingsService m_settingsService{};
		SelectionService m_selectionService{};
		SceneService m_sceneService{};
		AssetDatabaseService m_assetDatabaseService{};
		EditorViewportService m_viewportService{};
		CommandService m_commandService{};
		UndoRedoService m_undoRedoService{};
		EditorContext m_editorContext{};
		std::unique_ptr<ViewportPanel> m_viewportPanel = nullptr;
		std::unique_ptr<ViewportPanel> m_gameViewportPanel = nullptr;
		std::unique_ptr<SceneHierarchyPanel> m_sceneHierarchyPanel = nullptr;
		std::unique_ptr<InspectorPanel> m_inspectorPanel = nullptr;
		std::unique_ptr<ConsolePanel> m_consolePanel = nullptr;
		std::unique_ptr<AssetBrowserPanel> m_assetBrowserPanel = nullptr;
		std::vector<EditorPanel*> m_panels{};
		bool m_resetLayoutRequested = false;
		bool m_defaultDockLayoutBuilt = false;
		bool m_initialized = false;
		uint32_t m_guiFrameIndex = 0;
	};
}

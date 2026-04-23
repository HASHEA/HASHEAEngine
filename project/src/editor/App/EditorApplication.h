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
#include "Services/EditorIconService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include "imgui.h"
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
		void sync_runtime_scene_presentations();
		EditorViewportInstance* get_primary_viewport();
		const EditorViewportInstance* get_primary_viewport() const;
		EditorViewportService& get_viewport_service();
		const EditorViewportService& get_viewport_service() const;

	private:
		struct ShortcutBinding
		{
			const char* action_id = nullptr;
			ImGuiKeyChord chord = 0;
			bool allow_when_text_input = false;
		};

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
		bool invoke_action(const char* action_id);
		bool draw_action_menu_item(AshEngine::UIContext& ui, const char* action_id, bool enabled = true);
		void handle_global_shortcuts();
		void draw_workspace_host();
		void draw_main_menu_bar();
		void draw_theme_menu(AshEngine::UIContext& ui);
		void apply_theme_preset(AshEngine::UIThemePreset preset);
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
		EditorIconService m_iconService{};
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

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
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include <memory>
#include <string>
#include <vector>

namespace AshEngine
{
	class RenderTarget;
}

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

		void update(const std::shared_ptr<AshEngine::RenderTarget>& viewport_render_target);
		void draw_gui();
		const EditorViewportState& get_viewport_state() const;

	private:
		void bootstrap_context();
		void bootstrap_panels();
		void shutdown_panels();
		void attach_panel(EditorPanel& panel);
		void register_actions();
		void update_editor_context(const std::shared_ptr<AshEngine::RenderTarget>& viewport_render_target);
		void select_default_entity();
		void log_message(const std::string& message);
		void draw_dockspace();
		void draw_main_menu_bar();
		void build_default_layout(uint32_t dockspace_id);

	private:
		EditorSettingsService m_settingsService{};
		SelectionService m_selectionService{};
		SceneService m_sceneService{};
		AssetDatabaseService m_assetDatabaseService{};
		CommandService m_commandService{};
		UndoRedoService m_undoRedoService{};
		EditorContext m_editorContext{};
		std::unique_ptr<ViewportPanel> m_viewportPanel = nullptr;
		std::unique_ptr<SceneHierarchyPanel> m_sceneHierarchyPanel = nullptr;
		std::unique_ptr<InspectorPanel> m_inspectorPanel = nullptr;
		std::unique_ptr<ConsolePanel> m_consolePanel = nullptr;
		std::unique_ptr<AssetBrowserPanel> m_assetBrowserPanel = nullptr;
		std::vector<EditorPanel*> m_panels{};
		bool m_resetLayoutRequested = false;
		bool m_dockLayoutInitialized = false;
		bool m_initialized = false;
		uint32_t m_guiFrameIndex = 0;
	};
}

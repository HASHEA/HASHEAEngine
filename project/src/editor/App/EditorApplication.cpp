#include "App/EditorApplication.h"
#include "Base/hlog.h"
#include "Base/window/Window.h"
#include "Core/EditorPanel.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/RenderDevice.h"
#include <fstream>
#include <json.hpp>

namespace AshEditor
{
	namespace
	{
		constexpr const char* k_workspaceHostWindowName = "Editor Workspace";
		constexpr const char* k_workspaceDockspaceName = "EditorWorkspaceDockspace";
		constexpr const char* k_viewportLayoutStateFile = "product/config/editor/ViewportLayout.json";
		using json = nlohmann::json;

		bool should_trace_gui_frame(uint32_t frame_index)
		{
			return frame_index <= 2;
		}

		static auto make_scene_path_for_settings(EditorSettingsService& settings_service, const std::filesystem::path& path) -> std::string
		{
			if (path.empty())
			{
				return {};
			}

			const std::filesystem::path workspace_root = settings_service.get_workspace_root();
			const std::filesystem::path relative_path = std::filesystem::relative(path, workspace_root);
			return relative_path.generic_string();
		}

		std::string make_viewport_kind_name(EditorViewportKind kind)
		{
			switch (kind)
			{
			case EditorViewportKind::Scene: return "scene";
			case EditorViewportKind::Game: return "game";
			default: return "auxiliary";
			}
		}

		EditorViewportKind parse_viewport_kind_name(const std::string& kind)
		{
			if (kind == "scene")
			{
				return EditorViewportKind::Scene;
			}
			if (kind == "game")
			{
				return EditorViewportKind::Game;
			}
			return EditorViewportKind::Auxiliary;
		}
	}

	bool EditorApplication::initialize()
	{
		if (m_initialized)
		{
			return true;
		}

		const std::filesystem::path workspace_root = discover_editor_workspace_root();
		m_settingsService.initialize(workspace_root);

		const EditorSettings& settings = m_settingsService.get_settings();
		std::filesystem::path startup_scene_path = settings.last_scene_path.empty()
			? m_settingsService.get_startup_scene_path()
			: m_settingsService.resolve_workspace_path(settings.last_scene_path);

		const bool startup_scene_loaded = m_sceneService.initialize(startup_scene_path);
		m_assetDatabaseService.set_asset_root(m_settingsService.get_assets_root_path());
		m_assetDatabaseService.refresh();

		bootstrap_context();
		reset_editor_state_after_scene_change();
		m_viewportService.ensure_viewport("scene", "Scene");
		m_viewportService.ensure_viewport("game", "Game");
		m_viewportService.set_primary_viewport("scene");
		load_viewport_layout_state();
		bootstrap_panels();
		apply_viewport_panel_open_state();
		register_actions();

		if (!startup_scene_loaded && !startup_scene_path.empty())
		{
			log_message("Failed to load startup scene. Editor fell back to a new default scene.");
		}

		if (!m_editorContext.gui_renderer_ready)
		{
			log_message("Engine UIContext is not ready. Editor panels will stay idle until the runtime UI layer is available.");
		}
		else
		{
			log_message("Editor UI is running on the engine UIContext.");
		}
		log_message("Editor workspace is running through UIContext dockspace layout.");
		log_message("Editor services initialized.");
		log_message("Scene loaded: " + m_sceneService.get_active_scene().get_name());
		log_message("Asset scan complete: " + std::to_string(m_assetDatabaseService.get_items().size()) + " items.");

		m_initialized = true;
		return true;
	}

	void EditorApplication::shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		shutdown_panels();
		save_viewport_layout_state();
		m_viewportService.clear();
		m_undoRedoService.clear();

		m_settingsService.save();
		m_initialized = false;
	}

	void EditorApplication::update()
	{
		update_editor_context();
		for (EditorPanel* panel : m_panels)
		{
			if (panel && panel->is_open())
			{
				panel->on_update(m_editorContext);
			}
		}
	}

	EditorViewportInstance* EditorApplication::get_primary_viewport()
	{
		return m_viewportService.get_primary_viewport();
	}

	const EditorViewportInstance* EditorApplication::get_primary_viewport() const
	{
		return m_viewportService.get_primary_viewport();
	}

	EditorViewportService& EditorApplication::get_viewport_service()
	{
		return m_viewportService;
	}

	const EditorViewportService& EditorApplication::get_viewport_service() const
	{
		return m_viewportService;
	}

	void EditorApplication::draw_gui()
	{
		++m_guiFrameIndex;
		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo(
				"EditorApplication::draw_gui frame {} begin. ui_context={}, ui_ready={}, panels={}.",
				m_guiFrameIndex,
				static_cast<const void*>(m_editorContext.ui_context),
				m_editorContext.gui_renderer_ready,
				m_panels.size());
		}

		if (!m_editorContext.gui_renderer_ready || !m_editorContext.ui_context || !m_editorContext.ui_context->is_frame_active())
		{
			if (should_trace_gui_frame(m_guiFrameIndex))
			{
				HLogWarning("EditorApplication::draw_gui frame {} skipped because UIContext is unavailable.", m_guiFrameIndex);
			}
			return;
		}

		draw_workspace_host();
		draw_main_menu_bar();
		for (EditorPanel* panel : m_panels)
		{
			if (panel && panel->is_open())
			{
				if (should_trace_gui_frame(m_guiFrameIndex))
				{
					HLogInfo(
						"EditorApplication::draw_gui frame {} drawing panel '{}'.",
						m_guiFrameIndex,
						panel->get_title());
				}
				panel->on_gui(m_editorContext);
				if (should_trace_gui_frame(m_guiFrameIndex))
				{
					HLogInfo(
						"EditorApplication::draw_gui frame {} finished panel '{}'.",
						m_guiFrameIndex,
						panel->get_title());
				}
			}
		}

		m_resetLayoutRequested = false;
		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::draw_gui frame {} end.", m_guiFrameIndex);
		}
	}

	void EditorApplication::bootstrap_context()
	{
		m_editorContext.selection_service = &m_selectionService;
		m_editorContext.scene_service = &m_sceneService;
		m_editorContext.asset_database_service = &m_assetDatabaseService;
		m_editorContext.command_service = &m_commandService;
		m_editorContext.undo_redo_service = &m_undoRedoService;
		m_editorContext.settings_service = &m_settingsService;
		m_editorContext.viewport_service = &m_viewportService;
		m_editorContext.ui_context = AshEngine::Application::get_ui_context();
		m_editorContext.gui_renderer_ready = m_editorContext.ui_context != nullptr && m_editorContext.ui_context->is_initialized();
	}

	void EditorApplication::bootstrap_panels()
	{
		m_panels.clear();
		m_viewportPanel = std::make_unique<ViewportPanel>("scene", "scene_viewport", "Scene");
		m_gameViewportPanel = std::make_unique<ViewportPanel>("game", "game_viewport", "Game");
		m_sceneHierarchyPanel = std::make_unique<SceneHierarchyPanel>();
		m_inspectorPanel = std::make_unique<InspectorPanel>();
		m_consolePanel = std::make_unique<ConsolePanel>();
		m_assetBrowserPanel = std::make_unique<AssetBrowserPanel>();

		attach_panel(*m_viewportPanel);
		attach_panel(*m_gameViewportPanel);
		attach_panel(*m_sceneHierarchyPanel);
		attach_panel(*m_inspectorPanel);
		attach_panel(*m_consolePanel);
		attach_panel(*m_assetBrowserPanel);
	}

	void EditorApplication::shutdown_panels()
	{
		for (auto it = m_panels.rbegin(); it != m_panels.rend(); ++it)
		{
			if (*it)
			{
				(*it)->on_detach(m_editorContext);
			}
		}

		m_panels.clear();
		m_assetBrowserPanel.reset();
		m_consolePanel.reset();
		m_inspectorPanel.reset();
		m_sceneHierarchyPanel.reset();
		m_gameViewportPanel.reset();
		m_viewportPanel.reset();
		m_resetLayoutRequested = false;
		m_defaultDockLayoutBuilt = false;
	}

	void EditorApplication::attach_panel(EditorPanel& panel)
	{
		m_panels.push_back(&panel);
		panel.on_attach(m_editorContext);
	}

	void EditorApplication::register_actions()
	{
		m_commandService.register_action("file.new_scene", "New Scene", [this]() {
			activate_new_scene("Untitled Scene");
			log_message("Created a new default scene.");
		});

		m_commandService.register_action("file.reload_scene", "Reload Scene", [this]() {
			reload_active_scene();
		});

		m_commandService.register_action("file.save_scene", "Save Scene", [this]() {
			const std::filesystem::path scene_path = m_sceneService.get_active_scene_path().empty()
				? m_settingsService.get_startup_scene_path()
				: m_sceneService.get_active_scene_path();
			if (m_sceneService.save_scene(scene_path))
			{
				m_settingsService.get_settings().last_scene_path = make_scene_path_for_settings(m_settingsService, scene_path);
				m_settingsService.save();
				log_message("Scene saved to " + scene_path.generic_string());
			}
			else
			{
				log_message("Failed to save scene.");
			}
		});

		m_commandService.register_action("assets.refresh", "Refresh Assets", [this]() {
			if (m_assetDatabaseService.refresh())
			{
				log_message("Asset database refreshed.");
			}
			else
			{
				log_message("Asset refresh skipped because the asset root was not found.");
			}
		});

		m_commandService.register_action("window.reset_layout", "Reset Layout", [this]() {
			m_resetLayoutRequested = true;
			m_viewportService.reset_presentations();
			apply_viewport_panel_open_state();
			log_message("Dockspace layout reset requested.");
		});

		m_commandService.register_action("edit.undo", "Undo", [this]() {
			if (m_undoRedoService.undo(m_editorContext))
			{
				log_message("Undo executed.");
			}
			else
			{
				log_message("Undo failed.");
			}
		});

		m_commandService.register_action("edit.redo", "Redo", [this]() {
			if (m_undoRedoService.redo(m_editorContext))
			{
				log_message("Redo executed.");
			}
			else
			{
				log_message("Redo failed.");
			}
		});
	}

	void EditorApplication::activate_new_scene(const std::string& name)
	{
		m_sceneService.new_scene(name);
		update_last_scene_path_setting({});
		reset_editor_state_after_scene_change();
	}

	void EditorApplication::update_last_scene_path_setting(const std::filesystem::path& path)
	{
		m_settingsService.get_settings().last_scene_path = make_scene_path_for_settings(m_settingsService, path);
		m_settingsService.save();
	}

	bool EditorApplication::load_scene_into_editor(const std::filesystem::path& path)
	{
		if (path.empty())
		{
			return false;
		}

		if (!m_sceneService.load_scene(path))
		{
			return false;
		}

		update_last_scene_path_setting(path);
		reset_editor_state_after_scene_change();
		return true;
	}

	bool EditorApplication::reload_active_scene()
	{
		const std::filesystem::path active_scene_path = m_sceneService.get_active_scene_path();
		if (active_scene_path.empty())
		{
			log_message("Reload Scene skipped because the active scene has not been saved yet.");
			return false;
		}

		const bool loaded = load_scene_into_editor(active_scene_path);
		if (loaded)
		{
			log_message("Scene reloaded from " + active_scene_path.generic_string());
		}
		else
		{
			activate_new_scene("Untitled Scene");
			log_message(
				"Failed to reload scene from " +
				active_scene_path.generic_string() +
				". Editor fell back to a new default scene.");
		}
		return loaded;
	}

	void EditorApplication::update_editor_context()
	{
		m_editorContext.ui_context = AshEngine::Application::get_ui_context();
		m_editorContext.gui_renderer_ready = m_editorContext.ui_context != nullptr && m_editorContext.ui_context->is_initialized();
		const EditorViewportInstance* primary_viewport = m_viewportService.get_primary_viewport();
		m_editorContext.viewport = primary_viewport ? primary_viewport->state : EditorViewportState{};
	}

	void EditorApplication::reset_editor_state_after_scene_change()
	{
		m_selectionService.clear();
		m_undoRedoService.clear();
		select_default_entity();
	}

	void EditorApplication::select_default_entity()
	{
		const auto& entities = m_sceneService.get_active_scene().get_entities();
		if (entities.empty())
		{
			m_selectionService.clear();
			return;
		}

		m_selectionService.select({ EditorSelectionKind::Entity, entities.front().get_id(), entities.front().get_name(), {} });
	}

	void EditorApplication::log_message(const std::string& message)
	{
		if (m_consolePanel)
		{
			m_consolePanel->add_message(message);
		}
	}

	void EditorApplication::draw_workspace_host()
	{
		if (!m_editorContext.ui_context)
		{
			return;
		}

		AshEngine::UIContext& ui = *m_editorContext.ui_context;
		const AshEngine::UIRect main_viewport = ui.get_main_viewport_rect();
		const AshEngine::UIVec2 workspace_size{ main_viewport.width, main_viewport.height };
		ui.set_next_window_viewport(ui.get_main_viewport_id());
		ui.set_next_window_position({ main_viewport.x, main_viewport.y }, AshEngine::UIConditionFlagBits::Always);
		ui.set_next_window_size(workspace_size, AshEngine::UIConditionFlagBits::Always);
		ui.push_style_var(AshEngine::UIStyleVarKind::WindowPadding, { 0.0f, 0.0f });
		const bool host_open = ui.begin_dockspace_host_window(
			k_workspaceHostWindowName,
			nullptr,
			AshEngine::UIWindowFlagBits::NoSavedSettings);
		ui.pop_style_var();

		if (!host_open)
		{
			ui.end_window();
			return;
		}

		const AshEngine::UIDockNodeId dockspace_id = ui.dock_space(
			k_workspaceDockspaceName,
			{},
			AshEngine::UIDockNodeFlagBits::PassthruCentralNode);
		if (dockspace_id != 0u && (!m_defaultDockLayoutBuilt || m_resetLayoutRequested))
		{
			build_default_dock_layout(dockspace_id, workspace_size);
			m_defaultDockLayoutBuilt = true;
		}

		ui.end_window();
	}

	void EditorApplication::draw_main_menu_bar()
	{
		if (!m_editorContext.ui_context || !m_editorContext.ui_context->begin_main_menu_bar())
		{
			return;
		}

		AshEngine::UIContext& ui = *m_editorContext.ui_context;
		if (ui.begin_menu("File"))
		{
			if (ui.menu_item("New Scene"))
			{
				m_commandService.invoke("file.new_scene");
			}
			if (ui.menu_item("Reload Scene", nullptr, false, !m_sceneService.get_active_scene_path().empty()))
			{
				m_commandService.invoke("file.reload_scene");
			}
			if (ui.menu_item("Save Scene", "Ctrl+S", false, true))
			{
				m_commandService.invoke("file.save_scene");
			}
			ui.separator();
			ui.text_colored({ 0.70f, 0.70f, 0.70f, 1.0f }, "Current Scene: %s", m_sceneService.get_active_scene().get_name().c_str());
			ui.end_menu();
		}

		if (ui.begin_menu("Edit"))
		{
			if (ui.menu_item("Undo", "Ctrl+Z", false, m_undoRedoService.can_undo()))
			{
				m_commandService.invoke("edit.undo");
			}
			if (ui.menu_item("Redo", "Ctrl+Y", false, m_undoRedoService.can_redo()))
			{
				m_commandService.invoke("edit.redo");
			}
			ui.end_menu();
		}

		if (ui.begin_menu("Window"))
		{
			if (ui.menu_item("Reset Layout"))
			{
				m_commandService.invoke("window.reset_layout");
			}

			ui.separator();
			for (EditorPanel* panel : m_panels)
			{
				if (!panel)
				{
					continue;
				}

				bool open = panel->is_open();
				ui.menu_item(panel->get_title().c_str(), nullptr, &open);
				panel->set_open(open);
				if (panel == m_viewportPanel.get())
				{
					m_viewportService.set_panel_open("scene", open);
				}
				else if (panel == m_gameViewportPanel.get())
				{
					m_viewportService.set_panel_open("game", open);
				}
			}
			ui.end_menu();
		}

		if (ui.begin_menu("Assets"))
		{
			if (ui.menu_item("Refresh"))
			{
				m_commandService.invoke("assets.refresh");
			}
			ui.end_menu();
		}

		ui.end_main_menu_bar();
	}

	void EditorApplication::build_default_dock_layout(AshEngine::UIDockNodeId dockspace_id, const AshEngine::UIVec2& size)
	{
		if (!m_editorContext.ui_context || dockspace_id == 0u)
		{
			return;
		}

		AshEngine::UIContext& ui = *m_editorContext.ui_context;
		ui.dock_builder_remove_node(dockspace_id);
		ui.dock_builder_add_node(dockspace_id, AshEngine::UIDockNodeFlagBits::DockSpace);
		ui.dock_builder_set_node_size(dockspace_id, size);

		AshEngine::UIDockNodeId left_node = 0u;
		AshEngine::UIDockNodeId center_node = 0u;
		ui.dock_builder_split_node(dockspace_id, AshEngine::UIDirection::Left, 0.18f, &left_node, &center_node);

		AshEngine::UIDockNodeId inspector_node = 0u;
		AshEngine::UIDockNodeId center_stack_node = 0u;
		ui.dock_builder_split_node(center_node, AshEngine::UIDirection::Right, 0.22f, &inspector_node, &center_stack_node);

		AshEngine::UIDockNodeId asset_node = 0u;
		AshEngine::UIDockNodeId hierarchy_node = 0u;
		ui.dock_builder_split_node(left_node, AshEngine::UIDirection::Down, 0.42f, &asset_node, &hierarchy_node);

		AshEngine::UIDockNodeId lower_center_node = 0u;
		AshEngine::UIDockNodeId scene_node = 0u;
		ui.dock_builder_split_node(center_stack_node, AshEngine::UIDirection::Down, 0.50f, &lower_center_node, &scene_node);

		AshEngine::UIDockNodeId console_node = 0u;
		AshEngine::UIDockNodeId game_node = 0u;
		ui.dock_builder_split_node(lower_center_node, AshEngine::UIDirection::Down, 0.54f, &console_node, &game_node);

		ui.dock_builder_dock_window("Scene Hierarchy", hierarchy_node);
		ui.dock_builder_dock_window("Asset Browser", asset_node);
		ui.dock_builder_dock_window("Scene", scene_node);
		ui.dock_builder_dock_window("Game", game_node);
		ui.dock_builder_dock_window("Console", console_node);
		ui.dock_builder_dock_window("Inspector", inspector_node);
		ui.dock_builder_finish(dockspace_id);
	}

	std::filesystem::path EditorApplication::get_viewport_layout_state_path() const
	{
		return m_settingsService.get_workspace_root() / k_viewportLayoutStateFile;
	}

	void EditorApplication::load_viewport_layout_state()
	{
		const std::filesystem::path state_path = get_viewport_layout_state_path();
		if (state_path.empty() || !std::filesystem::exists(state_path))
		{
			return;
		}

		std::ifstream input(state_path);
		if (!input.is_open())
		{
			log_message("Failed to open viewport layout state file.");
			return;
		}

		json root{};
		input >> root;

		std::vector<EditorViewportPersistenceState> states{};
		if (root.contains("viewports") && root["viewports"].is_array())
		{
			for (const json& entry : root["viewports"])
			{
				EditorViewportPersistenceState state{};
				state.id = entry.value("id", std::string{});
				if (state.id.empty())
				{
					continue;
				}

				state.panel_open = entry.value("panelOpen", true);
				state.show_toolbar = entry.value("showToolbar", true);
				state.preserve_aspect = entry.value("preserveAspect", false);
				state.accepts_input = entry.value("acceptsInput", false);
				state.show_stats = entry.value("showStats", true);
				state.show_overlays = entry.value("showOverlays", false);
				states.push_back(state);

				if (EditorViewportPresentation* presentation = m_viewportService.get_presentation(state.id))
				{
					presentation->kind = parse_viewport_kind_name(entry.value("kind", std::string{}));
				}
			}
		}

		m_viewportService.apply_persistence_state(states, root.value("primaryViewportId", std::string{ "scene" }));
	}

	void EditorApplication::save_viewport_layout_state() const
	{
		const std::filesystem::path state_path = get_viewport_layout_state_path();
		if (state_path.empty())
		{
			return;
		}

		std::filesystem::create_directories(state_path.parent_path());

		json root{};
		if (const EditorViewportInstance* primary_viewport = m_viewportService.get_primary_viewport())
		{
			root["primaryViewportId"] = primary_viewport->id;
		}

		root["viewports"] = json::array();
		for (const EditorViewportPersistenceState& state : m_viewportService.capture_persistence_state())
		{
			json entry{};
			entry["id"] = state.id;
			entry["panelOpen"] = state.panel_open;
			entry["showToolbar"] = state.show_toolbar;
			entry["preserveAspect"] = state.preserve_aspect;
			entry["acceptsInput"] = state.accepts_input;
			entry["showStats"] = state.show_stats;
			entry["showOverlays"] = state.show_overlays;
			if (const EditorViewportPresentation* presentation = m_viewportService.get_presentation(state.id))
			{
				entry["kind"] = make_viewport_kind_name(presentation->kind);
			}
			root["viewports"].push_back(std::move(entry));
		}

		std::ofstream output(state_path, std::ios::out | std::ios::trunc);
		if (!output.is_open())
		{
			return;
		}

		output << root.dump(2);
	}

	void EditorApplication::apply_viewport_panel_open_state()
	{
		if (m_viewportPanel)
		{
			if (const EditorViewportPresentation* presentation = m_viewportService.get_presentation("scene"))
			{
				m_viewportPanel->set_open(presentation->panel_open);
			}
		}

		if (m_gameViewportPanel)
		{
			if (const EditorViewportPresentation* presentation = m_viewportService.get_presentation("game"))
			{
				m_gameViewportPanel->set_open(presentation->panel_open);
			}
		}
	}
}

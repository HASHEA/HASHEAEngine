#include "App/EditorApplication.h"
#include "Base/hlog.h"
#include "Core/EditorPanel.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/RenderDevice.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace AshEditor
{
	namespace
	{
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

		m_sceneService.initialize(startup_scene_path);
		m_assetDatabaseService.set_asset_root(m_settingsService.get_assets_root_path());
		m_assetDatabaseService.refresh();

		bootstrap_context();
		select_default_entity();
		bootstrap_panels();
		register_actions();

		if (!m_editorContext.gui_renderer_ready)
		{
			log_message("Engine UIContext is not ready. Editor panels will stay idle until the runtime UI layer is available.");
		}
		else
		{
			log_message("Editor UI is running on the engine UIContext.");
		}
		log_message("Custom editor layout ini routing is reserved until engine UIContext exposes runtime config injection.");
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

		m_settingsService.save();
		m_initialized = false;
	}

	void EditorApplication::update(const std::shared_ptr<AshEngine::RenderTarget>& viewport_render_target)
	{
		update_editor_context(viewport_render_target);
		for (EditorPanel* panel : m_panels)
		{
			if (panel && panel->is_open())
			{
				panel->on_update(m_editorContext);
			}
		}
	}

	const EditorViewportState& EditorApplication::get_viewport_state() const
	{
		return m_editorContext.viewport;
	}

	void EditorApplication::draw_gui()
	{
		++m_guiFrameIndex;
		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo(
				"EditorApplication::draw_gui frame {} begin. imgui_ctx={}, ui_ready={}, panels={}.",
				m_guiFrameIndex,
				static_cast<const void*>(ImGui::GetCurrentContext()),
				m_editorContext.gui_renderer_ready,
				m_panels.size());
		}

		if (!m_editorContext.gui_renderer_ready || ImGui::GetCurrentContext() == nullptr)
		{
			if (should_trace_gui_frame(m_guiFrameIndex))
			{
				HLogWarning("EditorApplication::draw_gui frame {} skipped because UIContext/ImGui is unavailable.", m_guiFrameIndex);
			}
			return;
		}

		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::draw_gui frame {} before dockspace.", m_guiFrameIndex);
		}
		draw_dockspace();
		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::draw_gui frame {} after dockspace.", m_guiFrameIndex);
		}

		draw_main_menu_bar();
		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::draw_gui frame {} after main menu.", m_guiFrameIndex);
		}

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
		m_editorContext.ui_context = AshEngine::Application::get_ui_context();
		m_editorContext.gui_renderer_ready = m_editorContext.ui_context != nullptr && m_editorContext.ui_context->is_initialized();
	}

	void EditorApplication::bootstrap_panels()
	{
		m_panels.clear();
		m_viewportPanel = std::make_unique<ViewportPanel>();
		m_sceneHierarchyPanel = std::make_unique<SceneHierarchyPanel>();
		m_inspectorPanel = std::make_unique<InspectorPanel>();
		m_consolePanel = std::make_unique<ConsolePanel>();
		m_assetBrowserPanel = std::make_unique<AssetBrowserPanel>();

		attach_panel(*m_viewportPanel);
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
		m_viewportPanel.reset();
		m_resetLayoutRequested = false;
		m_dockLayoutInitialized = false;
	}

	void EditorApplication::attach_panel(EditorPanel& panel)
	{
		m_panels.push_back(&panel);
		panel.on_attach(m_editorContext);
	}

	void EditorApplication::register_actions()
	{
		m_commandService.register_action("file.new_scene", "New Scene", [this]() {
			m_sceneService.new_scene("Untitled Scene");
			select_default_entity();
			log_message("Created a new default scene.");
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
			log_message("Layout reset requested.");
		});

		m_commandService.register_action("edit.undo", "Undo", [this]() {
			if (m_undoRedoService.undo(m_editorContext))
			{
				log_message("Undo executed.");
			}
		});

		m_commandService.register_action("edit.redo", "Redo", [this]() {
			if (m_undoRedoService.redo(m_editorContext))
			{
				log_message("Redo executed.");
			}
		});
	}

	void EditorApplication::update_editor_context(const std::shared_ptr<AshEngine::RenderTarget>& viewport_render_target)
	{
		m_editorContext.ui_context = AshEngine::Application::get_ui_context();
		m_editorContext.gui_renderer_ready = m_editorContext.ui_context != nullptr && m_editorContext.ui_context->is_initialized();
		if (m_viewportPanel)
		{
			m_viewportPanel->set_render_target(viewport_render_target);
		}
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

	void EditorApplication::draw_dockspace()
	{
		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::draw_dockspace frame {} begin.", m_guiFrameIndex);
		}

		ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGui::Begin("EditorDockSpace", nullptr, window_flags);
		ImGui::PopStyleVar(2);

		const ImGuiID dockspace_id = ImGui::GetID("AshEditorDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		build_default_layout(dockspace_id);
		ImGui::End();

		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::draw_dockspace frame {} end. dockspace_id={}.", m_guiFrameIndex, dockspace_id);
		}
	}

	void EditorApplication::draw_main_menu_bar()
	{
		if (!ImGui::BeginMainMenuBar())
		{
			return;
		}

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
			{
				m_commandService.invoke("file.new_scene");
			}
			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, true))
			{
				m_commandService.invoke("file.save_scene");
			}
			ImGui::Separator();
			ImGui::TextDisabled("Current Scene: %s", m_sceneService.get_active_scene().get_name().c_str());
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_undoRedoService.can_undo()))
			{
				m_commandService.invoke("edit.undo");
			}
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_undoRedoService.can_redo()))
			{
				m_commandService.invoke("edit.redo");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			if (ImGui::MenuItem("Reset Layout"))
			{
				m_commandService.invoke("window.reset_layout");
			}

			ImGui::Separator();
			for (EditorPanel* panel : m_panels)
			{
				if (!panel)
				{
					continue;
				}

				bool open = panel->is_open();
				if (ImGui::MenuItem(panel->get_title().c_str(), nullptr, &open))
				{
					panel->set_open(open);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Assets"))
		{
			if (ImGui::MenuItem("Refresh"))
			{
				m_commandService.invoke("assets.refresh");
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	void EditorApplication::build_default_layout(uint32_t dockspace_id)
	{
		if (dockspace_id == 0)
		{
			if (should_trace_gui_frame(m_guiFrameIndex))
			{
				HLogWarning("EditorApplication::build_default_layout frame {} skipped because dockspace id is 0.", m_guiFrameIndex);
			}
			return;
		}

		if (m_resetLayoutRequested)
		{
			ImGui::DockBuilderRemoveNode(dockspace_id);
			m_resetLayoutRequested = false;
			m_dockLayoutInitialized = false;
		}

		if (m_dockLayoutInitialized)
		{
			if (should_trace_gui_frame(m_guiFrameIndex))
			{
				HLogInfo("EditorApplication::build_default_layout frame {} skipped because layout is already initialized.", m_guiFrameIndex);
			}
			return;
		}

		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::build_default_layout frame {} begin for dockspace {}.", m_guiFrameIndex, dockspace_id);
		}

		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

		ImGuiID center_id = dockspace_id;
		ImGuiID left_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Left, 0.20f, nullptr, &center_id);
		ImGuiID right_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.25f, nullptr, &center_id);
		ImGuiID bottom_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.28f, nullptr, &center_id);
		ImGuiID asset_id = ImGui::DockBuilderSplitNode(bottom_id, ImGuiDir_Right, 0.50f, nullptr, &bottom_id);

		if (m_sceneHierarchyPanel)
		{
			ImGui::DockBuilderDockWindow(m_sceneHierarchyPanel->get_title().c_str(), left_id);
		}
		if (m_inspectorPanel)
		{
			ImGui::DockBuilderDockWindow(m_inspectorPanel->get_title().c_str(), right_id);
		}
		if (m_viewportPanel)
		{
			ImGui::DockBuilderDockWindow(m_viewportPanel->get_title().c_str(), center_id);
		}
		if (m_consolePanel)
		{
			ImGui::DockBuilderDockWindow(m_consolePanel->get_title().c_str(), bottom_id);
		}
		if (m_assetBrowserPanel)
		{
			ImGui::DockBuilderDockWindow(m_assetBrowserPanel->get_title().c_str(), asset_id);
		}

		ImGui::DockBuilderFinish(dockspace_id);
		m_dockLayoutInitialized = true;

		if (should_trace_gui_frame(m_guiFrameIndex))
		{
			HLogInfo("EditorApplication::build_default_layout frame {} end for dockspace {}.", m_guiFrameIndex, dockspace_id);
		}
	}
}

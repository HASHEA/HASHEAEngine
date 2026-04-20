#include "Panels/InspectorPanel.h"
#include "Base/hlog.h"
#include "Core/EntityCommands.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/AssetDatabaseService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include <array>
#include <vector>

namespace AshEditor
{
	namespace
	{
		// Keep the add/remove button flow identical across component sections.
		auto draw_add_component_button(AshEngine::UIContext& ui, const char* label) -> bool
		{
			return ui.button(label);
		}

		auto draw_remove_component_button(AshEngine::UIContext& ui, const char* label) -> bool
		{
			return ui.button(label);
		}

		bool edit_name_component(AshEngine::UIContext& ui, const char* label, AshEngine::NameComponent& component)
		{
			return ui.input_text(label, component.value);
		}

		bool edit_mesh_path(AshEngine::UIContext& ui, AshEngine::MeshComponent& component)
		{
			return ui.input_text("Asset Path", component.asset_path);
		}

		bool edit_vec3(
			AshEngine::UIContext& ui,
			const char* label,
			glm::vec3& value,
			float speed,
			float min_value,
			float max_value,
			const char* format = "%.3f")
		{
			return ui.drag_float3(label, &value.x, speed, min_value, max_value, format);
		}

		bool edit_color3(AshEngine::UIContext& ui, const char* label, glm::vec3& value)
		{
			return ui.color_edit3(label, &value.x);
		}

		bool camera_components_equal(const AshEngine::CameraComponent& lhs, const AshEngine::CameraComponent& rhs)
		{
			return
				lhs.primary == rhs.primary &&
				lhs.projection == rhs.projection &&
				lhs.fov_y_degrees == rhs.fov_y_degrees &&
				lhs.near_plane == rhs.near_plane &&
				lhs.far_plane == rhs.far_plane &&
				lhs.orthographic_height == rhs.orthographic_height;
		}

		bool light_components_equal(const AshEngine::LightComponent& lhs, const AshEngine::LightComponent& rhs)
		{
			return
				lhs.type == rhs.type &&
				lhs.color == rhs.color &&
				lhs.intensity == rhs.intensity &&
				lhs.range == rhs.range &&
				lhs.inner_cone_angle_degrees == rhs.inner_cone_angle_degrees &&
				lhs.outer_cone_angle_degrees == rhs.outer_cone_angle_degrees;
		}

		bool mesh_components_equal(const AshEngine::MeshComponent& lhs, const AshEngine::MeshComponent& rhs)
		{
			return
				lhs.asset_path == rhs.asset_path &&
				lhs.mesh_index == rhs.mesh_index &&
				lhs.visible == rhs.visible &&
				lhs.mobility == rhs.mobility &&
				lhs.layer_mask == rhs.layer_mask;
		}

		template<typename Component>
		bool optional_components_equal(
			const std::optional<Component>& lhs,
			const std::optional<Component>& rhs,
			bool (*equals_fn)(const Component&, const Component&))
		{
			if (lhs.has_value() != rhs.has_value())
			{
				return false;
			}

			if (!lhs.has_value())
			{
				return true;
			}

			return equals_fn(*lhs, *rhs);
		}

		std::optional<AshEngine::CameraComponent> get_camera_component_value(const AshEngine::Entity& entity)
		{
			return entity.has_camera_component()
				? std::optional<AshEngine::CameraComponent>{ entity.get_camera_component() }
				: std::nullopt;
		}

		std::optional<AshEngine::LightComponent> get_light_component_value(const AshEngine::Entity& entity)
		{
			return entity.has_light_component()
				? std::optional<AshEngine::LightComponent>{ entity.get_light_component() }
				: std::nullopt;
		}

		std::optional<AshEngine::MeshComponent> get_mesh_component_value(const AshEngine::Entity& entity)
		{
			return entity.has_mesh_component()
				? std::optional<AshEngine::MeshComponent>{ entity.get_mesh_component() }
				: std::nullopt;
		}

		void draw_selection_summary(AshEngine::UIContext& ui, const EditorSelection& selection)
		{
			ui.text("Selection: %s", selection.label.c_str());
			ui.text("Id: %llu", static_cast<unsigned long long>(selection.id));
			ui.separator();
		}

		void draw_hierarchy_section(AshEngine::UIContext& ui, const AshEngine::Entity& entity)
		{
			if (!ui.collapsing_header("Hierarchy", AshEngine::UITreeNodeFlagBits::DefaultOpen))
			{
				return;
			}

			const AshEngine::Entity parent = entity.get_parent();
			ui.text("Parent Id: %llu", static_cast<unsigned long long>(parent.get_id()));
			ui.text("Children: %u", static_cast<unsigned int>(entity.get_children().size()));
		}

		void draw_asset_inspector(EditorContext& context, AshEngine::UIContext& ui, const EditorSelection& selection)
		{
			const AshEngine::AssetInfo* asset = context.asset_database_service->find_by_id(selection.id);
			if (!asset)
			{
				ui.text_unformatted("Selected asset no longer exists.");
				return;
			}

			ui.text("Type: %s", AssetDatabaseService::get_type_label(asset->type));
			ui.text("Path: %s", asset->relative_path.generic_string().c_str());
			ui.text("Parent: %s", asset->parent_path.generic_string().c_str());
			ui.text("Directory: %s", asset->is_directory ? "true" : "false");
			ui.text("File Size: %llu", static_cast<unsigned long long>(asset->file_size));
			ui.text("Load State: %s", AssetDatabaseService::get_load_state_label(context.asset_database_service->get_load_state(asset->id)));
		}
	}

	InspectorPanel::InspectorPanel()
		: EditorPanel("inspector", "Inspector")
	{
	}

	void InspectorPanel::reset_entity_drafts()
	{
		m_identityDraft = {};
		m_transformDraft = {};
		m_cameraDraft = {};
		m_lightDraft = {};
		m_meshDraft = {};
	}

	void InspectorPanel::sync_entity_drafts(const AshEngine::Entity& entity)
	{
		if (m_identityDraft.entity_id != entity.get_id())
		{
			m_identityDraft.entity_id = entity.get_id();
			m_identityDraft.original_name = entity.get_name();
			m_identityDraft.current_name = m_identityDraft.original_name;
		}
		else if (m_identityDraft.current_name == m_identityDraft.original_name)
		{
			m_identityDraft.original_name = entity.get_name();
			m_identityDraft.current_name = m_identityDraft.original_name;
		}

		const AshEngine::TransformComponent live_transform = entity.get_transform_component();
		if (m_transformDraft.entity_id != entity.get_id())
		{
			m_transformDraft.entity_id = entity.get_id();
			m_transformDraft.original_value = live_transform;
			m_transformDraft.current_value = live_transform;
		}
		else if (m_transformDraft.current_value.position == m_transformDraft.original_value.position &&
			m_transformDraft.current_value.rotation_euler_degrees == m_transformDraft.original_value.rotation_euler_degrees &&
			m_transformDraft.current_value.scale == m_transformDraft.original_value.scale)
		{
			m_transformDraft.original_value = live_transform;
			m_transformDraft.current_value = live_transform;
		}
	}

	void InspectorPanel::sync_camera_draft(const AshEngine::Entity& entity)
	{
		const std::optional<AshEngine::CameraComponent> live_value = get_camera_component_value(entity);
		if (m_cameraDraft.entity_id != entity.get_id())
		{
			m_cameraDraft.entity_id = entity.get_id();
			m_cameraDraft.original_value = live_value;
			m_cameraDraft.current_value = live_value;
		}
		else if (optional_components_equal(m_cameraDraft.current_value, m_cameraDraft.original_value, &camera_components_equal))
		{
			m_cameraDraft.original_value = live_value;
			m_cameraDraft.current_value = live_value;
		}
	}

	void InspectorPanel::sync_light_draft(const AshEngine::Entity& entity)
	{
		const std::optional<AshEngine::LightComponent> live_value = get_light_component_value(entity);
		if (m_lightDraft.entity_id != entity.get_id())
		{
			m_lightDraft.entity_id = entity.get_id();
			m_lightDraft.original_value = live_value;
			m_lightDraft.current_value = live_value;
		}
		else if (optional_components_equal(m_lightDraft.current_value, m_lightDraft.original_value, &light_components_equal))
		{
			m_lightDraft.original_value = live_value;
			m_lightDraft.current_value = live_value;
		}
	}

	void InspectorPanel::sync_mesh_draft(const AshEngine::Entity& entity)
	{
		const std::optional<AshEngine::MeshComponent> live_value = get_mesh_component_value(entity);
		if (m_meshDraft.entity_id != entity.get_id())
		{
			m_meshDraft.entity_id = entity.get_id();
			m_meshDraft.original_value = live_value;
			m_meshDraft.current_value = live_value;
		}
		else if (optional_components_equal(m_meshDraft.current_value, m_meshDraft.original_value, &mesh_components_equal))
		{
			m_meshDraft.original_value = live_value;
			m_meshDraft.current_value = live_value;
		}
	}

	bool InspectorPanel::has_pending_identity_changes() const
	{
		return m_identityDraft.current_name != m_identityDraft.original_name;
	}

	bool InspectorPanel::has_pending_transform_changes() const
	{
		return m_transformDraft.current_value.position != m_transformDraft.original_value.position ||
			m_transformDraft.current_value.rotation_euler_degrees != m_transformDraft.original_value.rotation_euler_degrees ||
			m_transformDraft.current_value.scale != m_transformDraft.original_value.scale;
	}

	bool InspectorPanel::has_pending_camera_changes() const
	{
		return !optional_components_equal(m_cameraDraft.current_value, m_cameraDraft.original_value, &camera_components_equal);
	}

	bool InspectorPanel::has_pending_light_changes() const
	{
		return !optional_components_equal(m_lightDraft.current_value, m_lightDraft.original_value, &light_components_equal);
	}

	bool InspectorPanel::has_pending_mesh_changes() const
	{
		return !optional_components_equal(m_meshDraft.current_value, m_meshDraft.original_value, &mesh_components_equal);
	}

	void InspectorPanel::draw_component_sections(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity)
	{
		draw_identity_section(context, ui, entity);
		ui.separator();
		draw_transform_section(context, ui, entity);
		ui.separator();
		draw_camera_section(context, ui, entity);
		ui.separator();
		draw_light_section(context, ui, entity);
		ui.separator();
		draw_mesh_section(context, ui, entity);
		ui.separator();
	}

	void InspectorPanel::draw_pending_change_hint(AshEngine::UIContext& ui, const char* label)
	{
		ui.text("%s", label);
	}

	void InspectorPanel::draw_apply_revert_row(
		AshEngine::UIContext& ui,
		const char* apply_label,
		const char* revert_label,
		bool can_apply,
		bool has_pending_changes,
		bool& apply_clicked,
		bool& revert_clicked)
	{
		apply_clicked = false;
		revert_clicked = false;

		ui.begin_disabled(!can_apply);
		apply_clicked = ui.button(apply_label);
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(!has_pending_changes);
		revert_clicked = ui.button(revert_label);
		ui.end_disabled();
	}

	void InspectorPanel::draw_identity_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity)
	{
		sync_entity_drafts(entity);
		const bool has_pending_changes = has_pending_identity_changes();
		const char* section_label = has_pending_changes ? "Identity *" : "Identity";
		if (!ui.collapsing_header(section_label, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		ui.input_text("Name", m_identityDraft.current_name);

		if (has_pending_changes)
		{
			draw_pending_change_hint(ui, "Pending changes. Apply or revert to update the entity.");
		}

		const bool can_apply = has_pending_changes && !m_identityDraft.current_name.empty();
		bool apply_clicked = false;
		bool revert_clicked = false;
		draw_apply_revert_row(ui, "Apply Name", "Revert Name", can_apply, has_pending_changes, apply_clicked, revert_clicked);
		if (apply_clicked)
		{
			bool applied = false;
			if (context.undo_redo_service)
			{
				applied = context.undo_redo_service->execute(
					std::make_unique<RenameEntityCommand>(
						entity.get_id(),
						m_identityDraft.original_name,
						m_identityDraft.current_name),
					context);
			}
			else
			{
				applied = entity.set_name(m_identityDraft.current_name);
			}

			if (applied)
			{
				m_identityDraft.original_name = m_identityDraft.current_name;
			}
		}
		if (revert_clicked)
		{
			m_identityDraft.current_name = m_identityDraft.original_name;
		}
	}

	void InspectorPanel::draw_transform_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity)
	{
		sync_entity_drafts(entity);
		const bool has_pending_changes = has_pending_transform_changes();
		const char* section_label = has_pending_changes ? "Transform *" : "Transform";
		if (!ui.collapsing_header(section_label, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		AshEngine::TransformComponent& transform = m_transformDraft.current_value;
		edit_vec3(ui, "Position", transform.position, 0.1f, 0.0f, 0.0f);
		edit_vec3(ui, "Rotation", transform.rotation_euler_degrees, 0.5f, 0.0f, 0.0f);
		edit_vec3(ui, "Scale", transform.scale, 0.05f, 0.0f, 0.0f);

		if (has_pending_changes)
		{
			draw_pending_change_hint(ui, "Pending changes. Apply or revert to update the entity.");
		}

		bool apply_clicked = false;
		bool revert_clicked = false;
		draw_apply_revert_row(ui, "Apply Transform", "Revert Transform", has_pending_changes, has_pending_changes, apply_clicked, revert_clicked);
		if (apply_clicked)
		{
			bool applied = false;
			if (context.undo_redo_service)
			{
				applied = context.undo_redo_service->execute(
					std::make_unique<TransformEntityCommand>(
						entity.get_id(),
						m_transformDraft.original_value,
						m_transformDraft.current_value),
					context);
			}
			else
			{
				applied = entity.set_transform_component(m_transformDraft.current_value);
			}

			if (applied)
			{
				m_transformDraft.original_value = m_transformDraft.current_value;
			}
		}
		if (revert_clicked)
		{
			m_transformDraft.current_value = m_transformDraft.original_value;
		}
	}

	void InspectorPanel::draw_camera_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity)
	{
		sync_camera_draft(entity);
		const bool has_pending_changes = has_pending_camera_changes();
		if (!m_cameraDraft.current_value.has_value() && !has_pending_changes)
		{
			if (draw_add_component_button(ui, "Add Camera"))
			{
				m_cameraDraft.current_value = AshEngine::CameraComponent{};
			}
			return;
		}

		const char* section_label = has_pending_changes ? "Camera *" : "Camera";
		if (!ui.collapsing_header(section_label, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		if (m_cameraDraft.current_value.has_value())
		{
			AshEngine::CameraComponent& camera = *m_cameraDraft.current_value;
			ui.checkbox("Primary", camera.primary);

			int projection = static_cast<int>(camera.projection);
			const std::vector<const char*> projection_labels{ "Perspective", "Orthographic" };
			if (ui.combo("Projection", projection, projection_labels))
			{
				camera.projection = static_cast<AshEngine::CameraProjectionType>(projection);
			}

			ui.drag_float("FOV Y", camera.fov_y_degrees, 0.1f, 1.0f, 179.0f);
			ui.drag_float("Near Plane", camera.near_plane, 0.01f, 0.001f, camera.far_plane);
			ui.drag_float("Far Plane", camera.far_plane, 1.0f, camera.near_plane, 10000.0f);
			ui.drag_float("Ortho Height", camera.orthographic_height, 0.1f, 0.1f, 1000.0f);

			if (draw_remove_component_button(ui, "Remove Camera"))
			{
				m_cameraDraft.current_value.reset();
			}
		}
		else
		{
			ui.text_unformatted("Camera component will be removed after Apply.");
		}

		if (has_pending_changes)
		{
			draw_pending_change_hint(ui, "Pending changes. Apply or revert to update the entity.");
		}

		bool apply_clicked = false;
		bool revert_clicked = false;
		draw_apply_revert_row(ui, "Apply Camera", "Revert Camera", has_pending_changes, has_pending_changes, apply_clicked, revert_clicked);
		if (apply_clicked)
		{
			bool applied = false;
			if (context.undo_redo_service)
			{
				applied = context.undo_redo_service->execute(
					std::make_unique<SetCameraComponentCommand>(
						entity.get_id(),
						m_cameraDraft.original_value,
						m_cameraDraft.current_value),
					context);
			}
			else if (m_cameraDraft.current_value.has_value())
			{
				applied = entity.has_camera_component()
					? entity.set_camera_component(*m_cameraDraft.current_value)
					: entity.add_camera_component(*m_cameraDraft.current_value);
			}
			else
			{
				applied = entity.has_camera_component() && entity.remove_camera_component();
			}

			if (applied)
			{
				m_cameraDraft.original_value = m_cameraDraft.current_value;
			}
		}
		if (revert_clicked)
		{
			m_cameraDraft.current_value = m_cameraDraft.original_value;
		}
	}

	void InspectorPanel::draw_light_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity)
	{
		sync_light_draft(entity);
		const bool has_pending_changes = has_pending_light_changes();
		if (!m_lightDraft.current_value.has_value() && !has_pending_changes)
		{
			if (draw_add_component_button(ui, "Add Light"))
			{
				m_lightDraft.current_value = AshEngine::LightComponent{};
			}
			return;
		}

		const char* section_label = has_pending_changes ? "Light *" : "Light";
		if (!ui.collapsing_header(section_label, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		if (m_lightDraft.current_value.has_value())
		{
			AshEngine::LightComponent& light = *m_lightDraft.current_value;
			int light_type = static_cast<int>(light.type);
			const std::vector<const char*> light_labels{ "Directional", "Point", "Spot" };
			if (ui.combo("Light Type", light_type, light_labels))
			{
				light.type = static_cast<AshEngine::LightType>(light_type);
			}

			edit_color3(ui, "Color", light.color);
			ui.drag_float("Intensity", light.intensity, 0.05f, 0.0f, 100.0f);
			ui.drag_float("Range", light.range, 0.1f, 0.0f, 1000.0f);
			ui.drag_float("Inner Cone", light.inner_cone_angle_degrees, 0.1f, 0.0f, 180.0f);
			ui.drag_float("Outer Cone", light.outer_cone_angle_degrees, 0.1f, 0.0f, 180.0f);

			if (draw_remove_component_button(ui, "Remove Light"))
			{
				m_lightDraft.current_value.reset();
			}
		}
		else
		{
			ui.text_unformatted("Light component will be removed after Apply.");
		}

		if (has_pending_changes)
		{
			draw_pending_change_hint(ui, "Pending changes. Apply or revert to update the entity.");
		}

		bool apply_clicked = false;
		bool revert_clicked = false;
		draw_apply_revert_row(ui, "Apply Light", "Revert Light", has_pending_changes, has_pending_changes, apply_clicked, revert_clicked);
		if (apply_clicked)
		{
			bool applied = false;
			if (context.undo_redo_service)
			{
				applied = context.undo_redo_service->execute(
					std::make_unique<SetLightComponentCommand>(
						entity.get_id(),
						m_lightDraft.original_value,
						m_lightDraft.current_value),
					context);
			}
			else if (m_lightDraft.current_value.has_value())
			{
				applied = entity.has_light_component()
					? entity.set_light_component(*m_lightDraft.current_value)
					: entity.add_light_component(*m_lightDraft.current_value);
			}
			else
			{
				applied = entity.has_light_component() && entity.remove_light_component();
			}

			if (applied)
			{
				m_lightDraft.original_value = m_lightDraft.current_value;
			}
		}
		if (revert_clicked)
		{
			m_lightDraft.current_value = m_lightDraft.original_value;
		}
	}

	void InspectorPanel::draw_mesh_section(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity)
	{
		sync_mesh_draft(entity);
		const bool has_pending_changes = has_pending_mesh_changes();
		if (!m_meshDraft.current_value.has_value() && !has_pending_changes)
		{
			if (draw_add_component_button(ui, "Add Mesh"))
			{
				m_meshDraft.current_value = AshEngine::MeshComponent{};
			}
			return;
		}

		const char* section_label = has_pending_changes ? "Mesh *" : "Mesh";
		if (!ui.collapsing_header(section_label, AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		if (m_meshDraft.current_value.has_value())
		{
			AshEngine::MeshComponent& mesh = *m_meshDraft.current_value;
			edit_mesh_path(ui, mesh);
			ui.checkbox("Visible", mesh.visible);

			if (draw_remove_component_button(ui, "Remove Mesh"))
			{
				m_meshDraft.current_value.reset();
			}
		}
		else
		{
			ui.text_unformatted("Mesh component will be removed after Apply.");
		}

		if (has_pending_changes)
		{
			draw_pending_change_hint(ui, "Pending changes. Apply or revert to update the entity.");
		}

		bool apply_clicked = false;
		bool revert_clicked = false;
		draw_apply_revert_row(ui, "Apply Mesh", "Revert Mesh", has_pending_changes, has_pending_changes, apply_clicked, revert_clicked);
		if (apply_clicked)
		{
			bool applied = false;
			if (context.undo_redo_service)
			{
				applied = context.undo_redo_service->execute(
					std::make_unique<SetMeshComponentCommand>(
						entity.get_id(),
						m_meshDraft.original_value,
						m_meshDraft.current_value),
					context);
			}
			else if (m_meshDraft.current_value.has_value())
			{
				applied = entity.has_mesh_component()
					? entity.set_mesh_component(*m_meshDraft.current_value)
					: entity.add_mesh_component(*m_meshDraft.current_value);
			}
			else
			{
				applied = entity.has_mesh_component() && entity.remove_mesh_component();
			}

			if (applied)
			{
				m_meshDraft.original_value = m_meshDraft.current_value;
			}
		}
		if (revert_clicked)
		{
			m_meshDraft.current_value = m_meshDraft.original_value;
		}
	}

	void InspectorPanel::draw_entity_inspector(EditorContext& context, AshEngine::UIContext& ui, AshEngine::Entity entity)
	{
		if (!entity.is_valid())
		{
			ui.text_unformatted("Selected entity no longer exists.");
			return;
		}

		draw_component_sections(context, ui, entity);
		draw_hierarchy_section(ui, entity);
	}

	void InspectorPanel::on_attach(EditorContext& context)
	{
		(void)context;
		HLogInfo("InspectorPanel attached.");
	}

	void InspectorPanel::on_gui(EditorContext& context)
	{
		if (!begin_panel_window(context))
		{
			end_panel_window(context);
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		if (!context.selection_service || !context.selection_service->has_selection())
		{
			reset_entity_drafts();
			ui.text_unformatted("Nothing selected.");
			end_panel_window(context);
			return;
		}

		const EditorSelection& selection = context.selection_service->get_selection();
		draw_selection_summary(ui, selection);

		if (selection.kind == EditorSelectionKind::Entity && context.scene_service)
		{
			AshEngine::Entity entity = context.scene_service->find_entity(selection.id);
			if (!entity.is_valid())
			{
				reset_entity_drafts();
			}
			draw_entity_inspector(context, ui, entity);
		}
		else if (selection.kind == EditorSelectionKind::Asset && context.asset_database_service)
		{
			reset_entity_drafts();
			draw_asset_inspector(context, ui, selection);
		}
		else
		{
			reset_entity_drafts();
			ui.text_unformatted("Inspector adapter for this selection type is pending.");
		}

		end_panel_window(context);
	}
}

#include "Panels/SceneHierarchyPanel.h"
#include "Base/hlog.h"
#include "Core/EditorCommand.h"
#include "Core/EntityCommands.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/CommandService.h"
#include "Services/EditorIconService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include "imgui.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UIColor k_sceneHierarchyAccentColor{ 0.67f, 0.78f, 0.92f, 1.0f };
		constexpr AshEngine::UIColor k_sceneHierarchyMutedColor{ 0.67f, 0.70f, 0.76f, 1.0f };
		constexpr const char* k_sceneEntityContextPopupId = "SceneHierarchyEntityContextMenu";
		constexpr const char* k_sceneContentContextPopupId = "SceneHierarchyContentContextMenu";

		auto get_action_shortcut(const CommandService* command_service, const char* action_id) -> const char*
		{
			if (!command_service || !action_id)
			{
				return nullptr;
			}

			const EditorAction* action = command_service->find_action(action_id);
			return action && !action->shortcut.empty() ? action->shortcut.c_str() : nullptr;
		}

		auto get_selected_entity_id(const EditorContext& context) -> EntityId
		{
			return
				context.selection_service &&
					context.selection_service->get_selection().kind == EditorSelectionKind::Entity
				? context.selection_service->get_selection().id
				: 0;
		}

		EntityId get_entity_parent_id(const AshEngine::Entity& entity)
		{
			const AshEngine::Entity parent = entity.get_parent();
			return parent.is_valid() ? parent.get_id() : 0;
		}

		uint32_t get_entity_sibling_index(const AshEngine::Entity& entity, const SceneService& scene_service)
		{
			return entity.is_valid() ? scene_service.get_entity_sibling_index(entity.get_id()) : 0u;
		}

		uint32_t get_parent_child_count(const SceneService& scene_service, EntityId parent_id)
		{
			if (parent_id == 0)
			{
				return static_cast<uint32_t>(scene_service.get_active_scene().get_root_entities().size());
			}

			const AshEngine::Entity parent = scene_service.find_entity(parent_id);
			return parent.is_valid() ? static_cast<uint32_t>(parent.get_children().size()) : 0u;
		}

		uint32_t get_reparent_insert_index_max(
			const SceneService& scene_service,
			EntityId entity_id,
			EntityId current_parent_id,
			EntityId target_parent_id)
		{
			uint32_t child_count = get_parent_child_count(scene_service, target_parent_id);
			if (entity_id != 0 && current_parent_id == target_parent_id && child_count > 0)
			{
				--child_count;
			}
			return child_count;
		}

		EditorIconId get_entity_icon_id(const AshEngine::Entity& entity)
		{
			if (entity.has_camera_component())
			{
				return EditorIconId::EntityCamera;
			}
			if (entity.has_light_component())
			{
				const AshEngine::LightComponent& light = entity.get_light_component();
				switch (light.type)
				{
				case AshEngine::LightType::Directional:
					return EditorIconId::EntityLightDirectional;
				case AshEngine::LightType::Point:
					return EditorIconId::EntityLightPoint;
				case AshEngine::LightType::Spot:
					return EditorIconId::EntityLightSpot;
				default:
					break;
				}
			}
			if (entity.has_mesh_component())
			{
				return EditorIconId::EntityMesh;
			}

			return entity.get_parent().is_valid() ? EditorIconId::EntityActor : EditorIconId::EntityScene;
		}

		constexpr const char* k_scene_hierarchy_drag_payload_type = "ASH_EDITOR_SCENE_ENTITY";

		struct SceneHierarchyDropRequest
		{
			EntityId entity_id = 0;
			EntityId new_parent_id = 0;
			uint32_t sibling_index = AshEngine::k_scene_append_sibling_index;
			EditorTreeDropVisual visual = EditorTreeDropVisual::None;
			bool valid = false;
		};

		struct SceneEntityDropValidationData
		{
			const SceneService* scene_service = nullptr;
			EntityId target_entity_id = 0;
			bool root_append_slot = false;
		};

		auto decode_dragged_entity_id(const ImGuiPayload* payload) -> EntityId
		{
			return
				payload &&
				payload->Data &&
				payload->DataSize == static_cast<int32_t>(sizeof(EntityId))
				? *static_cast<const EntityId*>(payload->Data)
				: 0;
		}

		auto adjust_insert_index_for_move(
			EntityId current_parent_id,
			uint32_t current_sibling_index,
			EntityId target_parent_id,
			uint32_t insert_index) -> uint32_t
		{
			if (current_parent_id == target_parent_id && current_sibling_index < insert_index)
			{
				return insert_index - 1u;
			}
			return insert_index;
		}

		auto build_drop_request_for_target(
			const SceneService& scene_service,
			EntityId dragged_entity_id,
			const AshEngine::Entity& target_entity,
			EditorTreeDropVisual visual) -> SceneHierarchyDropRequest
		{
			if (dragged_entity_id == 0 || !target_entity.is_valid() || dragged_entity_id == target_entity.get_id())
			{
				return {};
			}

			const AshEngine::Entity dragged_entity = scene_service.find_entity(dragged_entity_id);
			if (!dragged_entity.is_valid())
			{
				return {};
			}

			const EntityId current_parent_id = get_entity_parent_id(dragged_entity);
			const uint32_t current_sibling_index = scene_service.get_entity_sibling_index(dragged_entity_id);
			const EntityId target_parent_id = get_entity_parent_id(target_entity);
			const uint32_t target_sibling_index = scene_service.get_entity_sibling_index(target_entity.get_id());

			SceneHierarchyDropRequest request{};
			request.entity_id = dragged_entity_id;

			if (visual == EditorTreeDropVisual::Before || visual == EditorTreeDropVisual::After)
			{
				if (!scene_service.can_reparent_entity(dragged_entity_id, target_parent_id))
				{
					return {};
				}

				const uint32_t raw_insert_index = target_sibling_index + (visual == EditorTreeDropVisual::After ? 1u : 0u);
				request.new_parent_id = target_parent_id;
				request.sibling_index = adjust_insert_index_for_move(
					current_parent_id,
					current_sibling_index,
					target_parent_id,
					raw_insert_index);
				request.visual = visual;
			}
			else if (visual == EditorTreeDropVisual::Into)
			{
				if (!scene_service.can_reparent_entity(dragged_entity_id, target_entity.get_id()))
				{
					return {};
				}

				request.new_parent_id = target_entity.get_id();
				request.sibling_index = get_reparent_insert_index_max(
					scene_service,
					dragged_entity_id,
					current_parent_id,
					target_entity.get_id());
				request.visual = EditorTreeDropVisual::Into;
			}
			else
			{
				return {};
			}

			request.valid =
				current_parent_id != request.new_parent_id ||
				current_sibling_index != request.sibling_index;
			return request;
		}

		auto build_root_append_drop_request(
			const SceneService& scene_service,
			EntityId dragged_entity_id) -> SceneHierarchyDropRequest
		{
			if (dragged_entity_id == 0)
			{
				return {};
			}

			const AshEngine::Entity dragged_entity = scene_service.find_entity(dragged_entity_id);
			if (!dragged_entity.is_valid())
			{
				return {};
			}

			const EntityId current_parent_id = get_entity_parent_id(dragged_entity);
			const uint32_t current_sibling_index = scene_service.get_entity_sibling_index(dragged_entity_id);
			SceneHierarchyDropRequest request{};
			request.entity_id = dragged_entity_id;
			request.new_parent_id = 0;
			request.sibling_index = get_reparent_insert_index_max(scene_service, dragged_entity_id, current_parent_id, 0);
			request.visual = EditorTreeDropVisual::Before;
			request.valid = current_parent_id != 0 || current_sibling_index != request.sibling_index;
			return request;
		}

		auto validate_scene_drop_target(
			const ImGuiPayload* payload,
			EditorTreeDropVisual visual,
			void* user_data) -> bool
		{
			const auto* validation_data = static_cast<const SceneEntityDropValidationData*>(user_data);
			if (!validation_data || !validation_data->scene_service)
			{
				return false;
			}

			const EntityId dragged_entity_id = decode_dragged_entity_id(payload);
			if (validation_data->root_append_slot)
			{
				return build_root_append_drop_request(*validation_data->scene_service, dragged_entity_id).valid;
			}

			const AshEngine::Entity target_entity = validation_data->scene_service->find_entity(validation_data->target_entity_id);
			return build_drop_request_for_target(*validation_data->scene_service, dragged_entity_id, target_entity, visual).valid;
		}

		auto make_scene_tree_style() -> EditorTreeWidgetStyle
		{
			EditorTreeWidgetStyle style{};
			style.row_height = 24.0f;
			style.indent_spacing = 12.0f;
			style.icon_size = 16.0f;
			style.icon_text_spacing = 4.0f;
			style.row_padding_y = 5.0f;
			style.row_spacing_y = 3.0f;
			style.connector_horizontal_padding = 3.0f;
			style.guide_line_padding_y = 0.0f;
			style.guide_line_color = { 0.46f, 0.49f, 0.54f, 0.55f };
			style.auto_expand_hover_delay_seconds = 0.45f;
			style.row_hover_fill_color = { 0.28f, 0.39f, 0.49f, 0.16f };
			style.row_hover_outline_color = { 0.38f, 0.56f, 0.74f, 0.38f };
			style.row_selected_fill_color = { 0.32f, 0.47f, 0.60f, 0.28f };
			style.row_selected_outline_color = { 0.43f, 0.64f, 0.85f, 0.84f };
			return style;
		}

		void draw_scene_summary(AshEngine::UIContext& ui, const AshEngine::Scene& scene, EntityId selected_entity_id)
		{
			ui.text_colored(k_sceneHierarchyAccentColor, "%s", scene.get_name().c_str());
			ui.text_colored(
				k_sceneHierarchyMutedColor,
				"%u entities | %u roots",
				scene.get_entity_count(),
				static_cast<unsigned int>(scene.get_root_entities().size()));
			if (selected_entity_id != 0)
			{
				ui.same_line();
				ui.text_colored(k_sceneHierarchyMutedColor, "| Selected %llu", static_cast<unsigned long long>(selected_entity_id));
			}
			ui.separator();
		}

		void draw_empty_scene_state(AshEngine::UIContext& ui)
		{
			ui.text_colored(k_sceneHierarchyMutedColor, "Scene is empty.");
			ui.text_unformatted("Create a root entity to start building the scene.");
		}

		void append_reparent_candidates(
			const SceneService& scene_service,
			EntityId entity_id,
			const AshEngine::Entity& entity,
			std::vector<EntityId>& out_ids,
			std::vector<std::string>& out_labels,
			uint32_t depth)
		{
			if (entity.get_id() != entity_id && scene_service.can_reparent_entity(entity_id, entity.get_id()))
			{
				out_ids.push_back(entity.get_id());
				out_labels.push_back(std::string(depth * 2, ' ') + entity.get_name());
			}

			for (const AshEngine::Entity& child : entity.get_children())
			{
				append_reparent_candidates(scene_service, entity_id, child, out_ids, out_labels, depth + 1);
			}
		}

	}

	SceneHierarchyPanel::SceneHierarchyPanel()
		: EditorPanel("scene_hierarchy", "Scene Hierarchy")
	{
	}

	void SceneHierarchyPanel::on_attach(EditorContext& context)
	{
		(void)context;
		HLogInfo("SceneHierarchyPanel attached.");
	}

	void SceneHierarchyPanel::execute_create_root(EditorContext& context)
	{
		create_entity(context, 0);
	}

	void SceneHierarchyPanel::execute_create_child_from_selection(EditorContext& context)
	{
		create_entity(context, get_selected_entity_id(context));
	}

	void SceneHierarchyPanel::request_rename_selected(EditorContext& context)
	{
		begin_rename_selected_entity(context);
	}

	void SceneHierarchyPanel::request_reparent_selected(EditorContext& context)
	{
		begin_reparent_selected_entity(context);
	}

	void SceneHierarchyPanel::request_delete_selected(EditorContext& context)
	{
		begin_delete_selected_entity(context);
	}

	void SceneHierarchyPanel::begin_rename_selected_entity(EditorContext& context)
	{
		if (!context.scene_service)
		{
			return;
		}

		const EntityId selected_entity_id = get_selected_entity_id(context);
		if (selected_entity_id == 0)
		{
			return;
		}

		const AshEngine::Entity entity = context.scene_service->find_entity(selected_entity_id);
		if (!entity.is_valid())
		{
			return;
		}

		m_pendingRenameEntityId = selected_entity_id;
		m_pendingRenameValue = entity.get_name();
		if (context.ui_context)
		{
			context.ui_context->open_popup("Rename Entity");
		}
	}

	void SceneHierarchyPanel::begin_reparent_selected_entity(EditorContext& context)
	{
		if (!context.scene_service)
		{
			return;
		}

		const EntityId selected_entity_id = get_selected_entity_id(context);
		if (selected_entity_id == 0)
		{
			return;
		}

		const AshEngine::Entity entity = context.scene_service->find_entity(selected_entity_id);
		if (!entity.is_valid())
		{
			return;
		}

		m_pendingReparentEntityId = selected_entity_id;
		m_pendingReparentParentIds.clear();
		m_pendingReparentParentLabels.clear();
		m_pendingReparentParentIds.push_back(0);
		m_pendingReparentParentLabels.push_back("<Root>");

		for (const AshEngine::Entity& root : context.scene_service->get_active_scene().get_root_entities())
		{
			append_reparent_candidates(*context.scene_service, selected_entity_id, root, m_pendingReparentParentIds, m_pendingReparentParentLabels, 0);
		}

		const EntityId current_parent_id = entity.get_parent().is_valid() ? entity.get_parent().get_id() : 0;
		m_pendingReparentInsertIndex = static_cast<int32_t>(get_entity_sibling_index(entity, *context.scene_service));
		m_pendingReparentIndex = 0;
		for (size_t index = 0; index < m_pendingReparentParentIds.size(); ++index)
		{
			if (m_pendingReparentParentIds[index] == current_parent_id)
			{
				m_pendingReparentIndex = static_cast<int32_t>(index);
				break;
			}
		}

		if (context.ui_context)
		{
			context.ui_context->open_popup("Reparent Entity");
		}
	}

	void SceneHierarchyPanel::begin_delete_selected_entity(EditorContext& context)
	{
		if (!context.scene_service)
		{
			return;
		}

		const EntityId selected_entity_id = get_selected_entity_id(context);
		if (selected_entity_id == 0)
		{
			return;
		}

		const AshEngine::Entity entity = context.scene_service->find_entity(selected_entity_id);
		if (!entity.is_valid())
		{
			return;
		}

		m_pendingDeleteEntityId = selected_entity_id;
		m_pendingDeleteEntityName = entity.get_name();
		if (context.ui_context)
		{
			context.ui_context->open_popup("Delete Entity");
		}
	}

	void SceneHierarchyPanel::create_entity(EditorContext& context, EntityId parent_id)
	{
		if (!context.scene_service || !context.undo_redo_service)
		{
			return;
		}

		AshEngine::Scene& scene = context.scene_service->get_active_scene();
		const std::string entity_name = "Entity " + std::to_string(scene.get_entity_count() + 1);
		context.undo_redo_service->execute(std::make_unique<CreateEntityCommand>(entity_name, parent_id), context);
	}

	void SceneHierarchyPanel::destroy_entity(EditorContext& context, EntityId entity_id)
	{
		if (!context.scene_service || !context.selection_service || !context.undo_redo_service)
		{
			return;
		}

		if (entity_id == 0)
		{
			return;
		}

		context.undo_redo_service->execute(std::make_unique<DeleteEntityCommand>(entity_id), context);
	}

	void SceneHierarchyPanel::draw_toolbar(EditorContext& context, EntityId selected_entity_id)
	{
		AshEngine::UIContext& ui = *context.ui_context;

		ui.text_colored(k_sceneHierarchyMutedColor, "Actions");
		ui.same_line();
		if (ui.button("Add Root"))
		{
			create_entity(context, 0);
		}
		ui.same_line();
		ui.begin_disabled(selected_entity_id == 0);
		if (ui.button("Add Child"))
		{
			create_entity(context, selected_entity_id);
		}
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(selected_entity_id == 0);
		if (ui.button("Rename"))
		{
			begin_rename_selected_entity(context);
		}
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(selected_entity_id == 0);
		if (ui.button("Reparent"))
		{
			begin_reparent_selected_entity(context);
		}
		ui.end_disabled();
		ui.same_line();
		ui.begin_disabled(selected_entity_id == 0);
		if (ui.button("Delete"))
		{
			begin_delete_selected_entity(context);
		}
		ui.end_disabled();
	}

	bool SceneHierarchyPanel::is_scene_entity_drag_active() const
	{
		const ImGuiPayload* payload = ImGui::GetDragDropPayload();
		return payload && payload->IsDataType(k_scene_hierarchy_drag_payload_type);
	}

	void SceneHierarchyPanel::handle_root_append_drop_target(
		EditorTreeWidget& tree_widget,
		EditorContext& context,
		bool dragging_scene_entity)
	{
		if (!context.scene_service)
		{
			return;
		}

		SceneEntityDropValidationData validation_data{};
		validation_data.scene_service = context.scene_service;
		validation_data.root_append_slot = true;

		EditorTreeDropTargetDesc drop_target{};
		drop_target.payload_type = k_scene_hierarchy_drag_payload_type;
		drop_target.validate_drop = validate_scene_drop_target;
		drop_target.validation_user_data = &validation_data;

		EditorTreeDropSlotDesc slot_desc{};
		slot_desc.unique_id = "__scene_hierarchy_root_append__";
		slot_desc.height = 18.0f;
		slot_desc.expand_to_available_height_while_dragging = dragging_scene_entity;
		slot_desc.preview_visual = EditorTreeDropVisual::Before;
		slot_desc.drop_target = &drop_target;

		const EditorTreeDropSlotResult result = tree_widget.draw_drop_slot(slot_desc, dragging_scene_entity);
		if (result.drop_delivered && result.accepted_payload && context.undo_redo_service)
		{
			const SceneHierarchyDropRequest request =
				build_root_append_drop_request(*context.scene_service, decode_dragged_entity_id(result.accepted_payload));
			if (request.valid)
			{
				context.undo_redo_service->execute(
					std::make_unique<ReparentEntityCommand>(
						request.entity_id,
						request.new_parent_id,
						request.sibling_index),
					context);
			}
		}
	}

	void SceneHierarchyPanel::draw_entity_tree(
		EditorTreeWidget& tree_widget,
		EditorContext& context,
		const AshEngine::Entity& entity,
		bool is_last_sibling)
	{
		const std::vector<AshEngine::Entity> children = entity.get_children();
		const bool has_children = !children.empty();
		const std::string entity_name = entity.get_name();
		const bool selected =
			context.selection_service &&
			context.selection_service->get_selection().kind == EditorSelectionKind::Entity &&
			context.selection_service->get_selection().id == entity.get_id();

		AshEngine::UITextureHandle closed_icon = nullptr;
		AshEngine::UITextureHandle open_icon = nullptr;
		if (context.icon_service)
		{
			closed_icon = context.icon_service->get_icon(get_entity_icon_id(entity), *context.ui_context);
			open_icon = closed_icon;
		}

		const EntityId entity_id = entity.get_id();
		SceneEntityDropValidationData validation_data{
			context.scene_service,
			entity_id,
			false
		};
		const EditorTreeDragSourceDesc drag_source{
			k_scene_hierarchy_drag_payload_type,
			&entity_id,
			static_cast<int32_t>(sizeof(entity_id)),
			entity_name.c_str()
		};
		const EditorTreeDropTargetDesc drop_target{
			k_scene_hierarchy_drag_payload_type,
			true,
			true,
			true,
			true,
			validate_scene_drop_target,
			const_cast<SceneEntityDropValidationData*>(&validation_data)
		};

		const std::string unique_id = std::to_string(entity_id);
		EditorTreeItemDesc desc{};
		desc.unique_id = unique_id;
		desc.label = entity_name;
		desc.icon = closed_icon;
		desc.icon_when_open = open_icon;
		desc.selected = selected;
		desc.has_children = has_children;
		desc.is_last_sibling = is_last_sibling;
		desc.drag_source = &drag_source;
		desc.drop_target = &drop_target;

		const EditorTreeItemResult result = tree_widget.draw_item(desc);
		if (result.clicked && context.selection_service)
		{
			context.selection_service->select({ EditorSelectionKind::Entity, entity_id, entity_name, {} });
		}
		if (context.ui_context && context.ui_context->is_item_clicked(AshEngine::UIMouseButton::Right))
		{
			if (context.selection_service)
			{
				context.selection_service->select({ EditorSelectionKind::Entity, entity_id, entity_name, {} });
			}
		}
		draw_entity_context_menu(context, entity);

		if (result.drop_delivered && result.accepted_payload && context.undo_redo_service)
		{
			const SceneHierarchyDropRequest request = build_drop_request_for_target(
				*context.scene_service,
				decode_dragged_entity_id(result.accepted_payload),
				entity,
				result.drop_visual);
			if (request.valid)
			{
				context.undo_redo_service->execute(
					std::make_unique<ReparentEntityCommand>(
						request.entity_id,
						request.new_parent_id,
						request.sibling_index),
					context);
			}
		}

		if (!result.opened)
		{
			return;
		}

		if (has_children)
		{
			tree_widget.push_level(!is_last_sibling);
			for (size_t child_index = 0; child_index < children.size(); ++child_index)
			{
				draw_entity_tree(tree_widget, context, children[child_index], child_index + 1 == children.size());
			}
			tree_widget.pop_level();
		}
		ImGui::TreePop();
	}

	void SceneHierarchyPanel::draw_entity_context_menu(EditorContext& context, const AshEngine::Entity& entity)
	{
		if (!ImGui::BeginPopupContextItem(k_sceneEntityContextPopupId, ImGuiPopupFlags_MouseButtonRight))
		{
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		if (context.selection_service)
		{
			context.selection_service->select({ EditorSelectionKind::Entity, entity.get_id(), entity.get_name(), {} });
		}

		if (ui.menu_item("Add Child", get_action_shortcut(context.command_service, "scene.create_child")))
		{
			if (context.command_service)
			{
				context.command_service->invoke("scene.create_child");
			}
			ui.close_current_popup();
		}
		if (ui.menu_item("Rename", get_action_shortcut(context.command_service, "selection.rename")))
		{
			if (context.command_service)
			{
				context.command_service->invoke("selection.rename");
			}
			ui.close_current_popup();
		}
		if (ui.menu_item("Reparent", get_action_shortcut(context.command_service, "selection.reparent")))
		{
			if (context.command_service)
			{
				context.command_service->invoke("selection.reparent");
			}
			ui.close_current_popup();
		}
		ui.separator();
		if (ui.menu_item("Delete", get_action_shortcut(context.command_service, "selection.delete")))
		{
			if (context.command_service)
			{
				context.command_service->invoke("selection.delete");
			}
			ui.close_current_popup();
		}

		ImGui::EndPopup();
	}

	void SceneHierarchyPanel::draw_content_context_menu(EditorContext& context, EntityId selected_entity_id)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		if (!ui.begin_popup(k_sceneContentContextPopupId))
		{
			return;
		}

		if (ui.menu_item("Add Root", get_action_shortcut(context.command_service, "scene.create_root")))
		{
			if (context.command_service)
			{
				context.command_service->invoke("scene.create_root");
			}
			ui.close_current_popup();
		}

		ui.separator();
		ui.begin_disabled(selected_entity_id == 0);
		if (ui.menu_item("Add Child", get_action_shortcut(context.command_service, "scene.create_child")))
		{
			if (context.command_service)
			{
				context.command_service->invoke("scene.create_child");
			}
			ui.close_current_popup();
		}
		ui.end_disabled();

		ui.end_popup();
	}

	void SceneHierarchyPanel::draw_rename_modal(EditorContext& context)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		if (!ui.begin_popup_modal("Rename Entity"))
		{
			return;
		}

		ui.text_unformatted("Update the selected entity name.");
		ui.input_text("Name", m_pendingRenameValue);
		ui.separator();

		bool can_apply = m_pendingRenameEntityId != 0 && !m_pendingRenameValue.empty();
		if (can_apply && context.scene_service)
		{
			const AshEngine::Entity entity = context.scene_service->find_entity(m_pendingRenameEntityId);
			can_apply = entity.is_valid() && entity.get_name() != m_pendingRenameValue;
		}
		ui.begin_disabled(!can_apply);
		if (ui.button("Apply"))
		{
			if (context.undo_redo_service)
			{
				context.undo_redo_service->execute(
					std::make_unique<RenameEntityCommand>(m_pendingRenameEntityId, m_pendingRenameValue),
					context);
			}
			m_pendingRenameEntityId = 0;
			m_pendingRenameValue.clear();
			ui.close_current_popup();
		}
		ui.end_disabled();
		ui.same_line();
		if (ui.button("Cancel"))
		{
			m_pendingRenameEntityId = 0;
			m_pendingRenameValue.clear();
			ui.close_current_popup();
		}
		ui.end_popup();
	}

	void SceneHierarchyPanel::draw_reparent_modal(EditorContext& context)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		if (!ui.begin_popup_modal("Reparent Entity"))
		{
			return;
		}

		ui.text_unformatted("Move the selected entity under another parent.");
		ui.combo("Parent", m_pendingReparentIndex, m_pendingReparentParentLabels);
		int32_t insert_index_max = 0;
		EntityId target_parent_id = 0;
		EntityId current_parent_id = 0;
		ui.separator();

		bool has_valid_target =
			m_pendingReparentEntityId != 0 &&
			m_pendingReparentIndex >= 0 &&
			m_pendingReparentIndex < static_cast<int32_t>(m_pendingReparentParentIds.size());
		if (has_valid_target && context.scene_service)
		{
			const AshEngine::Entity entity = context.scene_service->find_entity(m_pendingReparentEntityId);
			has_valid_target = entity.is_valid();
			if (has_valid_target)
			{
				target_parent_id = m_pendingReparentParentIds[static_cast<size_t>(m_pendingReparentIndex)];
				current_parent_id = get_entity_parent_id(entity);
				insert_index_max = static_cast<int32_t>(get_reparent_insert_index_max(
					*context.scene_service,
					m_pendingReparentEntityId,
					current_parent_id,
					target_parent_id));
				m_pendingReparentInsertIndex = std::clamp(m_pendingReparentInsertIndex, 0, insert_index_max);
				const int32_t current_sibling_index = static_cast<int32_t>(get_entity_sibling_index(entity, *context.scene_service));
				has_valid_target =
					current_parent_id != target_parent_id ||
					current_sibling_index != m_pendingReparentInsertIndex;
			}
		}

		ui.input_int("Insert At", m_pendingReparentInsertIndex);
		m_pendingReparentInsertIndex = std::clamp(m_pendingReparentInsertIndex, 0, insert_index_max);
		ui.text("Valid Range: 0 - %d", insert_index_max);
		ui.text_unformatted("Insert At is the 0-based sibling slot under the target parent.");
		ui.begin_disabled(!has_valid_target);
		if (ui.button("Apply"))
		{
			if (context.undo_redo_service && has_valid_target)
			{
				context.undo_redo_service->execute(
					std::make_unique<ReparentEntityCommand>(
						m_pendingReparentEntityId,
						target_parent_id,
						static_cast<uint32_t>(m_pendingReparentInsertIndex)),
					context);
			}
			m_pendingReparentEntityId = 0;
			m_pendingReparentIndex = 0;
			m_pendingReparentInsertIndex = 0;
			m_pendingReparentParentIds.clear();
			m_pendingReparentParentLabels.clear();
			ui.close_current_popup();
		}
		ui.end_disabled();
		ui.same_line();
		if (ui.button("Cancel"))
		{
			m_pendingReparentEntityId = 0;
			m_pendingReparentIndex = 0;
			m_pendingReparentInsertIndex = 0;
			m_pendingReparentParentIds.clear();
			m_pendingReparentParentLabels.clear();
			ui.close_current_popup();
		}
		ui.end_popup();
	}

	void SceneHierarchyPanel::draw_delete_modal(EditorContext& context)
	{
		AshEngine::UIContext& ui = *context.ui_context;
		if (!ui.begin_popup_modal("Delete Entity"))
		{
			return;
		}

		ui.text_unformatted("Delete the selected entity?");
		ui.text("Target: %s", m_pendingDeleteEntityName.empty() ? "<Unknown>" : m_pendingDeleteEntityName.c_str());
		ui.text_unformatted("This action can be undone.");
		ui.separator();

		const bool can_delete = m_pendingDeleteEntityId != 0;
		ui.begin_disabled(!can_delete);
		if (ui.button("Delete"))
		{
			destroy_entity(context, m_pendingDeleteEntityId);
			m_pendingDeleteEntityId = 0;
			m_pendingDeleteEntityName.clear();
			ui.close_current_popup();
		}
		ui.end_disabled();
		ui.same_line();
		if (ui.button("Cancel"))
		{
			m_pendingDeleteEntityId = 0;
			m_pendingDeleteEntityName.clear();
			ui.close_current_popup();
		}
		ui.end_popup();
	}

	void SceneHierarchyPanel::on_gui(EditorContext& context)
	{
		if (!begin_panel_window(context))
		{
			end_panel_window(context);
			return;
		}

		AshEngine::UIContext& ui = *context.ui_context;
		if (!context.scene_service)
		{
			ui.text_unformatted("Scene service unavailable.");
			end_panel_window(context);
			return;
		}

		AshEngine::Scene& scene = context.scene_service->get_active_scene();
		const EntityId selected_entity_id = get_selected_entity_id(context);
		const bool dragging_scene_entity = is_scene_entity_drag_active();
		draw_scene_summary(ui, scene, selected_entity_id);

		draw_toolbar(context, selected_entity_id);
		ui.separator();

		const std::vector<AshEngine::Entity> roots = scene.get_root_entities();
		if (roots.empty())
		{
			draw_empty_scene_state(ui);
		}

		{
			EditorTreeWidget tree_widget(ui, m_treeWidgetState, make_scene_tree_style());
			tree_widget.reset_drag_state_if_inactive();
			for (size_t root_index = 0; root_index < roots.size(); ++root_index)
			{
				draw_entity_tree(tree_widget, context, roots[root_index], root_index + 1 == roots.size());
			}

			handle_root_append_drop_target(tree_widget, context, dragging_scene_entity);
		}

		const bool open_content_menu =
			ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
			!ImGui::IsAnyItemHovered() &&
			!ImGui::IsAnyItemActive() &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Right);
		const bool clear_selection =
			ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
			!ImGui::IsAnyItemHovered() &&
			!ImGui::IsAnyItemActive() &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left);
		if (open_content_menu)
		{
			ui.open_popup(k_sceneContentContextPopupId);
		}
		if (clear_selection && context.selection_service)
		{
			context.selection_service->clear();
		}

		draw_content_context_menu(context, get_selected_entity_id(context));

		draw_rename_modal(context);
		draw_reparent_modal(context);
		draw_delete_modal(context);
		end_panel_window(context);
	}
}

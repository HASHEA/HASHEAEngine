#include "Panels/SceneHierarchyPanel.h"
#include "Base/hlog.h"
#include "Core/EditorCommand.h"
#include "Core/EntityCommands.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
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

		void draw_entity_tree(AshEngine::UIContext& ui, EditorContext& context, const AshEngine::Entity& entity)
		{
			const std::vector<AshEngine::Entity> children = entity.get_children();
			AshEngine::UITreeNodeFlags flags = AshEngine::UITreeNodeFlagBits::OpenOnArrow | AshEngine::UITreeNodeFlagBits::SpanAvailWidth;
			if (children.empty())
			{
				flags |= AshEngine::UITreeNodeFlagBits::Leaf;
			}

			const bool selected =
				context.selection_service &&
				context.selection_service->get_selection().kind == EditorSelectionKind::Entity &&
				context.selection_service->get_selection().id == entity.get_id();
			if (selected)
			{
				flags |= AshEngine::UITreeNodeFlagBits::Selected;
			}

			const void* stable_id = reinterpret_cast<const void*>(static_cast<uintptr_t>(entity.get_id()));
			const bool opened = ui.tree_node(stable_id, entity.get_name().c_str(), flags);
			if (ui.is_item_clicked() && context.selection_service)
			{
				context.selection_service->select({ EditorSelectionKind::Entity, entity.get_id(), entity.get_name(), {} });
			}

			if (opened)
			{
				for (const AshEngine::Entity& child : children)
				{
					draw_entity_tree(ui, context, child);
				}
				ui.tree_pop();
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

	void SceneHierarchyPanel::destroy_selected_entity(EditorContext& context)
	{
		destroy_entity(context, get_selected_entity_id(context));
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
		if (ui.button("Delete Selected"))
		{
			begin_delete_selected_entity(context);
		}
		ui.end_disabled();
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
		ui.separator();

		bool has_valid_target =
			m_pendingReparentEntityId != 0 &&
			m_pendingReparentIndex >= 0 &&
			m_pendingReparentIndex < static_cast<int32_t>(m_pendingReparentParentIds.size());
		if (has_valid_target && context.scene_service)
		{
			const AshEngine::Entity entity = context.scene_service->find_entity(m_pendingReparentEntityId);
			has_valid_target =
				entity.is_valid() &&
				get_entity_parent_id(entity) != m_pendingReparentParentIds[static_cast<size_t>(m_pendingReparentIndex)];
		}
		ui.begin_disabled(!has_valid_target);
		if (ui.button("Apply"))
		{
			if (context.undo_redo_service && has_valid_target)
			{
				const EntityId target_parent_id = m_pendingReparentParentIds[static_cast<size_t>(m_pendingReparentIndex)];
				context.undo_redo_service->execute(
					std::make_unique<ReparentEntityCommand>(m_pendingReparentEntityId, target_parent_id),
					context);
			}
			m_pendingReparentEntityId = 0;
			m_pendingReparentIndex = 0;
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
		ui.text_unformatted("This action can be undone, but restore currently uses the minimal subtree snapshot path.");
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
		ui.text("Scene: %s", scene.get_name().c_str());
		ui.text("Entities: %u", scene.get_entity_count());
		ui.separator();

		draw_toolbar(context, selected_entity_id);
		ui.separator();

		const std::vector<AshEngine::Entity> roots = scene.get_root_entities();
		if (roots.empty())
		{
			ui.text_unformatted("Scene is empty.");
		}

		for (const AshEngine::Entity& entity : roots)
		{
			draw_entity_tree(ui, context, entity);
		}

		draw_rename_modal(context);
		draw_reparent_modal(context);
		draw_delete_modal(context);
		end_panel_window(context);
	}
}

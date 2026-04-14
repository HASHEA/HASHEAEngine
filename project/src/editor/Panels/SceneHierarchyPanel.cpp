#include "Panels/SceneHierarchyPanel.h"
#include "Base/hlog.h"
#include "Function/Scene/Scene.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "imgui.h"
#include <string>

namespace AshEditor
{
	namespace
	{
		void draw_entity_tree(EditorContext& context, const AshEngine::Entity& entity)
		{
			const std::vector<AshEngine::Entity> children = entity.get_children();
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (children.empty())
			{
				flags |= ImGuiTreeNodeFlags_Leaf;
			}

			const bool selected =
				context.selection_service &&
				context.selection_service->get_selection().kind == EditorSelectionKind::Entity &&
				context.selection_service->get_selection().id == entity.get_id();
			if (selected)
			{
				flags |= ImGuiTreeNodeFlags_Selected;
			}

			const bool opened = ImGui::TreeNodeEx(
				reinterpret_cast<void*>(static_cast<uintptr_t>(entity.get_id())),
				flags,
				"%s",
				entity.get_name().c_str());
			if (ImGui::IsItemClicked() && context.selection_service)
			{
				context.selection_service->select({ EditorSelectionKind::Entity, entity.get_id(), entity.get_name(), {} });
			}

			if (!opened)
			{
				return;
			}

			for (const AshEngine::Entity& child : children)
			{
				draw_entity_tree(context, child);
			}

			ImGui::TreePop();
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

	void SceneHierarchyPanel::on_gui(EditorContext& context)
	{
		if (!begin_panel_window())
		{
			end_panel_window();
			return;
		}

		if (!context.scene_service)
		{
			ImGui::TextUnformatted("Scene service unavailable.");
			end_panel_window();
			return;
		}

		AshEngine::Scene& scene = context.scene_service->get_active_scene();
		ImGui::Text("Scene: %s", scene.get_name().c_str());
		ImGui::Text("Entities: %u", scene.get_entity_count());
		ImGui::Separator();

		if (ImGui::Button("Add Empty"))
		{
			const std::string entity_name = "Entity " + std::to_string(scene.get_entity_count() + 1);
			EntityId parent_id = 0;
			if (context.selection_service && context.selection_service->get_selection().kind == EditorSelectionKind::Entity)
			{
				parent_id = context.selection_service->get_selection().id;
			}

			const AshEngine::Entity entity = context.scene_service->create_entity(entity_name, parent_id);
			if (context.selection_service)
			{
				context.selection_service->select({ EditorSelectionKind::Entity, entity.get_id(), entity.get_name(), {} });
			}
		}
		ImGui::Separator();

		const std::vector<AshEngine::Entity> roots = scene.get_root_entities();
		if (roots.empty())
		{
			ImGui::TextUnformatted("Scene is empty.");
		}

		for (const AshEngine::Entity& entity : roots)
		{
			draw_entity_tree(context, entity);
		}

		end_panel_window();
	}
}

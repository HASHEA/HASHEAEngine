#include "Panels/InspectorPanel.h"
#include "Base/hlog.h"
#include "Function/Scene/Scene.h"
#include "Services/AssetDatabaseService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "imgui.h"
#include <cstring>

namespace AshEditor
{
	namespace
	{
		bool edit_name_component(const char* label, AshEngine::NameComponent& component)
		{
			char buffer[256]{};
			std::strncpy(buffer, component.value.c_str(), sizeof(buffer) - 1);
			if (!ImGui::InputText(label, buffer, sizeof(buffer)))
			{
				return false;
			}

			component.value = buffer;
			return true;
		}

		bool edit_mesh_path(AshEngine::MeshComponent& component)
		{
			char buffer[260]{};
			std::strncpy(buffer, component.asset_path.c_str(), sizeof(buffer) - 1);
			if (!ImGui::InputText("Asset Path", buffer, sizeof(buffer)))
			{
				return false;
			}

			component.asset_path = buffer;
			return true;
		}
	}

	InspectorPanel::InspectorPanel()
		: EditorPanel("inspector", "Inspector")
	{
	}

	void InspectorPanel::on_attach(EditorContext& context)
	{
		(void)context;
		HLogInfo("InspectorPanel attached.");
	}

	void InspectorPanel::on_gui(EditorContext& context)
	{
		if (!begin_panel_window())
		{
			end_panel_window();
			return;
		}

		if (!context.selection_service || !context.selection_service->has_selection())
		{
			ImGui::TextUnformatted("Nothing selected.");
			end_panel_window();
			return;
		}

		const EditorSelection& selection = context.selection_service->get_selection();
		ImGui::Text("Selection: %s", selection.label.c_str());
		ImGui::Text("Id: %llu", static_cast<unsigned long long>(selection.id));
		ImGui::Separator();

		if (selection.kind == EditorSelectionKind::Entity && context.scene_service)
		{
			AshEngine::Entity entity = context.scene_service->find_entity(selection.id);
			if (!entity.is_valid())
			{
				ImGui::TextUnformatted("Selected entity no longer exists.");
				end_panel_window();
				return;
			}

			AshEngine::NameComponent name = entity.get_name_component();
			if (edit_name_component("Name", name) && entity.set_name_component(name))
			{
				context.selection_service->select({ selection.kind, selection.id, name.value, selection.path });
			}

			AshEngine::TransformComponent transform = entity.get_transform_component();
			bool transform_changed = false;
			transform_changed |= ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
			transform_changed |= ImGui::DragFloat3("Rotation", &transform.rotation_euler_degrees.x, 0.5f);
			transform_changed |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f);
			if (transform_changed)
			{
				entity.set_transform_component(transform);
			}

			ImGui::Separator();
			if (entity.has_camera_component())
			{
				if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
				{
					AshEngine::CameraComponent camera = entity.get_camera_component();
					bool camera_changed = false;
					camera_changed |= ImGui::Checkbox("Primary", &camera.primary);
					int projection = static_cast<int>(camera.projection);
					const char* projection_labels[] = { "Perspective", "Orthographic" };
					if (ImGui::Combo("Projection", &projection, projection_labels, IM_ARRAYSIZE(projection_labels)))
					{
						camera.projection = static_cast<AshEngine::CameraProjectionType>(projection);
						camera_changed = true;
					}
					camera_changed |= ImGui::DragFloat("FOV Y", &camera.fov_y_degrees, 0.1f, 1.0f, 179.0f);
					camera_changed |= ImGui::DragFloat("Near Plane", &camera.near_plane, 0.01f, 0.001f, camera.far_plane);
					camera_changed |= ImGui::DragFloat("Far Plane", &camera.far_plane, 1.0f, camera.near_plane, 10000.0f);
					camera_changed |= ImGui::DragFloat("Ortho Height", &camera.orthographic_height, 0.1f, 0.1f, 1000.0f);
					if (camera_changed)
					{
						entity.set_camera_component(camera);
					}
					if (ImGui::Button("Remove Camera"))
					{
						entity.remove_camera_component();
					}
				}
			}
			else if (ImGui::Button("Add Camera"))
			{
				entity.add_camera_component({});
			}

			ImGui::Separator();
			if (entity.has_light_component())
			{
				if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
				{
					AshEngine::LightComponent light = entity.get_light_component();
					bool light_changed = false;
					int light_type = static_cast<int>(light.type);
					const char* light_labels[] = { "Directional", "Point", "Spot" };
					if (ImGui::Combo("Light Type", &light_type, light_labels, IM_ARRAYSIZE(light_labels)))
					{
						light.type = static_cast<AshEngine::LightType>(light_type);
						light_changed = true;
					}
					light_changed |= ImGui::ColorEdit3("Color", &light.color.x);
					light_changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);
					light_changed |= ImGui::DragFloat("Range", &light.range, 0.1f, 0.0f, 1000.0f);
					light_changed |= ImGui::DragFloat("Inner Cone", &light.inner_cone_angle_degrees, 0.1f, 0.0f, 180.0f);
					light_changed |= ImGui::DragFloat("Outer Cone", &light.outer_cone_angle_degrees, 0.1f, 0.0f, 180.0f);
					if (light_changed)
					{
						entity.set_light_component(light);
					}
					if (ImGui::Button("Remove Light"))
					{
						entity.remove_light_component();
					}
				}
			}
			else if (ImGui::Button("Add Light"))
			{
				entity.add_light_component({});
			}

			ImGui::Separator();
			if (entity.has_mesh_component())
			{
				if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
				{
					AshEngine::MeshComponent mesh = entity.get_mesh_component();
					bool mesh_changed = false;
					mesh_changed |= edit_mesh_path(mesh);
					mesh_changed |= ImGui::Checkbox("Visible", &mesh.visible);
					if (mesh_changed)
					{
						entity.set_mesh_component(mesh);
					}
					if (ImGui::Button("Remove Mesh"))
					{
						entity.remove_mesh_component();
					}
				}
			}
			else if (ImGui::Button("Add Mesh"))
			{
				entity.add_mesh_component({});
			}

			ImGui::Separator();
			const AshEngine::Entity parent = entity.get_parent();
			ImGui::Text("Parent Id: %llu", static_cast<unsigned long long>(parent.get_id()));
			ImGui::Text("Children: %u", static_cast<unsigned int>(entity.get_children().size()));
		}
		else if (selection.kind == EditorSelectionKind::Asset && context.asset_database_service)
		{
			const AshEngine::AssetInfo* asset = context.asset_database_service->find_by_id(selection.id);
			if (!asset)
			{
				ImGui::TextUnformatted("Selected asset no longer exists.");
				end_panel_window();
				return;
			}

			ImGui::Text("Type: %s", AssetDatabaseService::get_type_label(asset->type));
			ImGui::TextWrapped("Path: %s", asset->relative_path.generic_string().c_str());
			ImGui::TextWrapped("Parent: %s", asset->parent_path.generic_string().c_str());
			ImGui::Text("Directory: %s", asset->is_directory ? "true" : "false");
			ImGui::Text("File Size: %llu", static_cast<unsigned long long>(asset->file_size));
			ImGui::Text("Load State: %s", AssetDatabaseService::get_load_state_label(context.asset_database_service->get_load_state(asset->id)));
		}
		else
		{
			ImGui::TextUnformatted("Inspector adapter for this selection type is pending.");
		}

		end_panel_window();
	}
}

#include "Scene.h"

#include "Function/Asset/AssetData.h"
#include "Function/Asset/AssetDatabase.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <json.hpp>
#include <unordered_map>
#include <entt/entt.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		struct EntityIdComponent
		{
			EntityId id = 0;
		};

		struct HierarchyComponent
		{
			EntityId parent = 0;
			std::vector<EntityId> children{};
		};

		struct SceneStorage
		{
			std::string name = "Untitled Scene";
			std::filesystem::path source_path{};
			entt::registry registry{};
			std::unordered_map<EntityId, entt::entity> entities{};
			std::vector<EntityId> entity_order{};
			EntityId next_entity_id = 1;
			bool dirty = false;
		};

		static constexpr uint32_t k_scene_file_version = 2;

		static SceneEnumValueDesc k_camera_projection_values[] =
		{
			{ static_cast<int32_t>(CameraProjectionType::Perspective), "Perspective" },
			{ static_cast<int32_t>(CameraProjectionType::Orthographic), "Orthographic" },
		};

		static SceneEnumValueDesc k_light_type_values[] =
		{
			{ static_cast<int32_t>(LightType::Directional), "Directional" },
			{ static_cast<int32_t>(LightType::Point), "Point" },
			{ static_cast<int32_t>(LightType::Spot), "Spot" },
		};

		static SceneEnumDesc k_scene_enum_descs[] =
		{
			{ "CameraProjectionType", k_camera_projection_values, static_cast<uint32_t>(std::size(k_camera_projection_values)) },
			{ "LightType", k_light_type_values, static_cast<uint32_t>(std::size(k_light_type_values)) },
		};

		static ScenePropertyDesc k_name_properties[] =
		{
			{ "value", ScenePropertyType::String, static_cast<uint32_t>(offsetof(NameComponent, value)), static_cast<uint32_t>(sizeof(std::string)), nullptr },
		};

		static ScenePropertyDesc k_transform_properties[] =
		{
			{ "position", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(TransformComponent, position)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr },
			{ "rotation_euler_degrees", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(TransformComponent, rotation_euler_degrees)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr },
			{ "scale", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(TransformComponent, scale)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr },
		};

		static ScenePropertyDesc k_camera_properties[] =
		{
			{ "primary", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(CameraComponent, primary)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "projection", ScenePropertyType::Enum, static_cast<uint32_t>(offsetof(CameraComponent, projection)), static_cast<uint32_t>(sizeof(CameraProjectionType)), "CameraProjectionType" },
			{ "fov_y_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, fov_y_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "near_plane", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, near_plane)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "far_plane", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, far_plane)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "orthographic_height", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, orthographic_height)), static_cast<uint32_t>(sizeof(float)), nullptr },
		};

		static ScenePropertyDesc k_light_properties[] =
		{
			{ "type", ScenePropertyType::Enum, static_cast<uint32_t>(offsetof(LightComponent, type)), static_cast<uint32_t>(sizeof(LightType)), "LightType" },
			{ "color", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(LightComponent, color)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr },
			{ "intensity", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, intensity)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "range", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, range)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "inner_cone_angle_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, inner_cone_angle_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "outer_cone_angle_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, outer_cone_angle_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr },
		};

		static ScenePropertyDesc k_mesh_properties[] =
		{
			{ "asset_path", ScenePropertyType::String, static_cast<uint32_t>(offsetof(MeshComponent, asset_path)), static_cast<uint32_t>(sizeof(std::string)), nullptr },
			{ "mesh_index", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(MeshComponent, mesh_index)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
			{ "visible", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(MeshComponent, visible)), static_cast<uint32_t>(sizeof(bool)), nullptr },
		};

		static SceneComponentDesc k_scene_component_descs[] =
		{
			{ SceneComponentType::Name, "NameComponent", k_name_properties, static_cast<uint32_t>(std::size(k_name_properties)), static_cast<uint32_t>(sizeof(NameComponent)) },
			{ SceneComponentType::Transform, "TransformComponent", k_transform_properties, static_cast<uint32_t>(std::size(k_transform_properties)), static_cast<uint32_t>(sizeof(TransformComponent)) },
			{ SceneComponentType::Camera, "CameraComponent", k_camera_properties, static_cast<uint32_t>(std::size(k_camera_properties)), static_cast<uint32_t>(sizeof(CameraComponent)) },
			{ SceneComponentType::Light, "LightComponent", k_light_properties, static_cast<uint32_t>(std::size(k_light_properties)), static_cast<uint32_t>(sizeof(LightComponent)) },
			{ SceneComponentType::Mesh, "MeshComponent", k_mesh_properties, static_cast<uint32_t>(std::size(k_mesh_properties)), static_cast<uint32_t>(sizeof(MeshComponent)) },
		};

		static auto to_json_vec3(const glm::vec3& value) -> json
		{
			return json::array({ value.x, value.y, value.z });
		}

		static auto from_json_vec3(const json& value, const glm::vec3& fallback) -> glm::vec3
		{
			if (!value.is_array() || value.size() != 3)
			{
				return fallback;
			}

			glm::vec3 result = fallback;
			result.x = value[0].get<float>();
			result.y = value[1].get<float>();
			result.z = value[2].get<float>();
			return result;
		}

		static auto make_scene_error(std::string* out_error, std::string_view message) -> void
		{
			if (out_error)
			{
				*out_error = std::string(message);
			}
		}

		static auto matrix_to_transform_component(const glm::mat4& matrix) -> TransformComponent
		{
			TransformComponent component{};
			glm::vec3 scale{};
			glm::quat rotation{};
			glm::vec3 translation{};
			glm::vec3 skew{};
			glm::vec4 perspective{};
			if (glm::decompose(matrix, scale, rotation, translation, skew, perspective))
			{
				component.position = translation;
				component.scale = scale;
				component.rotation_euler_degrees = glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
			}
			return component;
		}
	}

	class Entity::Impl
	{
	public:
		SceneStorage storage{};
	};

	class Scene::Impl : public Entity::Impl
	{
	};

	namespace
	{
		static auto find_handle(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> entt::entity
		{
			if (!impl)
			{
				return entt::null;
			}
			const auto found = impl->storage.entities.find(id);
			return found != impl->storage.entities.end() ? found->second : entt::null;
		}

		static auto find_hierarchy(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> HierarchyComponent*
		{
			const entt::entity handle = find_handle(impl, id);
			if (handle == entt::null)
			{
				return nullptr;
			}
			return impl->storage.registry.try_get<HierarchyComponent>(handle);
		}

		static auto find_hierarchy_const(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> const HierarchyComponent*
		{
			const entt::entity handle = find_handle(impl, id);
			if (handle == entt::null)
			{
				return nullptr;
			}
			return impl->storage.registry.try_get<HierarchyComponent>(handle);
		}

		static auto is_scene_descendant(const std::shared_ptr<Scene::Impl>& impl, EntityId potential_parent, EntityId id) -> bool
		{
			const HierarchyComponent* hierarchy = find_hierarchy_const(impl, id);
			if (!hierarchy)
			{
				return false;
			}
			EntityId parent = hierarchy->parent;
			while (parent != 0)
			{
				if (parent == potential_parent)
				{
					return true;
				}
				const HierarchyComponent* parent_hierarchy = find_hierarchy_const(impl, parent);
				parent = parent_hierarchy ? parent_hierarchy->parent : 0;
			}
			return false;
		}

		static auto detach_from_parent(SceneStorage& storage, EntityId id) -> void
		{
			const auto found = storage.entities.find(id);
			if (found == storage.entities.end())
			{
				return;
			}

			HierarchyComponent* hierarchy = storage.registry.try_get<HierarchyComponent>(found->second);
			if (!hierarchy || hierarchy->parent == 0)
			{
				return;
			}

			const auto parent_it = storage.entities.find(hierarchy->parent);
			if (parent_it != storage.entities.end())
			{
				if (HierarchyComponent* parent_hierarchy = storage.registry.try_get<HierarchyComponent>(parent_it->second))
				{
					parent_hierarchy->children.erase(
						std::remove(parent_hierarchy->children.begin(), parent_hierarchy->children.end(), id),
						parent_hierarchy->children.end());
				}
			}
			hierarchy->parent = 0;
		}

		static auto attach_to_parent(SceneStorage& storage, EntityId id, EntityId parent) -> void
		{
			const auto found = storage.entities.find(id);
			const auto parent_found = storage.entities.find(parent);
			if (found == storage.entities.end() || parent_found == storage.entities.end())
			{
				return;
			}

			HierarchyComponent* hierarchy = storage.registry.try_get<HierarchyComponent>(found->second);
			HierarchyComponent* parent_hierarchy = storage.registry.try_get<HierarchyComponent>(parent_found->second);
			if (!hierarchy || !parent_hierarchy)
			{
				return;
			}

			hierarchy->parent = parent;
			parent_hierarchy->children.push_back(id);
		}

		static auto create_entity_internal(const std::shared_ptr<Scene::Impl>& impl, EntityId explicit_id, std::string_view name) -> Entity
		{
			if (!impl)
			{
				return {};
			}

			EntityId id = explicit_id != 0 ? explicit_id : impl->storage.next_entity_id++;
			impl->storage.next_entity_id = std::max(impl->storage.next_entity_id, id + 1);
			if (impl->storage.entities.find(id) != impl->storage.entities.end())
			{
				return {};
			}

			const entt::entity handle = impl->storage.registry.create();
			impl->storage.registry.emplace<EntityIdComponent>(handle, EntityIdComponent{ id });
			impl->storage.registry.emplace<NameComponent>(handle, NameComponent{ name.empty() ? std::string("Entity") : std::string(name) });
			impl->storage.registry.emplace<TransformComponent>(handle, TransformComponent{});
			impl->storage.registry.emplace<HierarchyComponent>(handle, HierarchyComponent{});
			impl->storage.entities.emplace(id, handle);
			impl->storage.entity_order.push_back(id);
			return Entity(impl, id);
		}

		static auto destroy_entity_recursive(SceneStorage& storage, EntityId id) -> bool
		{
			const auto found = storage.entities.find(id);
			if (found == storage.entities.end())
			{
				return false;
			}

			const std::vector<EntityId> children = storage.registry.get<HierarchyComponent>(found->second).children;
			for (EntityId child_id : children)
			{
				destroy_entity_recursive(storage, child_id);
			}

			detach_from_parent(storage, id);
			storage.entity_order.erase(std::remove(storage.entity_order.begin(), storage.entity_order.end(), id), storage.entity_order.end());
			storage.registry.destroy(found->second);
			storage.entities.erase(found);
			return true;
		}
	}

	Entity::Entity(std::shared_ptr<Impl> impl, EntityId id)
		: m_impl(std::move(impl)), m_id(id)
	{
	}

	bool Entity::is_valid() const
	{
		return find_handle(std::static_pointer_cast<Scene::Impl>(m_impl), m_id) != entt::null;
	}

	EntityId Entity::get_id() const
	{
		return is_valid() ? m_id : 0;
	}

	NameComponent Entity::get_name_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null ? impl->storage.registry.get<NameComponent>(handle) : NameComponent{};
	}

	bool Entity::set_name_component(const NameComponent& component)
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null)
		{
			return false;
		}
		impl->storage.registry.replace<NameComponent>(handle, component);
		impl->storage.dirty = true;
		return true;
	}

	std::string Entity::get_name() const
	{
		return get_name_component().value;
	}

	bool Entity::set_name(std::string_view name)
	{
		NameComponent component = get_name_component();
		component.value = std::string(name);
		return set_name_component(component);
	}

	TransformComponent Entity::get_transform_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null ? impl->storage.registry.get<TransformComponent>(handle) : TransformComponent{};
	}

	bool Entity::set_transform_component(const TransformComponent& component)
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null)
		{
			return false;
		}
		impl->storage.registry.replace<TransformComponent>(handle, component);
		impl->storage.dirty = true;
		return true;
	}

	bool Entity::has_camera_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null && impl->storage.registry.any_of<CameraComponent>(handle);
	}

	CameraComponent Entity::get_camera_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle != entt::null)
		{
			if (const CameraComponent* component = impl->storage.registry.try_get<CameraComponent>(handle))
			{
				return *component;
			}
		}
		return {};
	}

	bool Entity::add_camera_component(const CameraComponent& component)
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null)
		{
			return false;
		}
		impl->storage.registry.emplace_or_replace<CameraComponent>(handle, component);
		impl->storage.dirty = true;
		return true;
	}

	bool Entity::set_camera_component(const CameraComponent& component)
	{
		return add_camera_component(component);
	}

	bool Entity::remove_camera_component()
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null || !impl->storage.registry.any_of<CameraComponent>(handle))
		{
			return false;
		}
		impl->storage.registry.remove<CameraComponent>(handle);
		impl->storage.dirty = true;
		return true;
	}

	bool Entity::has_light_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null && impl->storage.registry.any_of<LightComponent>(handle);
	}

	LightComponent Entity::get_light_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle != entt::null)
		{
			if (const LightComponent* component = impl->storage.registry.try_get<LightComponent>(handle))
			{
				return *component;
			}
		}
		return {};
	}

	bool Entity::add_light_component(const LightComponent& component)
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null)
		{
			return false;
		}
		impl->storage.registry.emplace_or_replace<LightComponent>(handle, component);
		impl->storage.dirty = true;
		return true;
	}

	bool Entity::set_light_component(const LightComponent& component)
	{
		return add_light_component(component);
	}

	bool Entity::remove_light_component()
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null || !impl->storage.registry.any_of<LightComponent>(handle))
		{
			return false;
		}
		impl->storage.registry.remove<LightComponent>(handle);
		impl->storage.dirty = true;
		return true;
	}

	bool Entity::has_mesh_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null && impl->storage.registry.any_of<MeshComponent>(handle);
	}

	MeshComponent Entity::get_mesh_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle != entt::null)
		{
			if (const MeshComponent* component = impl->storage.registry.try_get<MeshComponent>(handle))
			{
				return *component;
			}
		}
		return {};
	}

	bool Entity::add_mesh_component(const MeshComponent& component)
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null)
		{
			return false;
		}
		impl->storage.registry.emplace_or_replace<MeshComponent>(handle, component);
		impl->storage.dirty = true;
		return true;
	}

	bool Entity::set_mesh_component(const MeshComponent& component)
	{
		return add_mesh_component(component);
	}

	bool Entity::remove_mesh_component()
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle == entt::null || !impl->storage.registry.any_of<MeshComponent>(handle))
		{
			return false;
		}
		impl->storage.registry.remove<MeshComponent>(handle);
		impl->storage.dirty = true;
		return true;
	}

	bool Entity::has_component(SceneComponentType type) const
	{
		switch (type)
		{
		case SceneComponentType::Name:
		case SceneComponentType::Transform:
			return is_valid();
		case SceneComponentType::Camera:
			return has_camera_component();
		case SceneComponentType::Light:
			return has_light_component();
		case SceneComponentType::Mesh:
			return has_mesh_component();
		default:
			return false;
		}
	}

	std::vector<SceneComponentType> Entity::get_component_types() const
	{
		std::vector<SceneComponentType> result{};
		if (!is_valid())
		{
			return result;
		}

		result.push_back(SceneComponentType::Name);
		result.push_back(SceneComponentType::Transform);
		if (has_camera_component())
		{
			result.push_back(SceneComponentType::Camera);
		}
		if (has_light_component())
		{
			result.push_back(SceneComponentType::Light);
		}
		if (has_mesh_component())
		{
			result.push_back(SceneComponentType::Mesh);
		}
		return result;
	}

	bool Entity::read_component(SceneComponentType type, void* out_component, size_t component_size) const
	{
		if (!out_component)
		{
			return false;
		}

		switch (type)
		{
		case SceneComponentType::Name:
			if (component_size != sizeof(NameComponent)) return false;
			*static_cast<NameComponent*>(out_component) = get_name_component();
			return true;
		case SceneComponentType::Transform:
			if (component_size != sizeof(TransformComponent)) return false;
			*static_cast<TransformComponent*>(out_component) = get_transform_component();
			return true;
		case SceneComponentType::Camera:
			if (component_size != sizeof(CameraComponent) || !has_camera_component()) return false;
			*static_cast<CameraComponent*>(out_component) = get_camera_component();
			return true;
		case SceneComponentType::Light:
			if (component_size != sizeof(LightComponent) || !has_light_component()) return false;
			*static_cast<LightComponent*>(out_component) = get_light_component();
			return true;
		case SceneComponentType::Mesh:
			if (component_size != sizeof(MeshComponent) || !has_mesh_component()) return false;
			*static_cast<MeshComponent*>(out_component) = get_mesh_component();
			return true;
		default:
			return false;
		}
	}

	bool Entity::write_component(SceneComponentType type, const void* component_data, size_t component_size)
	{
		if (!component_data)
		{
			return false;
		}

		switch (type)
		{
		case SceneComponentType::Name:
			return component_size == sizeof(NameComponent) && set_name_component(*static_cast<const NameComponent*>(component_data));
		case SceneComponentType::Transform:
			return component_size == sizeof(TransformComponent) && set_transform_component(*static_cast<const TransformComponent*>(component_data));
		case SceneComponentType::Camera:
			return component_size == sizeof(CameraComponent) && set_camera_component(*static_cast<const CameraComponent*>(component_data));
		case SceneComponentType::Light:
			return component_size == sizeof(LightComponent) && set_light_component(*static_cast<const LightComponent*>(component_data));
		case SceneComponentType::Mesh:
			return component_size == sizeof(MeshComponent) && set_mesh_component(*static_cast<const MeshComponent*>(component_data));
		default:
			return false;
		}
	}

	Entity Entity::get_parent() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const HierarchyComponent* hierarchy = find_hierarchy_const(impl, m_id);
		return hierarchy && hierarchy->parent != 0 ? Entity(m_impl, hierarchy->parent) : Entity{};
	}

	std::vector<Entity> Entity::get_children() const
	{
		std::vector<Entity> result{};
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const HierarchyComponent* hierarchy = find_hierarchy_const(impl, m_id);
		if (!hierarchy)
		{
			return result;
		}

		result.reserve(hierarchy->children.size());
		for (EntityId child_id : hierarchy->children)
		{
			if (find_handle(impl, child_id) != entt::null)
			{
				result.emplace_back(m_impl, child_id);
			}
		}
		return result;
	}

	bool Entity::set_parent(const Entity& parent)
	{
		if (!is_valid() || !parent.is_valid() || std::static_pointer_cast<Scene::Impl>(parent.m_impl) != std::static_pointer_cast<Scene::Impl>(m_impl))
		{
			return false;
		}
		Scene scene(std::static_pointer_cast<Scene::Impl>(m_impl));
		return scene.reparent_entity(m_id, parent.get_id());
	}

	bool Entity::clear_parent()
	{
		if (!is_valid())
		{
			return false;
		}
		Scene scene(std::static_pointer_cast<Scene::Impl>(m_impl));
		return scene.reparent_entity(m_id, 0);
	}

	Entity Entity::create_child(std::string_view name)
	{
		if (!is_valid())
		{
			return {};
		}
		Scene scene(std::static_pointer_cast<Scene::Impl>(m_impl));
		return scene.create_entity(name, *this);
	}

	bool Entity::destroy()
	{
		Scene scene(std::static_pointer_cast<Scene::Impl>(m_impl));
		return scene.destroy_entity(m_id);
	}

	Scene::Scene(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}

	Scene Scene::create(std::string_view name)
	{
		auto impl = std::make_shared<Impl>();
		impl->storage.name = name.empty() ? "Untitled Scene" : std::string(name);
		return Scene(std::move(impl));
	}

	Scene Scene::load_from_file(const std::filesystem::path& path, std::string* out_error)
	{
		std::ifstream input(path);
		if (!input.is_open())
		{
			make_scene_error(out_error, "Failed to open scene file.");
			return {};
		}

		json root{};
		try
		{
			input >> root;
		}
		catch (const std::exception& exception)
		{
			make_scene_error(out_error, exception.what());
			return {};
		}

		Scene scene = Scene::create(root.value("name", std::string("Untitled Scene")));
		scene.m_impl->storage.source_path = path;
		const uint32_t version = root.value("version", k_scene_file_version);
		if (version > k_scene_file_version)
		{
			make_scene_error(out_error, "Scene file version is newer than this runtime supports.");
			return {};
		}

		const json entities_json = root.value("entities", json::array());
		if (!entities_json.is_array())
		{
			make_scene_error(out_error, "Scene file contains an invalid entity list.");
			return {};
		}

		std::unordered_map<EntityId, EntityId> desired_parents{};
		EntityId max_id = 0;
		for (const json& entity_json : entities_json)
		{
			const EntityId id = entity_json.value("id", static_cast<EntityId>(0));
			if (id == 0)
			{
				continue;
			}

			Entity entity = create_entity_internal(scene.m_impl, id, entity_json.value("name", std::string("Entity")));
			if (!entity.is_valid())
			{
				continue;
			}

			desired_parents.emplace(id, entity_json.value("parent", static_cast<EntityId>(0)));
			max_id = std::max(max_id, id);

			const json transform_json = entity_json.value("transform", json::object());
			TransformComponent transform = entity.get_transform_component();
			transform.position = from_json_vec3(transform_json.value("position", json::array()), transform.position);
			transform.rotation_euler_degrees = from_json_vec3(transform_json.value("rotation_euler_degrees", json::array()), transform.rotation_euler_degrees);
			transform.scale = from_json_vec3(transform_json.value("scale", json::array({ 1.0f, 1.0f, 1.0f })), transform.scale);
			entity.set_transform_component(transform);

			if (entity_json.contains("camera"))
			{
				const json& camera_json = entity_json["camera"];
				CameraComponent camera{};
				camera.primary = camera_json.value("primary", camera.primary);
				camera.projection = static_cast<CameraProjectionType>(camera_json.value("projection", static_cast<int32_t>(camera.projection)));
				camera.fov_y_degrees = camera_json.value("fov_y_degrees", camera.fov_y_degrees);
				camera.near_plane = camera_json.value("near_plane", camera.near_plane);
				camera.far_plane = camera_json.value("far_plane", camera.far_plane);
				camera.orthographic_height = camera_json.value("orthographic_height", camera.orthographic_height);
				entity.add_camera_component(camera);
			}

			if (entity_json.contains("light"))
			{
				const json& light_json = entity_json["light"];
				LightComponent light{};
				light.type = static_cast<LightType>(light_json.value("type", static_cast<int32_t>(light.type)));
				light.color = from_json_vec3(light_json.value("color", json::array()), light.color);
				light.intensity = light_json.value("intensity", light.intensity);
				light.range = light_json.value("range", light.range);
				light.inner_cone_angle_degrees = light_json.value("inner_cone_angle_degrees", light.inner_cone_angle_degrees);
				light.outer_cone_angle_degrees = light_json.value("outer_cone_angle_degrees", light.outer_cone_angle_degrees);
				entity.add_light_component(light);
			}

			if (entity_json.contains("mesh"))
			{
				const json& mesh_json = entity_json["mesh"];
				MeshComponent mesh{};
				mesh.asset_path = mesh_json.value("asset_path", std::string{});
				mesh.mesh_index = mesh_json.value("mesh_index", 0u);
				mesh.visible = mesh_json.value("visible", true);
				entity.add_mesh_component(mesh);
			}
		}

		const EntityId saved_next_entity_id = root.value("next_entity_id", static_cast<EntityId>(0));
		scene.m_impl->storage.next_entity_id = std::max(scene.m_impl->storage.next_entity_id, max_id + 1);
		if (saved_next_entity_id > 0)
		{
			scene.m_impl->storage.next_entity_id = std::max(scene.m_impl->storage.next_entity_id, saved_next_entity_id);
		}
		for (const auto& [id, parent] : desired_parents)
		{
			if (parent != 0)
			{
				scene.reparent_entity(id, parent);
			}
		}

		scene.m_impl->storage.dirty = false;
		if (out_error)
		{
			out_error->clear();
		}
		return scene;
	}

	bool Scene::is_valid() const
	{
		return m_impl != nullptr;
	}

	const std::string& Scene::get_name() const
	{
		static const std::string k_empty_name{};
		return m_impl ? m_impl->storage.name : k_empty_name;
	}

	void Scene::set_name(std::string_view name)
	{
		if (!m_impl)
		{
			return;
		}
		m_impl->storage.name = name.empty() ? "Untitled Scene" : std::string(name);
		m_impl->storage.dirty = true;
	}

	Entity Scene::create_entity(std::string_view name)
	{
		return create_entity(name, Entity{});
	}

	Entity Scene::create_entity(std::string_view name, const Entity& parent)
	{
		if (!m_impl)
		{
			return {};
		}

		Entity entity = create_entity_internal(m_impl, 0, name);
		if (entity.is_valid() && parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl)
		{
			reparent_entity(entity.get_id(), parent.get_id());
		}
		m_impl->storage.dirty = true;
		return entity;
	}

	bool Scene::destroy_entity(EntityId id)
	{
		if (!m_impl)
		{
			return false;
		}
		const bool destroyed = destroy_entity_recursive(m_impl->storage, id);
		if (destroyed)
		{
			m_impl->storage.dirty = true;
		}
		return destroyed;
	}

	bool Scene::reparent_entity(EntityId id, EntityId new_parent_id)
	{
		if (!m_impl || id == 0 || id == new_parent_id)
		{
			return false;
		}
		if (find_handle(m_impl, id) == entt::null)
		{
			return false;
		}
		if (new_parent_id != 0 && (find_handle(m_impl, new_parent_id) == entt::null || is_scene_descendant(m_impl, id, new_parent_id)))
		{
			return false;
		}

		detach_from_parent(m_impl->storage, id);
		if (new_parent_id != 0)
		{
			attach_to_parent(m_impl->storage, id, new_parent_id);
		}
		m_impl->storage.dirty = true;
		return true;
	}

	Entity Scene::find_entity(EntityId id) const
	{
		return find_handle(m_impl, id) != entt::null ? Entity(m_impl, id) : Entity{};
	}

	std::vector<Entity> Scene::get_entities() const
	{
		std::vector<Entity> result{};
		if (!m_impl)
		{
			return result;
		}
		result.reserve(m_impl->storage.entity_order.size());
		for (EntityId id : m_impl->storage.entity_order)
		{
			if (find_handle(m_impl, id) != entt::null)
			{
				result.emplace_back(m_impl, id);
			}
		}
		return result;
	}

	std::vector<Entity> Scene::get_root_entities() const
	{
		std::vector<Entity> result{};
		if (!m_impl)
		{
			return result;
		}
		for (EntityId id : m_impl->storage.entity_order)
		{
			if (const HierarchyComponent* hierarchy = find_hierarchy_const(m_impl, id); hierarchy && hierarchy->parent == 0)
			{
				result.emplace_back(m_impl, id);
			}
		}
		return result;
	}

	std::vector<Entity> Scene::get_entities_with_component(SceneComponentType type) const
	{
		std::vector<Entity> result{};
		for (const Entity& entity : get_entities())
		{
			if (entity.has_component(type))
			{
				result.push_back(entity);
			}
		}
		return result;
	}

	std::vector<Entity> Scene::get_entities_with_components(const std::vector<SceneComponentType>& required_types) const
	{
		std::vector<Entity> result{};
		for (const Entity& entity : get_entities())
		{
			bool matches = true;
			for (SceneComponentType type : required_types)
			{
				if (!entity.has_component(type))
				{
					matches = false;
					break;
				}
			}
			if (matches)
			{
				result.push_back(entity);
			}
		}
		return result;
	}

	uint32_t Scene::get_entity_count() const
	{
		return m_impl ? static_cast<uint32_t>(m_impl->storage.entities.size()) : 0;
	}

	Entity Scene::instantiate_model(const Model& model, const Entity& parent, std::string_view root_name_override)
	{
		if (!m_impl || !model.is_valid())
		{
			return {};
		}

		const Entity resolved_parent = parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl ? parent : Entity{};
		if (model.nodes.empty())
		{
			Entity root = create_entity(root_name_override.empty() ? (model.name.empty() ? "Model" : model.name) : root_name_override, resolved_parent);
			if (model.meshes.size() == 1)
			{
				MeshComponent mesh{};
				mesh.asset_path = model.source_path.generic_string();
				mesh.mesh_index = 0;
				root.add_mesh_component(mesh);
			}
			else
			{
				for (uint32_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index)
				{
					Entity child = create_entity(model.meshes[mesh_index].name.empty() ? ("Mesh_" + std::to_string(mesh_index)) : model.meshes[mesh_index].name, root);
					MeshComponent mesh{};
					mesh.asset_path = model.source_path.generic_string();
					mesh.mesh_index = mesh_index;
					child.add_mesh_component(mesh);
				}
			}
			return root;
		}

		auto spawn_model_node = [&](auto&& self, uint32_t node_index, const Entity& parent_entity, std::string_view override_name) -> Entity
		{
			const ModelNode& node = model.nodes[node_index];
			const std::string entity_name = override_name.empty()
				? (node.name.empty() ? std::string("Entity") : node.name)
				: std::string(override_name);
			Entity entity = create_entity(entity_name, parent_entity);
			entity.set_transform_component(matrix_to_transform_component(node.local_transform));
			if (node.mesh_index >= 0)
			{
				MeshComponent mesh{};
				mesh.asset_path = model.source_path.generic_string();
				mesh.mesh_index = static_cast<uint32_t>(node.mesh_index);
				entity.add_mesh_component(mesh);
			}
			for (uint32_t child_index : node.children)
			{
				self(self, child_index, entity, {});
			}
			return entity;
		};

		if (model.root_nodes.size() == 1)
		{
			return spawn_model_node(spawn_model_node, model.root_nodes.front(), resolved_parent, root_name_override);
		}

		Entity synthetic_root = create_entity(root_name_override.empty() ? (model.name.empty() ? "Model" : model.name) : root_name_override, resolved_parent);
		for (uint32_t root_node : model.root_nodes)
		{
			spawn_model_node(spawn_model_node, root_node, synthetic_root, {});
		}
		return synthetic_root;
	}

	Entity Scene::instantiate_ashasset(const AshAsset& asset, const Entity& parent, std::string_view root_name_override)
	{
		if (!m_impl || !asset.is_valid())
		{
			return {};
		}

		const Entity resolved_parent = parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl ? parent : Entity{};
		auto spawn_asset_node = [&](auto&& self, uint32_t node_index, const Entity& parent_entity, std::string_view override_name) -> Entity
		{
			const AshAssetNode& node = asset.nodes[node_index];
			const std::string entity_name = override_name.empty()
				? (node.name.empty() ? std::string("Entity") : node.name)
				: std::string(override_name);
			Entity entity = create_entity(entity_name, parent_entity);
			entity.set_transform_component(node.transform);
			if (node.camera.has_value())
			{
				entity.add_camera_component(node.camera.value());
			}
			if (node.light.has_value())
			{
				entity.add_light_component(node.light.value());
			}
			if (node.mesh.has_value())
			{
				entity.add_mesh_component(node.mesh.value());
			}
			for (uint32_t child_index : node.children)
			{
				self(self, child_index, entity, {});
			}
			return entity;
		};

		if (asset.root_nodes.size() == 1)
		{
			return spawn_asset_node(spawn_asset_node, asset.root_nodes.front(), resolved_parent, root_name_override);
		}

		Entity synthetic_root = create_entity(root_name_override.empty() ? (asset.name.empty() ? "AshAsset" : asset.name) : root_name_override, resolved_parent);
		for (uint32_t root_node : asset.root_nodes)
		{
			spawn_asset_node(spawn_asset_node, root_node, synthetic_root, {});
		}
		return synthetic_root;
	}

	Entity Scene::instantiate_asset(AssetDatabase& database, const std::filesystem::path& path, const Entity& parent)
	{
		std::string extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (extension == ".ashasset")
		{
			std::shared_ptr<const AshAsset> asset{};
			return database.load_ashasset_by_path(path, asset) ? instantiate_ashasset(*asset, parent) : Entity{};
		}

		std::shared_ptr<const Model> model{};
		return database.load_model_by_path(path, model) ? instantiate_model(*model, parent) : Entity{};
	}

	bool Scene::save_to_file(const std::filesystem::path& path, std::string* out_error)
	{
		if (!m_impl)
		{
			make_scene_error(out_error, "Scene is invalid.");
			return false;
		}

		std::error_code directory_error{};
		if (!path.parent_path().empty())
		{
			std::filesystem::create_directories(path.parent_path(), directory_error);
			if (directory_error)
			{
				make_scene_error(out_error, directory_error.message());
				return false;
			}
		}

		json root{};
		root["version"] = k_scene_file_version;
		root["name"] = m_impl->storage.name;
		root["next_entity_id"] = m_impl->storage.next_entity_id;
		root["entities"] = json::array();

		for (EntityId id : m_impl->storage.entity_order)
		{
			const entt::entity handle = find_handle(m_impl, id);
			if (handle == entt::null)
			{
				continue;
			}

			const NameComponent& name = m_impl->storage.registry.get<NameComponent>(handle);
			const TransformComponent& transform = m_impl->storage.registry.get<TransformComponent>(handle);
			const HierarchyComponent& hierarchy = m_impl->storage.registry.get<HierarchyComponent>(handle);

			json entity_json{};
			entity_json["id"] = id;
			entity_json["parent"] = hierarchy.parent;
			entity_json["name"] = name.value;
			entity_json["transform"] =
			{
				{ "position", to_json_vec3(transform.position) },
				{ "rotation_euler_degrees", to_json_vec3(transform.rotation_euler_degrees) },
				{ "scale", to_json_vec3(transform.scale) },
			};

			if (const CameraComponent* camera = m_impl->storage.registry.try_get<CameraComponent>(handle))
			{
				entity_json["camera"] =
				{
					{ "primary", camera->primary },
					{ "projection", static_cast<int32_t>(camera->projection) },
					{ "fov_y_degrees", camera->fov_y_degrees },
					{ "near_plane", camera->near_plane },
					{ "far_plane", camera->far_plane },
					{ "orthographic_height", camera->orthographic_height },
				};
			}

			if (const LightComponent* light = m_impl->storage.registry.try_get<LightComponent>(handle))
			{
				entity_json["light"] =
				{
					{ "type", static_cast<int32_t>(light->type) },
					{ "color", to_json_vec3(light->color) },
					{ "intensity", light->intensity },
					{ "range", light->range },
					{ "inner_cone_angle_degrees", light->inner_cone_angle_degrees },
					{ "outer_cone_angle_degrees", light->outer_cone_angle_degrees },
				};
			}

			if (const MeshComponent* mesh = m_impl->storage.registry.try_get<MeshComponent>(handle))
			{
				entity_json["mesh"] =
				{
					{ "asset_path", mesh->asset_path },
					{ "mesh_index", mesh->mesh_index },
					{ "visible", mesh->visible },
				};
			}

			root["entities"].push_back(std::move(entity_json));
		}

		std::ofstream output(path);
		if (!output.is_open())
		{
			make_scene_error(out_error, "Failed to open scene output file.");
			return false;
		}

		try
		{
			output << root.dump(2);
		}
		catch (const std::exception& exception)
		{
			make_scene_error(out_error, exception.what());
			return false;
		}

		m_impl->storage.source_path = path;
		m_impl->storage.dirty = false;
		if (out_error)
		{
			out_error->clear();
		}
		return true;
	}

	const std::filesystem::path& Scene::get_source_path() const
	{
		static const std::filesystem::path k_empty_path{};
		return m_impl ? m_impl->storage.source_path : k_empty_path;
	}

	bool Scene::is_dirty() const
	{
		return m_impl ? m_impl->storage.dirty : false;
	}

	void Scene::mark_clean()
	{
		if (m_impl)
		{
			m_impl->storage.dirty = false;
		}
	}

	const SceneComponentDesc* get_scene_component_descriptor(SceneComponentType type)
	{
		for (const SceneComponentDesc& desc : k_scene_component_descs)
		{
			if (desc.type == type)
			{
				return &desc;
			}
		}
		return nullptr;
	}

	const SceneComponentDesc* get_scene_component_descriptors(uint32_t* out_count)
	{
		if (out_count)
		{
			*out_count = static_cast<uint32_t>(std::size(k_scene_component_descs));
		}
		return k_scene_component_descs;
	}

	const SceneEnumDesc* get_scene_enum_descriptor(const char* name)
	{
		if (!name)
		{
			return nullptr;
		}

		for (const SceneEnumDesc& desc : k_scene_enum_descs)
		{
			if (desc.name && std::string_view(desc.name) == std::string_view(name))
			{
				return &desc;
			}
		}
		return nullptr;
	}
}

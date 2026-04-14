#include "Scene.h"

#include "Base/hlog.h"
#include <algorithm>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <json.hpp>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		struct EntityRecord
		{
			EntityId id = 0;
			EntityId parent = 0;
			NameComponent name{};
			TransformComponent transform{};
			std::optional<CameraComponent> camera{};
			std::optional<LightComponent> light{};
			std::optional<MeshComponent> mesh{};
			std::vector<EntityId> children{};
		};

		struct SceneStorage
		{
			std::string name = "Untitled Scene";
			std::filesystem::path source_path{};
			std::unordered_map<EntityId, EntityRecord> entities{};
			std::vector<EntityId> entity_order{};
			EntityId next_entity_id = 1;
			bool dirty = false;
		};

		static constexpr uint32_t k_scene_file_version = 1;

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
		static auto find_record(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> EntityRecord*
		{
			if (!impl)
			{
				return nullptr;
			}
			auto it = impl->storage.entities.find(id);
			return it != impl->storage.entities.end() ? &it->second : nullptr;
		}

		static auto find_record_const(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> const EntityRecord*
		{
			if (!impl)
			{
				return nullptr;
			}
			auto it = impl->storage.entities.find(id);
			return it != impl->storage.entities.end() ? &it->second : nullptr;
		}

		static auto detach_from_parent(SceneStorage& storage, EntityRecord& record) -> void
		{
			if (record.parent == 0)
			{
				return;
			}

			auto parent_it = storage.entities.find(record.parent);
			if (parent_it != storage.entities.end())
			{
				auto& siblings = parent_it->second.children;
				siblings.erase(std::remove(siblings.begin(), siblings.end(), record.id), siblings.end());
			}
			record.parent = 0;
		}

		static auto destroy_entity_recursive(SceneStorage& storage, EntityId id) -> bool
		{
			auto it = storage.entities.find(id);
			if (it == storage.entities.end())
			{
				return false;
			}

			const std::vector<EntityId> children = it->second.children;
			for (EntityId child_id : children)
			{
				destroy_entity_recursive(storage, child_id);
			}

			detach_from_parent(storage, it->second);
			storage.entity_order.erase(std::remove(storage.entity_order.begin(), storage.entity_order.end(), id), storage.entity_order.end());
			storage.entities.erase(it);
			return true;
		}
	}

	Entity::Entity(std::shared_ptr<Impl> impl, EntityId id)
		: m_impl(std::move(impl)), m_id(id)
	{
	}

	bool Entity::is_valid() const
	{
		return find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id) != nullptr;
	}

	EntityId Entity::get_id() const
	{
		return is_valid() ? m_id : 0;
	}

	NameComponent Entity::get_name_component() const
	{
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->name;
		}
		return {};
	}

	bool Entity::set_name_component(const NameComponent& component)
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			record->name = component;
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
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
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->transform;
		}
		return {};
	}

	bool Entity::set_transform_component(const TransformComponent& component)
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			record->transform = component;
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
	}

	bool Entity::has_camera_component() const
	{
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->camera.has_value();
		}
		return false;
	}

	CameraComponent Entity::get_camera_component() const
	{
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->camera.value_or(CameraComponent{});
		}
		return {};
	}

	bool Entity::add_camera_component(const CameraComponent& component)
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			record->camera = component;
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
	}

	bool Entity::set_camera_component(const CameraComponent& component)
	{
		return add_camera_component(component);
	}

	bool Entity::remove_camera_component()
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			if (!record->camera.has_value())
			{
				return false;
			}
			record->camera.reset();
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
	}

	bool Entity::has_light_component() const
	{
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->light.has_value();
		}
		return false;
	}

	LightComponent Entity::get_light_component() const
	{
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->light.value_or(LightComponent{});
		}
		return {};
	}

	bool Entity::add_light_component(const LightComponent& component)
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			record->light = component;
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
	}

	bool Entity::set_light_component(const LightComponent& component)
	{
		return add_light_component(component);
	}

	bool Entity::remove_light_component()
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			if (!record->light.has_value())
			{
				return false;
			}
			record->light.reset();
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
	}

	bool Entity::has_mesh_component() const
	{
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->mesh.has_value();
		}
		return false;
	}

	MeshComponent Entity::get_mesh_component() const
	{
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->mesh.value_or(MeshComponent{});
		}
		return {};
	}

	bool Entity::add_mesh_component(const MeshComponent& component)
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			record->mesh = component;
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
	}

	bool Entity::set_mesh_component(const MeshComponent& component)
	{
		return add_mesh_component(component);
	}

	bool Entity::remove_mesh_component()
	{
		if (EntityRecord* record = find_record(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			if (!record->mesh.has_value())
			{
				return false;
			}
			record->mesh.reset();
			std::static_pointer_cast<Scene::Impl>(m_impl)->storage.dirty = true;
			return true;
		}
		return false;
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
			if (component_size != sizeof(NameComponent))
			{
				return false;
			}
			*static_cast<NameComponent*>(out_component) = get_name_component();
			return true;
		case SceneComponentType::Transform:
			if (component_size != sizeof(TransformComponent))
			{
				return false;
			}
			*static_cast<TransformComponent*>(out_component) = get_transform_component();
			return true;
		case SceneComponentType::Camera:
			if (!has_camera_component() || component_size != sizeof(CameraComponent))
			{
				return false;
			}
			*static_cast<CameraComponent*>(out_component) = get_camera_component();
			return true;
		case SceneComponentType::Light:
			if (!has_light_component() || component_size != sizeof(LightComponent))
			{
				return false;
			}
			*static_cast<LightComponent*>(out_component) = get_light_component();
			return true;
		case SceneComponentType::Mesh:
			if (!has_mesh_component() || component_size != sizeof(MeshComponent))
			{
				return false;
			}
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
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			return record->parent != 0 ? Entity(m_impl, record->parent) : Entity{};
		}
		return {};
	}

	std::vector<Entity> Entity::get_children() const
	{
		std::vector<Entity> result{};
		if (const EntityRecord* record = find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), m_id))
		{
			result.reserve(record->children.size());
			for (EntityId child_id : record->children)
			{
				if (find_record_const(std::static_pointer_cast<Scene::Impl>(m_impl), child_id))
				{
					result.push_back(Entity(m_impl, child_id));
				}
			}
		}
		return result;
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
		catch (const std::exception& e)
		{
			make_scene_error(out_error, e.what());
			return {};
		}

		auto impl = std::make_shared<Impl>();
		try
		{
			const uint32_t version = root.value("version", k_scene_file_version);
			if (version > k_scene_file_version)
			{
				make_scene_error(out_error, "Scene file version is newer than this runtime supports.");
				return {};
			}

			impl->storage.name = root.value("name", std::string("Untitled Scene"));
			impl->storage.source_path = path;
			impl->storage.next_entity_id = root.value("next_entity_id", static_cast<EntityId>(1));

			const json entities_json = root.value("entities", json::array());
			if (!entities_json.is_array())
			{
				make_scene_error(out_error, "Scene file contains an invalid entity list.");
				return {};
			}

			EntityId max_id = 0;
			for (const json& entity_json : entities_json)
			{
				EntityRecord record{};
				record.id = entity_json.value("id", static_cast<EntityId>(0));
				if (record.id == 0)
				{
					continue;
				}

				record.parent = entity_json.value("parent", static_cast<EntityId>(0));
				record.name.value = entity_json.value("name", std::string("Entity"));

				const json transform_json = entity_json.value("transform", json::object());
				record.transform.position = from_json_vec3(transform_json.value("position", json::array()), record.transform.position);
				record.transform.rotation_euler_degrees = from_json_vec3(transform_json.value("rotation_euler_degrees", json::array()), record.transform.rotation_euler_degrees);
				record.transform.scale = from_json_vec3(transform_json.value("scale", json::array({ 1.0f, 1.0f, 1.0f })), record.transform.scale);

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
					record.camera = camera;
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
					record.light = light;
				}

				if (entity_json.contains("mesh"))
				{
					const json& mesh_json = entity_json["mesh"];
					MeshComponent mesh{};
					mesh.asset_path = mesh_json.value("asset_path", std::string{});
					mesh.visible = mesh_json.value("visible", mesh.visible);
					record.mesh = mesh;
				}

				max_id = std::max(max_id, record.id);
				impl->storage.entity_order.push_back(record.id);
				impl->storage.entities.emplace(record.id, std::move(record));
			}

			impl->storage.next_entity_id = std::max(impl->storage.next_entity_id, max_id + 1);
		}
		catch (const std::exception& e)
		{
			make_scene_error(out_error, e.what());
			return {};
		}

		for (EntityId id : impl->storage.entity_order)
		{
			EntityRecord* record = find_record(impl, id);
			if (!record)
			{
				continue;
			}

			record->children.clear();
			if (record->parent == 0 || record->parent == record->id)
			{
				record->parent = 0;
				continue;
			}

			EntityRecord* parent = find_record(impl, record->parent);
			if (!parent)
			{
				record->parent = 0;
				continue;
			}

			parent->children.push_back(record->id);
		}

		impl->storage.dirty = false;
		if (out_error)
		{
			out_error->clear();
		}
		return Scene(std::move(impl));
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

		const EntityId id = m_impl->storage.next_entity_id++;
		EntityRecord record{};
		record.id = id;
		record.name.value = name.empty() ? "Entity" : std::string(name);
		const bool attach_to_parent = parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl;
		if (attach_to_parent)
		{
			record.parent = parent.get_id();
		}

		m_impl->storage.entity_order.push_back(id);
		m_impl->storage.entities.emplace(id, std::move(record));

		if (attach_to_parent)
		{
			if (EntityRecord* parent_record = find_record(m_impl, parent.get_id()))
			{
				parent_record->children.push_back(id);
			}
		}

		m_impl->storage.dirty = true;
		return Entity(m_impl, id);
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

	Entity Scene::find_entity(EntityId id) const
	{
		if (!m_impl || !find_record_const(m_impl, id))
		{
			return {};
		}
		return Entity(m_impl, id);
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
			if (find_record_const(m_impl, id))
			{
				result.push_back(Entity(m_impl, id));
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
			const EntityRecord* record = find_record_const(m_impl, id);
			if (record && record->parent == 0)
			{
				result.push_back(Entity(m_impl, id));
			}
		}
		return result;
	}

	uint32_t Scene::get_entity_count() const
	{
		return m_impl ? static_cast<uint32_t>(m_impl->storage.entities.size()) : 0;
	}

	bool Scene::save_to_file(const std::filesystem::path& path, std::string* out_error)
	{
		if (!m_impl)
		{
			make_scene_error(out_error, "Scene is invalid.");
			return false;
		}

		std::error_code directory_error{};
		const std::filesystem::path parent_path = path.parent_path();
		if (!parent_path.empty())
		{
			std::filesystem::create_directories(parent_path, directory_error);
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
			const EntityRecord* record = find_record_const(m_impl, id);
			if (!record)
			{
				continue;
			}

			json entity_json{};
			entity_json["id"] = record->id;
			entity_json["parent"] = record->parent;
			entity_json["name"] = record->name.value;
			entity_json["transform"] =
			{
				{ "position", to_json_vec3(record->transform.position) },
				{ "rotation_euler_degrees", to_json_vec3(record->transform.rotation_euler_degrees) },
				{ "scale", to_json_vec3(record->transform.scale) },
			};

			if (record->camera.has_value())
			{
				const CameraComponent& camera = record->camera.value();
				entity_json["camera"] =
				{
					{ "primary", camera.primary },
					{ "projection", static_cast<int32_t>(camera.projection) },
					{ "fov_y_degrees", camera.fov_y_degrees },
					{ "near_plane", camera.near_plane },
					{ "far_plane", camera.far_plane },
					{ "orthographic_height", camera.orthographic_height },
				};
			}

			if (record->light.has_value())
			{
				const LightComponent& light = record->light.value();
				entity_json["light"] =
				{
					{ "type", static_cast<int32_t>(light.type) },
					{ "color", to_json_vec3(light.color) },
					{ "intensity", light.intensity },
					{ "range", light.range },
					{ "inner_cone_angle_degrees", light.inner_cone_angle_degrees },
					{ "outer_cone_angle_degrees", light.outer_cone_angle_degrees },
				};
			}

			if (record->mesh.has_value())
			{
				const MeshComponent& mesh = record->mesh.value();
				entity_json["mesh"] =
				{
					{ "asset_path", mesh.asset_path },
					{ "visible", mesh.visible },
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
		catch (const std::exception& e)
		{
			make_scene_error(out_error, e.what());
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

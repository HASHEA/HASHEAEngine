#include "Services/SceneService.h"
#include <json.hpp>
#include <string_view>

namespace AshEditor
{
	namespace
	{
		using json = nlohmann::json;

		auto to_json_vec2(const glm::vec2& value) -> json
		{
			return json::array({ value.x, value.y });
		}

		auto to_json_vec3(const glm::vec3& value) -> json
		{
			return json::array({ value.x, value.y, value.z });
		}

		auto to_json_vec4(const glm::vec4& value) -> json
		{
			return json::array({ value.x, value.y, value.z, value.w });
		}

		auto from_json_vec2(const json& value, const glm::vec2& fallback) -> glm::vec2
		{
			if (!value.is_array() || value.size() != 2)
			{
				return fallback;
			}

			glm::vec2 result = fallback;
			result.x = value[0].get<float>();
			result.y = value[1].get<float>();
			return result;
		}

		auto from_json_vec3(const json& value, const glm::vec3& fallback) -> glm::vec3
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

		auto from_json_vec4(const json& value, const glm::vec4& fallback) -> glm::vec4
		{
			if (!value.is_array() || value.size() != 4)
			{
				return fallback;
			}

			glm::vec4 result = fallback;
			result.x = value[0].get<float>();
			result.y = value[1].get<float>();
			result.z = value[2].get<float>();
			result.w = value[3].get<float>();
			return result;
		}

		auto read_unsigned_integral(const uint8_t* data, uint32_t size) -> uint64_t
		{
			switch (size)
			{
			case 1: return *reinterpret_cast<const uint8_t*>(data);
			case 2: return *reinterpret_cast<const uint16_t*>(data);
			case 4: return *reinterpret_cast<const uint32_t*>(data);
			case 8: return *reinterpret_cast<const uint64_t*>(data);
			default: return 0;
			}
		}

		auto read_signed_integral(const uint8_t* data, uint32_t size) -> int64_t
		{
			switch (size)
			{
			case 1: return *reinterpret_cast<const int8_t*>(data);
			case 2: return *reinterpret_cast<const int16_t*>(data);
			case 4: return *reinterpret_cast<const int32_t*>(data);
			case 8: return *reinterpret_cast<const int64_t*>(data);
			default: return 0;
			}
		}

		void write_unsigned_integral(uint8_t* data, uint32_t size, uint64_t value)
		{
			switch (size)
			{
			case 1:
				*reinterpret_cast<uint8_t*>(data) = static_cast<uint8_t>(value);
				return;
			case 2:
				*reinterpret_cast<uint16_t*>(data) = static_cast<uint16_t>(value);
				return;
			case 4:
				*reinterpret_cast<uint32_t*>(data) = static_cast<uint32_t>(value);
				return;
			case 8:
				*reinterpret_cast<uint64_t*>(data) = static_cast<uint64_t>(value);
				return;
			default:
				return;
			}
		}

		void write_signed_integral(uint8_t* data, uint32_t size, int64_t value)
		{
			switch (size)
			{
			case 1:
				*reinterpret_cast<int8_t*>(data) = static_cast<int8_t>(value);
				return;
			case 2:
				*reinterpret_cast<int16_t*>(data) = static_cast<int16_t>(value);
				return;
			case 4:
				*reinterpret_cast<int32_t*>(data) = static_cast<int32_t>(value);
				return;
			case 8:
				*reinterpret_cast<int64_t*>(data) = static_cast<int64_t>(value);
				return;
			default:
				return;
			}
		}

		auto serialize_component_payload(const void* component_data, const AshEngine::SceneComponentDesc& component_desc) -> json
		{
			json component_json = json::object();
			const uint8_t* base = static_cast<const uint8_t*>(component_data);
			for (uint32_t property_index = 0; property_index < component_desc.property_count; ++property_index)
			{
				const AshEngine::ScenePropertyDesc& property_desc = component_desc.properties[property_index];
				const uint8_t* property_data = base + property_desc.offset;
				switch (property_desc.type)
				{
				case AshEngine::ScenePropertyType::Bool:
					component_json[property_desc.name] = *reinterpret_cast<const bool*>(property_data);
					break;
				case AshEngine::ScenePropertyType::Int32:
					component_json[property_desc.name] = read_signed_integral(property_data, property_desc.size);
					break;
				case AshEngine::ScenePropertyType::UInt32:
				case AshEngine::ScenePropertyType::Enum:
					component_json[property_desc.name] = read_unsigned_integral(property_data, property_desc.size);
					break;
				case AshEngine::ScenePropertyType::Float:
					component_json[property_desc.name] = *reinterpret_cast<const float*>(property_data);
					break;
				case AshEngine::ScenePropertyType::Vec2:
					component_json[property_desc.name] = to_json_vec2(*reinterpret_cast<const glm::vec2*>(property_data));
					break;
				case AshEngine::ScenePropertyType::Vec3:
					component_json[property_desc.name] = to_json_vec3(*reinterpret_cast<const glm::vec3*>(property_data));
					break;
				case AshEngine::ScenePropertyType::Vec4:
					component_json[property_desc.name] = to_json_vec4(*reinterpret_cast<const glm::vec4*>(property_data));
					break;
				case AshEngine::ScenePropertyType::String:
					component_json[property_desc.name] = *reinterpret_cast<const std::string*>(property_data);
					break;
				default:
					break;
				}
			}
			return component_json;
		}

		void deserialize_component_payload(const json& component_json, const AshEngine::SceneComponentDesc& component_desc, void* component_data)
		{
			uint8_t* base = static_cast<uint8_t*>(component_data);
			for (uint32_t property_index = 0; property_index < component_desc.property_count; ++property_index)
			{
				const AshEngine::ScenePropertyDesc& property_desc = component_desc.properties[property_index];
				if (!component_json.contains(property_desc.name))
				{
					continue;
				}

				const json& property_json = component_json[property_desc.name];
				uint8_t* property_data = base + property_desc.offset;
				switch (property_desc.type)
				{
				case AshEngine::ScenePropertyType::Bool:
					*reinterpret_cast<bool*>(property_data) = property_json.get<bool>();
					break;
				case AshEngine::ScenePropertyType::Int32:
					write_signed_integral(property_data, property_desc.size, property_json.get<int64_t>());
					break;
				case AshEngine::ScenePropertyType::UInt32:
				case AshEngine::ScenePropertyType::Enum:
					write_unsigned_integral(property_data, property_desc.size, property_json.get<uint64_t>());
					break;
				case AshEngine::ScenePropertyType::Float:
					*reinterpret_cast<float*>(property_data) = property_json.get<float>();
					break;
				case AshEngine::ScenePropertyType::Vec2:
					*reinterpret_cast<glm::vec2*>(property_data) = from_json_vec2(property_json, *reinterpret_cast<glm::vec2*>(property_data));
					break;
				case AshEngine::ScenePropertyType::Vec3:
					*reinterpret_cast<glm::vec3*>(property_data) = from_json_vec3(property_json, *reinterpret_cast<glm::vec3*>(property_data));
					break;
				case AshEngine::ScenePropertyType::Vec4:
					*reinterpret_cast<glm::vec4*>(property_data) = from_json_vec4(property_json, *reinterpret_cast<glm::vec4*>(property_data));
					break;
				case AshEngine::ScenePropertyType::String:
					*reinterpret_cast<std::string*>(property_data) = property_json.get<std::string>();
					break;
				default:
					break;
				}
			}
		}

		template<typename Component>
		auto make_component_snapshot(AshEngine::SceneComponentType type, const Component& component) -> std::optional<SceneComponentSnapshot>
		{
			const AshEngine::SceneComponentDesc* component_desc = AshEngine::get_scene_component_descriptor(type);
			if (!component_desc)
			{
				return std::nullopt;
			}

			SceneComponentSnapshot snapshot{};
			snapshot.type = type;
			snapshot.serialized_value = serialize_component_payload(&component, *component_desc).dump();
			return snapshot;
		}

		auto capture_component_snapshot(const AshEngine::Entity& entity, AshEngine::SceneComponentType type) -> std::optional<SceneComponentSnapshot>
		{
			switch (type)
			{
			case AshEngine::SceneComponentType::Name:
				return make_component_snapshot(type, entity.get_name_component());
			case AshEngine::SceneComponentType::Transform:
				return make_component_snapshot(type, entity.get_transform_component());
			case AshEngine::SceneComponentType::Camera:
				return entity.has_camera_component()
					? make_component_snapshot(type, entity.get_camera_component())
					: std::nullopt;
			case AshEngine::SceneComponentType::Light:
				return entity.has_light_component()
					? make_component_snapshot(type, entity.get_light_component())
					: std::nullopt;
			case AshEngine::SceneComponentType::Mesh:
				return entity.has_mesh_component()
					? make_component_snapshot(type, entity.get_mesh_component())
					: std::nullopt;
			default:
				return std::nullopt;
			}
		}

		auto capture_snapshot_recursive(const SceneService& scene_service, const AshEngine::Entity& entity) -> SceneEntitySnapshot
		{
			SceneEntitySnapshot snapshot{};
			snapshot.entity_id = entity.get_id();
			snapshot.sibling_index = scene_service.get_entity_sibling_index(entity.get_id());

			for (AshEngine::SceneComponentType type : entity.get_component_types())
			{
				if (std::optional<SceneComponentSnapshot> component_snapshot = capture_component_snapshot(entity, type); component_snapshot.has_value())
				{
					snapshot.components.push_back(std::move(*component_snapshot));
				}
			}

			for (const AshEngine::Entity& child : entity.get_children())
			{
				snapshot.children.push_back(capture_snapshot_recursive(scene_service, child));
			}
			return snapshot;
		}

		template<typename Component>
		bool apply_required_component(AshEngine::Entity entity, const SceneComponentSnapshot& snapshot)
		{
			const AshEngine::SceneComponentDesc* component_desc = AshEngine::get_scene_component_descriptor(snapshot.type);
			if (!component_desc)
			{
				return false;
			}

			const json payload = json::parse(snapshot.serialized_value, nullptr, false);
			if (payload.is_discarded())
			{
				return false;
			}

			Component component{};
			deserialize_component_payload(payload, *component_desc, &component);
			return entity.write_component(snapshot.type, &component, sizeof(Component));
		}

		template<typename Component>
		bool apply_optional_component(
			AshEngine::Entity entity,
			const SceneComponentSnapshot& snapshot,
			bool (AshEngine::Entity::*has_fn)() const,
			bool (AshEngine::Entity::*set_fn)(const Component&),
			bool (AshEngine::Entity::*add_fn)(const Component&))
		{
			const AshEngine::SceneComponentDesc* component_desc = AshEngine::get_scene_component_descriptor(snapshot.type);
			if (!component_desc)
			{
				return false;
			}

			const json payload = json::parse(snapshot.serialized_value, nullptr, false);
			if (payload.is_discarded())
			{
				return false;
			}

			Component component{};
			deserialize_component_payload(payload, *component_desc, &component);
			return (entity.*has_fn)()
				? (entity.*set_fn)(component)
				: (entity.*add_fn)(component);
		}

		bool remove_optional_component(AshEngine::Entity entity, AshEngine::SceneComponentType type)
		{
			switch (type)
			{
			case AshEngine::SceneComponentType::Camera:
				return !entity.has_camera_component() || entity.remove_camera_component();
			case AshEngine::SceneComponentType::Light:
				return !entity.has_light_component() || entity.remove_light_component();
			case AshEngine::SceneComponentType::Mesh:
				return !entity.has_mesh_component() || entity.remove_mesh_component();
			default:
				return true;
			}
		}

		bool snapshot_has_component(const SceneEntitySnapshot& snapshot, AshEngine::SceneComponentType type)
		{
			for (const SceneComponentSnapshot& component_snapshot : snapshot.components)
			{
				if (component_snapshot.type == type)
				{
					return true;
				}
			}
			return false;
		}

		bool apply_entity_snapshot(AshEngine::Entity entity, const SceneEntitySnapshot& snapshot)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			const AshEngine::SceneComponentType optional_types[] =
			{
				AshEngine::SceneComponentType::Camera,
				AshEngine::SceneComponentType::Light,
				AshEngine::SceneComponentType::Mesh,
			};
			for (AshEngine::SceneComponentType type : optional_types)
			{
				if (!snapshot_has_component(snapshot, type) && !remove_optional_component(entity, type))
				{
					return false;
				}
			}

			for (const SceneComponentSnapshot& component_snapshot : snapshot.components)
			{
				switch (component_snapshot.type)
				{
				case AshEngine::SceneComponentType::Name:
					if (!apply_required_component<AshEngine::NameComponent>(entity, component_snapshot))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Transform:
					if (!apply_required_component<AshEngine::TransformComponent>(entity, component_snapshot))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Camera:
					if (!apply_optional_component<AshEngine::CameraComponent>(
						entity,
						component_snapshot,
						&AshEngine::Entity::has_camera_component,
						&AshEngine::Entity::set_camera_component,
						&AshEngine::Entity::add_camera_component))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Light:
					if (!apply_optional_component<AshEngine::LightComponent>(
						entity,
						component_snapshot,
						&AshEngine::Entity::has_light_component,
						&AshEngine::Entity::set_light_component,
						&AshEngine::Entity::add_light_component))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Mesh:
					if (!apply_optional_component<AshEngine::MeshComponent>(
						entity,
						component_snapshot,
						&AshEngine::Entity::has_mesh_component,
						&AshEngine::Entity::set_mesh_component,
						&AshEngine::Entity::add_mesh_component))
					{
						return false;
					}
					break;
				default:
					return false;
				}
			}

			return true;
		}

		auto restore_snapshot_recursive(SceneService& scene_service, const SceneEntitySnapshot& snapshot, EntityId parent_id) -> AshEngine::Entity
		{
			AshEngine::Entity entity = scene_service.create_entity_with_id(snapshot.entity_id, "Entity", parent_id, snapshot.sibling_index);
			if (!entity.is_valid() || !apply_entity_snapshot(entity, snapshot))
			{
				if (entity.is_valid())
				{
					scene_service.destroy_entity(entity.get_id());
				}
				return {};
			}

			for (const SceneEntitySnapshot& child_snapshot : snapshot.children)
			{
				if (!restore_snapshot_recursive(scene_service, child_snapshot, entity.get_id()).is_valid())
				{
					scene_service.destroy_entity(entity.get_id());
					return {};
				}
			}
			return entity;
		}
	}

	bool SceneService::initialize(const std::filesystem::path& startup_scene_path)
	{
		if (!startup_scene_path.empty() && std::filesystem::exists(startup_scene_path))
		{
			if (load_scene(startup_scene_path))
			{
				return true;
			}

			new_scene("Untitled Scene");
			return false;
		}

		new_scene("Untitled Scene");
		if (!startup_scene_path.empty())
		{
			m_activeScenePath = startup_scene_path;
		}
		return true;
	}

	AshEngine::Scene& SceneService::get_active_scene()
	{
		return m_activeScene;
	}

	const AshEngine::Scene& SceneService::get_active_scene() const
	{
		return m_activeScene;
	}

	AshEngine::Entity SceneService::find_entity(EntityId id) const
	{
		return m_activeScene.find_entity(id);
	}

	uint32_t SceneService::get_entity_sibling_index(EntityId id) const
	{
		return m_activeScene.get_entity_sibling_index(id);
	}

	AshEngine::Entity SceneService::create_entity(const std::string& name, EntityId parent_id)
	{
		return create_entity(name, parent_id, AshEngine::k_scene_append_sibling_index);
	}

	AshEngine::Entity SceneService::create_entity(const std::string& name, EntityId parent_id, uint32_t sibling_index)
	{
		const AshEngine::Entity parent = parent_id != 0 ? m_activeScene.find_entity(parent_id) : AshEngine::Entity{};
		return parent.is_valid()
			? m_activeScene.create_entity(name, parent, sibling_index)
			: m_activeScene.create_entity(name, {}, sibling_index);
	}

	AshEngine::Entity SceneService::create_entity_with_id(EntityId id, const std::string& name, EntityId parent_id)
	{
		return create_entity_with_id(id, name, parent_id, AshEngine::k_scene_append_sibling_index);
	}

	AshEngine::Entity SceneService::create_entity_with_id(EntityId id, const std::string& name, EntityId parent_id, uint32_t sibling_index)
	{
		if (id == 0)
		{
			return {};
		}

		const AshEngine::Entity parent = parent_id != 0 ? m_activeScene.find_entity(parent_id) : AshEngine::Entity{};
		return parent.is_valid()
			? m_activeScene.create_entity_with_id(id, name, parent, sibling_index)
			: m_activeScene.create_entity_with_id(id, name, {}, sibling_index);
	}

	bool SceneService::rename_entity(EntityId id, std::string_view name)
	{
		AshEngine::Entity entity = find_entity(id);
		return entity.is_valid() && entity.set_name(name);
	}

	bool SceneService::destroy_entity(EntityId id)
	{
		return id != 0 && m_activeScene.destroy_entity(id);
	}

	bool SceneService::reparent_entity(EntityId id, EntityId new_parent_id)
	{
		return reparent_entity(id, new_parent_id, AshEngine::k_scene_append_sibling_index);
	}

	bool SceneService::reparent_entity(EntityId id, EntityId new_parent_id, uint32_t sibling_index)
	{
		return can_reparent_entity(id, new_parent_id) && m_activeScene.reparent_entity(id, new_parent_id, sibling_index);
	}

	bool SceneService::can_reparent_entity(EntityId id, EntityId new_parent_id) const
	{
		if (id == 0 || id == new_parent_id)
		{
			return false;
		}

		const AshEngine::Entity entity = find_entity(id);
		if (!entity.is_valid())
		{
			return false;
		}

		if (new_parent_id == 0)
		{
			return true;
		}

		const AshEngine::Entity new_parent = find_entity(new_parent_id);
		if (!new_parent.is_valid())
		{
			return false;
		}

		return !is_descendant_of(new_parent_id, id);
	}

	bool SceneService::is_descendant_of(EntityId id, EntityId potential_ancestor_id) const
	{
		if (id == 0 || potential_ancestor_id == 0)
		{
			return false;
		}

		AshEngine::Entity current = find_entity(id).get_parent();
		while (current.is_valid())
		{
			if (current.get_id() == potential_ancestor_id)
			{
				return true;
			}
			current = current.get_parent();
		}
		return false;
	}

	std::optional<SceneEntitySnapshot> SceneService::capture_entity_snapshot(EntityId id) const
	{
		const AshEngine::Entity entity = find_entity(id);
		if (!entity.is_valid())
		{
			return std::nullopt;
		}
		return capture_snapshot_recursive(*this, entity);
	}

	AshEngine::Entity SceneService::restore_entity_snapshot(const SceneEntitySnapshot& snapshot, EntityId parent_id)
	{
		return restore_snapshot_recursive(*this, snapshot, parent_id);
	}

	void SceneService::new_scene(const std::string& name)
	{
		m_activeScene = AshEngine::Scene::create(name);
		m_activeScenePath.clear();
		create_default_entities();
		m_activeScene.mark_clean();
	}

	bool SceneService::load_scene(const std::filesystem::path& path)
	{
		std::string error{};
		AshEngine::Scene loaded_scene = AshEngine::Scene::load_from_file(path, &error);
		if (!loaded_scene.is_valid())
		{
			return false;
		}

		m_activeScene = std::move(loaded_scene);
		m_activeScenePath = path;
		return true;
	}

	bool SceneService::save_scene(const std::filesystem::path& path)
	{
		if (path.empty())
		{
			return false;
		}

		std::string error{};
		if (!m_activeScene.save_to_file(path, &error))
		{
			return false;
		}

		m_activeScenePath = path;
		return true;
	}

	const std::filesystem::path& SceneService::get_active_scene_path() const
	{
		return m_activeScenePath;
	}

	void SceneService::create_default_entities()
	{
		AshEngine::Entity root = m_activeScene.create_entity("SceneRoot");

		AshEngine::Entity camera = m_activeScene.create_entity("MainCamera", root);
		AshEngine::TransformComponent camera_transform = camera.get_transform_component();
		camera_transform.position = { 0.0f, 2.0f, -8.0f };
		camera.set_transform_component(camera_transform);
		camera.add_camera_component({});

		AshEngine::Entity light = m_activeScene.create_entity("DirectionalLight", root);
		AshEngine::TransformComponent light_transform = light.get_transform_component();
		light_transform.rotation_euler_degrees = { -45.0f, 0.0f, 0.0f };
		light.set_transform_component(light_transform);
		light.add_light_component({});
	}
}

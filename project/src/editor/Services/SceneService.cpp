#include "Services/SceneService.h"
#include <string_view>

namespace AshEditor
{
	namespace
	{
		auto capture_snapshot_recursive(const AshEngine::Entity& entity) -> SceneEntitySnapshot
		{
			SceneEntitySnapshot snapshot{};
			snapshot.name = entity.get_name();
			snapshot.transform = entity.get_transform_component();
			if (entity.has_camera_component())
			{
				snapshot.camera = entity.get_camera_component();
			}
			if (entity.has_light_component())
			{
				snapshot.light = entity.get_light_component();
			}
			if (entity.has_mesh_component())
			{
				snapshot.mesh = entity.get_mesh_component();
			}

			for (const AshEngine::Entity& child : entity.get_children())
			{
				snapshot.children.push_back(capture_snapshot_recursive(child));
			}
			return snapshot;
		}

		auto restore_snapshot_recursive(SceneService& scene_service, const SceneEntitySnapshot& snapshot, EntityId parent_id) -> AshEngine::Entity
		{
			AshEngine::Entity entity = scene_service.create_entity(snapshot.name, parent_id);
			if (!entity.is_valid())
			{
				return {};
			}

			entity.set_transform_component(snapshot.transform);
			if (snapshot.camera.has_value())
			{
				entity.add_camera_component(*snapshot.camera);
			}
			if (snapshot.light.has_value())
			{
				entity.add_light_component(*snapshot.light);
			}
			if (snapshot.mesh.has_value())
			{
				entity.add_mesh_component(*snapshot.mesh);
			}

			for (const SceneEntitySnapshot& child_snapshot : snapshot.children)
			{
				restore_snapshot_recursive(scene_service, child_snapshot, entity.get_id());
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

	AshEngine::Entity SceneService::create_entity(const std::string& name, EntityId parent_id)
	{
		const AshEngine::Entity parent = parent_id != 0 ? m_activeScene.find_entity(parent_id) : AshEngine::Entity{};
		return parent.is_valid() ? m_activeScene.create_entity(name, parent) : m_activeScene.create_entity(name);
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
		return can_reparent_entity(id, new_parent_id) && m_activeScene.reparent_entity(id, new_parent_id);
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
		return capture_snapshot_recursive(entity);
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

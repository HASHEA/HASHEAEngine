#include "Services/SceneService.h"

namespace AshEditor
{
	bool SceneService::initialize(const std::filesystem::path& startup_scene_path)
	{
		if (!startup_scene_path.empty() && std::filesystem::exists(startup_scene_path))
		{
			return load_scene(startup_scene_path);
		}

		new_scene("Untitled Scene");
		m_activeScenePath = startup_scene_path;
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

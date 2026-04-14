#pragma once
#include "Function/Scene/Scene.h"
#include <filesystem>

namespace AshEditor
{
	using EntityId = AshEngine::EntityId;

	class SceneService
	{
	public:
		bool initialize(const std::filesystem::path& startup_scene_path);

		AshEngine::Scene& get_active_scene();
		const AshEngine::Scene& get_active_scene() const;

		AshEngine::Entity find_entity(EntityId id) const;

		AshEngine::Entity create_entity(const std::string& name, EntityId parent_id = 0);
		void new_scene(const std::string& name);

		bool load_scene(const std::filesystem::path& path);
		bool save_scene(const std::filesystem::path& path);

		const std::filesystem::path& get_active_scene_path() const;

	private:
		void create_default_entities();

	private:
		AshEngine::Scene m_activeScene{};
		std::filesystem::path m_activeScenePath{};
	};
}

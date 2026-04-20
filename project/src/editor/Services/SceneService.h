#pragma once
#include "Function/Scene/Scene.h"
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace AshEditor
{
	using EntityId = AshEngine::EntityId;

	struct SceneEntitySnapshot
	{
		std::string name{};
		AshEngine::TransformComponent transform{};
		std::optional<AshEngine::CameraComponent> camera{};
		std::optional<AshEngine::LightComponent> light{};
		std::optional<AshEngine::MeshComponent> mesh{};
		std::vector<SceneEntitySnapshot> children{};
	};

	class SceneService
	{
	public:
		bool initialize(const std::filesystem::path& startup_scene_path);

		AshEngine::Scene& get_active_scene();
		const AshEngine::Scene& get_active_scene() const;

		AshEngine::Entity find_entity(EntityId id) const;

		AshEngine::Entity create_entity(const std::string& name, EntityId parent_id = 0);
		bool rename_entity(EntityId id, std::string_view name);
		bool destroy_entity(EntityId id);
		bool reparent_entity(EntityId id, EntityId new_parent_id);
		bool can_reparent_entity(EntityId id, EntityId new_parent_id) const;
		bool is_descendant_of(EntityId id, EntityId potential_ancestor_id) const;
		std::optional<SceneEntitySnapshot> capture_entity_snapshot(EntityId id) const;
		AshEngine::Entity restore_entity_snapshot(const SceneEntitySnapshot& snapshot, EntityId parent_id = 0);
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

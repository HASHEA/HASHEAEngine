#pragma once
#include "Function/Scene/Scene.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	using EntityId = AshEngine::EntityId;

	struct SceneComponentSnapshot
	{
		AshEngine::SceneComponentType type = AshEngine::SceneComponentType::Name;
		std::string serialized_value{};
	};

	struct SceneEntitySnapshot
	{
		EntityId entity_id = 0;
		uint32_t sibling_index = AshEngine::k_scene_append_sibling_index;
		std::vector<SceneComponentSnapshot> components{};
		std::vector<SceneEntitySnapshot> children{};
	};

	class SceneService
	{
	public:
		bool initialize(const std::filesystem::path& startup_scene_path);

		AshEngine::Scene& get_active_scene();
		const AshEngine::Scene& get_active_scene() const;

		AshEngine::Entity find_entity(EntityId id) const;
		uint32_t get_entity_sibling_index(EntityId id) const;

		AshEngine::Entity create_entity(const std::string& name, EntityId parent_id = 0);
		AshEngine::Entity create_entity(const std::string& name, EntityId parent_id, uint32_t sibling_index);
		AshEngine::Entity create_entity_with_id(EntityId id, const std::string& name, EntityId parent_id = 0);
		AshEngine::Entity create_entity_with_id(EntityId id, const std::string& name, EntityId parent_id, uint32_t sibling_index);
		bool rename_entity(EntityId id, std::string_view name);
		bool destroy_entity(EntityId id);
		bool reparent_entity(EntityId id, EntityId new_parent_id);
		bool reparent_entity(EntityId id, EntityId new_parent_id, uint32_t sibling_index);
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

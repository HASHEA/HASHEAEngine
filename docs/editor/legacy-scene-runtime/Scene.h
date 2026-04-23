#pragma once
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include <string>
#include <vector>

namespace AshEditor
{
	struct SceneEntityData
	{
		EntityId id = 0;
		EntityId parent_id = 0;
		NameComponent name{};
		TransformComponent transform{};
	};

	class Scene
	{
	public:
		Scene() = default;
		explicit Scene(std::string name);

		void reset(std::string name);

		const std::string& get_name() const;
		void set_name(std::string name);

		bool is_dirty() const;
		void mark_dirty(bool dirty = true);

		SceneEntityData& create_entity(std::string name, EntityId parent_id = 0);
		bool destroy_entity(EntityId id);
		SceneEntityData* find_entity(EntityId id);
		const SceneEntityData* find_entity(EntityId id) const;

		std::vector<SceneEntityData>& get_entities();
		const std::vector<SceneEntityData>& get_entities() const;

		EntityId get_next_entity_id() const;
		void set_next_entity_id(EntityId next_entity_id);

	private:
		std::string m_name = "Untitled Scene";
		std::vector<SceneEntityData> m_entities{};
		EntityId m_nextEntityId = 1;
		bool m_dirty = false;
	};
}

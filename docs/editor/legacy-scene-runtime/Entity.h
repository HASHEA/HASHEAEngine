#pragma once
#include <cstdint>

namespace AshEditor
{
	using EntityId = uint64_t;

	struct SceneEntityData;
	class Scene;

	class Entity
	{
	public:
		Entity() = default;
		Entity(Scene* scene, EntityId id);

		bool is_valid() const;
		EntityId get_id() const;
		SceneEntityData* get();
		const SceneEntityData* get() const;

	private:
		Scene* m_scene = nullptr;
		EntityId m_id = 0;
	};
}

#include "Scene/Scene.h"
#include <algorithm>
#include <utility>

namespace AshEditor
{
	Entity::Entity(Scene* scene, EntityId id)
		: m_scene(scene)
		, m_id(id)
	{
	}

	bool Entity::is_valid() const
	{
		return m_scene != nullptr && m_scene->find_entity(m_id) != nullptr;
	}

	EntityId Entity::get_id() const
	{
		return m_id;
	}

	SceneEntityData* Entity::get()
	{
		return m_scene ? m_scene->find_entity(m_id) : nullptr;
	}

	const SceneEntityData* Entity::get() const
	{
		return m_scene ? m_scene->find_entity(m_id) : nullptr;
	}

	Scene::Scene(std::string name)
		: m_name(std::move(name))
	{
	}

	void Scene::reset(std::string name)
	{
		m_name = std::move(name);
		m_entities.clear();
		m_nextEntityId = 1;
		m_dirty = false;
	}

	const std::string& Scene::get_name() const
	{
		return m_name;
	}

	void Scene::set_name(std::string name)
	{
		m_name = std::move(name);
		m_dirty = true;
	}

	bool Scene::is_dirty() const
	{
		return m_dirty;
	}

	void Scene::mark_dirty(bool dirty)
	{
		m_dirty = dirty;
	}

	SceneEntityData& Scene::create_entity(std::string name, EntityId parent_id)
	{
		SceneEntityData entity{};
		entity.id = m_nextEntityId++;
		entity.parent_id = parent_id;
		entity.name.name = std::move(name);
		m_entities.push_back(std::move(entity));
		m_dirty = true;
		return m_entities.back();
	}

	bool Scene::destroy_entity(EntityId id)
	{
		const auto entityIt = std::find_if(
			m_entities.begin(),
			m_entities.end(),
			[id](const SceneEntityData& entity) { return entity.id == id; });
		if (entityIt == m_entities.end())
		{
			return false;
		}

		m_entities.erase(
			std::remove_if(
				m_entities.begin(),
				m_entities.end(),
				[id](const SceneEntityData& entity)
				{
					return entity.id == id || entity.parent_id == id;
				}),
			m_entities.end());
		m_dirty = true;
		return true;
	}

	SceneEntityData* Scene::find_entity(EntityId id)
	{
		const auto entityIt = std::find_if(
			m_entities.begin(),
			m_entities.end(),
			[id](const SceneEntityData& entity) { return entity.id == id; });
		return entityIt != m_entities.end() ? &(*entityIt) : nullptr;
	}

	const SceneEntityData* Scene::find_entity(EntityId id) const
	{
		const auto entityIt = std::find_if(
			m_entities.begin(),
			m_entities.end(),
			[id](const SceneEntityData& entity) { return entity.id == id; });
		return entityIt != m_entities.end() ? &(*entityIt) : nullptr;
	}

	std::vector<SceneEntityData>& Scene::get_entities()
	{
		return m_entities;
	}

	const std::vector<SceneEntityData>& Scene::get_entities() const
	{
		return m_entities;
	}

	EntityId Scene::get_next_entity_id() const
	{
		return m_nextEntityId;
	}

	void Scene::set_next_entity_id(EntityId next_entity_id)
	{
		m_nextEntityId = next_entity_id;
	}
}

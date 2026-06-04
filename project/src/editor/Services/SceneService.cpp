#include "Services/SceneService.h"
#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/SceneSnapshotComponentUtils.h"

#include <algorithm>

namespace AshEditor
{
	namespace
	{
		bool ContainsSceneEntityId(const std::vector<SceneEntityId>& vecEntityIds, SceneEntityId uEntityId)
		{
			return std::find(vecEntityIds.begin(), vecEntityIds.end(), uEntityId) != vecEntityIds.end();
		}

		SceneEntitySnapshot CaptureSnapshotRecursive(
			const SceneService& refSceneService,
			const AshEngine::Entity& refEntity)
		{
			SceneEntitySnapshot snapshot{};
			snapshot.uEntityId = refEntity.get_id();
			snapshot.uSiblingIndex = refSceneService.GetEntitySiblingIndex(refEntity.get_id());

			snapshot.vecComponents = SceneSnapshotComponentUtils::CaptureComponentSnapshots(refEntity);

			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				snapshot.vecChildren.push_back(CaptureSnapshotRecursive(refSceneService, refChild));
			}
			return snapshot;
		}

		AshEngine::Entity RestoreSnapshotRecursive(
			SceneService& refSceneService,
			const SceneEntitySnapshot& refSnapshot,
			const SceneEntityId uParentId)
		{
			AshEngine::Entity entity =
				refSceneService.CreateEntityWithId(refSnapshot.uEntityId, "Entity", uParentId, refSnapshot.uSiblingIndex);
			if (!entity.is_valid() || !SceneSnapshotComponentUtils::ApplyEntitySnapshot(entity, refSnapshot))
			{
				if (entity.is_valid())
				{
					refSceneService.DestroyEntity(entity.get_id());
				}
				return {};
			}

			for (const SceneEntitySnapshot& refChildSnapshot : refSnapshot.vecChildren)
			{
				if (!RestoreSnapshotRecursive(refSceneService, refChildSnapshot, entity.get_id()).is_valid())
				{
					refSceneService.DestroyEntity(entity.get_id());
					return {};
				}
			}
			return entity;
		}
	}

	bool SceneService::Initialize(const std::filesystem::path& pathStartupScene)
	{
		if (!pathStartupScene.empty() && std::filesystem::exists(pathStartupScene))
		{
			if (LoadScene(pathStartupScene))
			{
				return true;
			}

			NewScene("Untitled Scene");
			return false;
		}

		NewScene("Untitled Scene");
		if (!pathStartupScene.empty())
		{
			_pathActiveScene = pathStartupScene;
		}
		return true;
	}

	void SceneService::SetEventBus(EditorEventBus* pEventBus)
	{
		if (_pEventBus == pEventBus)
		{
			return;
		}

		UnsubscribeActiveSceneChanges();
		_pEventBus = pEventBus;
		SubscribeActiveSceneChanges();
	}

	AshEngine::Scene& SceneService::GetActiveScene()
	{
		return _activeScene;
	}

	const AshEngine::Scene& SceneService::GetActiveScene() const
	{
		return _activeScene;
	}

	AshEngine::Entity SceneService::FindEntity(const SceneEntityId uSceneEntityId) const
	{
		return _activeScene.find_entity(uSceneEntityId);
	}

	uint32_t SceneService::GetEntitySiblingIndex(const SceneEntityId uSceneEntityId) const
	{
		return _activeScene.get_entity_sibling_index(uSceneEntityId);
	}

	AshEngine::Entity SceneService::CreateEntity(const std::string& strName, const SceneEntityId uParentId)
	{
		return CreateEntity(strName, uParentId, kSceneAppendSiblingIndex);
	}

	AshEngine::Entity SceneService::CreateEntity(
		const std::string& strName,
		const SceneEntityId uParentId,
		const uint32_t uSiblingIndex)
	{
		const AshEngine::Entity parent = uParentId != 0 ? _activeScene.find_entity(uParentId) : AshEngine::Entity{};
		AshEngine::Entity entity = parent.is_valid()
			? _activeScene.create_entity(strName, parent, uSiblingIndex)
			: _activeScene.create_entity(strName, {}, uSiblingIndex);
		if (!entity.is_valid())
		{
			HLogWarning(
				"SceneService failed to create entity '{}' (parent={}, sibling_index={}).",
				strName,
				static_cast<unsigned long long>(uParentId),
				static_cast<unsigned int>(uSiblingIndex));
		}
		return entity;
	}

	AshEngine::Entity SceneService::CreateEntityWithId(
		const SceneEntityId uSceneEntityId,
		const std::string& strName,
		const SceneEntityId uParentId)
	{
		return CreateEntityWithId(uSceneEntityId, strName, uParentId, kSceneAppendSiblingIndex);
	}

	AshEngine::Entity SceneService::CreateEntityWithId(
		const SceneEntityId uSceneEntityId,
		const std::string& strName,
		const SceneEntityId uParentId,
		const uint32_t uSiblingIndex)
	{
		if (uSceneEntityId == 0)
		{
			return {};
		}

		const AshEngine::Entity parent = uParentId != 0 ? _activeScene.find_entity(uParentId) : AshEngine::Entity{};
		AshEngine::Entity entity = parent.is_valid()
			? _activeScene.create_entity_with_id(uSceneEntityId, strName, parent, uSiblingIndex)
			: _activeScene.create_entity_with_id(uSceneEntityId, strName, {}, uSiblingIndex);
		if (!entity.is_valid())
		{
			HLogWarning(
				"SceneService failed to create entity '{}' with explicit id={} (parent={}, sibling_index={}).",
				strName,
				static_cast<unsigned long long>(uSceneEntityId),
				static_cast<unsigned long long>(uParentId),
				static_cast<unsigned int>(uSiblingIndex));
		}
		return entity;
	}

	bool SceneService::RenameEntity(const SceneEntityId uSceneEntityId, const std::string_view svName)
	{
		AshEngine::Entity entity = FindEntity(uSceneEntityId);
		return entity.is_valid() && entity.set_name(svName);
	}

	bool SceneService::DestroyEntity(const SceneEntityId uSceneEntityId)
	{
		const bool bDestroyed = uSceneEntityId != 0 && _activeScene.destroy_entity(uSceneEntityId);
		if (!bDestroyed)
		{
			HLogWarning(
				"SceneService failed to destroy entity id={}.",
				static_cast<unsigned long long>(uSceneEntityId));
		}
		return bDestroyed;
	}

	bool SceneService::ReparentEntity(const SceneEntityId uSceneEntityId, const SceneEntityId uNewParentId)
	{
		return ReparentEntity(uSceneEntityId, uNewParentId, kSceneAppendSiblingIndex);
	}

	bool SceneService::ReparentEntity(
		const SceneEntityId uSceneEntityId,
		const SceneEntityId uNewParentId,
		const uint32_t uSiblingIndex)
	{
		return CanReparentEntity(uSceneEntityId, uNewParentId) &&
			_activeScene.reparent_entity(uSceneEntityId, uNewParentId, uSiblingIndex);
	}

	bool SceneService::CanReparentEntity(const SceneEntityId uSceneEntityId, const SceneEntityId uNewParentId) const
	{
		if (uSceneEntityId == 0 || uSceneEntityId == uNewParentId)
		{
			return false;
		}

		const AshEngine::Entity entity = FindEntity(uSceneEntityId);
		if (!entity.is_valid())
		{
			return false;
		}

		if (uNewParentId == 0)
		{
			return true;
		}

		const AshEngine::Entity newParent = FindEntity(uNewParentId);
		if (!newParent.is_valid())
		{
			return false;
		}

		return !IsDescendantOf(uNewParentId, uSceneEntityId);
	}

	bool SceneService::IsDescendantOf(const SceneEntityId uSceneEntityId, const SceneEntityId uPotentialAncestorId) const
	{
		if (uSceneEntityId == 0 || uPotentialAncestorId == 0)
		{
			return false;
		}

		AshEngine::Entity current = FindEntity(uSceneEntityId).get_parent();
		while (current.is_valid())
		{
			if (current.get_id() == uPotentialAncestorId)
			{
				return true;
			}
			current = current.get_parent();
		}
		return false;
	}

	std::vector<SceneEntityId> SceneService::BuildTopLevelEntityIds(const std::vector<uint64_t>& vecCandidateEntityIds) const
	{
		std::vector<SceneEntityId> vecEntityIds{};
		vecEntityIds.reserve(vecCandidateEntityIds.size());
		for (const uint64_t uCandidateEntityId : vecCandidateEntityIds)
		{
			const SceneEntityId uEntityId = static_cast<SceneEntityId>(uCandidateEntityId);
			if (uEntityId == 0 || ContainsSceneEntityId(vecEntityIds, uEntityId))
			{
				continue;
			}

			const AshEngine::Entity entity = FindEntity(uEntityId);
			if (entity.is_valid())
			{
				vecEntityIds.push_back(uEntityId);
			}
		}

		std::vector<SceneEntityId> vecTopLevelEntityIds{};
		vecTopLevelEntityIds.reserve(vecEntityIds.size());
		for (const SceneEntityId uEntityId : vecEntityIds)
		{
			bool bHasCandidateAncestor = false;
			for (const SceneEntityId uPotentialAncestorId : vecEntityIds)
			{
				if (uPotentialAncestorId != uEntityId && IsDescendantOf(uEntityId, uPotentialAncestorId))
				{
					bHasCandidateAncestor = true;
					break;
				}
			}

			if (!bHasCandidateAncestor)
			{
				vecTopLevelEntityIds.push_back(uEntityId);
			}
		}
		return vecTopLevelEntityIds;
	}

	std::optional<SceneEntitySnapshot> SceneService::CaptureEntitySnapshot(const SceneEntityId uSceneEntityId) const
	{
		const AshEngine::Entity entity = FindEntity(uSceneEntityId);
		if (!entity.is_valid())
		{
			HLogWarning(
				"SceneService failed to capture snapshot because entity id={} is invalid.",
				static_cast<unsigned long long>(uSceneEntityId));
			return std::nullopt;
		}
		return CaptureSnapshotRecursive(*this, entity);
	}

	AshEngine::Entity SceneService::RestoreEntitySnapshot(const SceneEntitySnapshot& refSnapshot, const SceneEntityId uParentId)
	{
		AshEngine::Entity entity = RestoreSnapshotRecursive(*this, refSnapshot, uParentId);
		if (!entity.is_valid())
		{
			HLogWarning(
				"SceneService failed to restore snapshot for entity id={} under parent={}.",
				static_cast<unsigned long long>(refSnapshot.uEntityId),
				static_cast<unsigned long long>(uParentId));
		}
		return entity;
	}

	void SceneService::NewScene(const std::string& strName)
	{
		UnsubscribeActiveSceneChanges();
		_activeScene = AshEngine::Scene::create(strName);
		_pathActiveScene.clear();
		CreateDefaultEntities();
		_activeScene.mark_clean();
		SubscribeActiveSceneChanges();
	}

	bool SceneService::LoadScene(const std::filesystem::path& pathScene)
	{
		std::string strError{};
		AshEngine::Scene loadedScene = AshEngine::Scene::load_from_file(pathScene, &strError);
		if (!loadedScene.is_valid())
		{
			return false;
		}

		UnsubscribeActiveSceneChanges();
		_activeScene = std::move(loadedScene);
		_pathActiveScene = pathScene;
		SubscribeActiveSceneChanges();
		return true;
	}

	bool SceneService::SaveScene(const std::filesystem::path& pathScene)
	{
		if (pathScene.empty())
		{
			return false;
		}

		std::string strError{};
		if (!_activeScene.save_to_file(pathScene, &strError))
		{
			return false;
		}

		_pathActiveScene = pathScene;
		PublishDirtyStateChanged(_activeScene.is_dirty());
		return true;
	}

	const std::filesystem::path& SceneService::GetActiveScenePath() const
	{
		return _pathActiveScene;
	}

	void SceneService::CreateDefaultEntities()
	{
		AshEngine::Entity root = _activeScene.create_entity("SceneRoot");

		AshEngine::Entity camera = _activeScene.create_entity("MainCamera", root);
		AshEngine::TransformComponent camera_transform = camera.get_transform_component();
		camera_transform.position = { 0.0f, 2.0f, -8.0f };
		camera.set_transform_component(camera_transform);
		camera.add_camera_component({});

		AshEngine::Entity light = _activeScene.create_entity("DirectionalLight", root);
		AshEngine::TransformComponent light_transform = light.get_transform_component();
		light_transform.rotation_euler_degrees = { -45.0f, 0.0f, 0.0f };
		light.set_transform_component(light_transform);
		light.add_light_component({});
	}

	void SceneService::SubscribeActiveSceneChanges()
	{
		if (!_pEventBus || !_activeScene.is_valid() || _uSceneChangeSubscriptionId != 0)
		{
			return;
		}

		_uSceneChangeSubscriptionId = _activeScene.subscribe_change_events(
			[this](const AshEngine::SceneChangeEvent& refEvent)
			{
				PublishSceneChanged(refEvent);
			});
	}

	void SceneService::UnsubscribeActiveSceneChanges()
	{
		if (_uSceneChangeSubscriptionId == 0)
		{
			return;
		}

		_activeScene.unsubscribe_change_events(_uSceneChangeSubscriptionId);
		_uSceneChangeSubscriptionId = 0;
	}

	void SceneService::PublishSceneChanged(const AshEngine::SceneChangeEvent& refEvent) const
	{
		if (!_pEventBus)
		{
			return;
		}

		EditorSceneChangedEvent event{};
		event.eKind = refEvent.kind;
		event.eComponentType = refEvent.component_type;
		event.uEntityId = refEvent.entity_id;
		event.uChangeVersion = refEvent.change_version;
		event.bDirty = refEvent.dirty;
		event.strSceneName = _activeScene.is_valid() ? _activeScene.get_name() : std::string{};
		event.strScenePath = _pathActiveScene.generic_string();
		_pEventBus->Publish(event);
	}

	void SceneService::PublishDirtyStateChanged(const bool bDirty) const
	{
		AshEngine::SceneChangeEvent event{};
		event.kind = AshEngine::SceneChangeKind::DirtyStateChanged;
		event.change_version = _activeScene.get_change_version();
		event.dirty = bDirty;
		PublishSceneChanged(event);
	}
}

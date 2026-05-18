#include "Services/SceneService.h"
#include "Base/hlog.h"
#include "Core/SceneComponentSerialization.h"

#include <algorithm>

namespace AshEditor
{
	namespace
	{
		bool ContainsSceneEntityId(const std::vector<SceneEntityId>& vecEntityIds, SceneEntityId uEntityId)
		{
			return std::find(vecEntityIds.begin(), vecEntityIds.end(), uEntityId) != vecEntityIds.end();
		}

		template<typename Component>
		std::optional<SceneComponentSnapshot> MakeComponentSnapshot(
			const AshEngine::SceneComponentType eType,
			const Component& refComponent)
		{
			const AshEngine::SceneComponentDesc* pComponentDesc = AshEngine::get_scene_component_descriptor(eType);
			if (!pComponentDesc)
			{
				return std::nullopt;
			}

			SceneComponentSnapshot snapshot{};
			snapshot.eType = eType;
			snapshot.strSerializedValue =
				SceneComponentSerialization::SerializeComponentPayload(&refComponent, *pComponentDesc);
			return snapshot;
		}

		std::optional<SceneComponentSnapshot> CaptureComponentSnapshot(
			const AshEngine::Entity& refEntity,
			const AshEngine::SceneComponentType eType)
		{
			switch (eType)
			{
			case AshEngine::SceneComponentType::Name:
				return MakeComponentSnapshot(eType, refEntity.get_name_component());
			case AshEngine::SceneComponentType::Transform:
				return MakeComponentSnapshot(eType, refEntity.get_transform_component());
			case AshEngine::SceneComponentType::Camera:
				return refEntity.has_camera_component()
					? MakeComponentSnapshot(eType, refEntity.get_camera_component())
					: std::nullopt;
			case AshEngine::SceneComponentType::Light:
				return refEntity.has_light_component()
					? MakeComponentSnapshot(eType, refEntity.get_light_component())
					: std::nullopt;
			case AshEngine::SceneComponentType::Mesh:
				return refEntity.has_mesh_component()
					? MakeComponentSnapshot(eType, refEntity.get_mesh_component())
					: std::nullopt;
			default:
				return std::nullopt;
			}
		}

		SceneEntitySnapshot CaptureSnapshotRecursive(
			const SceneService& refSceneService,
			const AshEngine::Entity& refEntity)
		{
			SceneEntitySnapshot snapshot{};
			snapshot.uEntityId = refEntity.get_id();
			snapshot.uSiblingIndex = refSceneService.GetEntitySiblingIndex(refEntity.get_id());

			for (const AshEngine::SceneComponentType eType : refEntity.get_component_types())
			{
				std::optional<SceneComponentSnapshot> optComponentSnapshot = CaptureComponentSnapshot(refEntity, eType);
				if (optComponentSnapshot.has_value())
				{
					snapshot.vecComponents.push_back(std::move(*optComponentSnapshot));
				}
			}

			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				snapshot.vecChildren.push_back(CaptureSnapshotRecursive(refSceneService, refChild));
			}
			return snapshot;
		}

		template<typename Component>
		bool ApplyRequiredComponent(AshEngine::Entity entity, const SceneComponentSnapshot& refSnapshot)
		{
			const AshEngine::SceneComponentDesc* pComponentDesc =
				AshEngine::get_scene_component_descriptor(refSnapshot.eType);
			if (!pComponentDesc)
			{
				return false;
			}

			Component component{};
			if (!SceneComponentSerialization::DeserializeComponentPayload(
				refSnapshot.strSerializedValue,
				*pComponentDesc,
				&component))
			{
				return false;
			}

			return entity.write_component(refSnapshot.eType, &component, sizeof(Component));
		}

		template<typename Component>
		bool ApplyOptionalComponent(
			AshEngine::Entity entity,
			const SceneComponentSnapshot& refSnapshot,
			bool (AshEngine::Entity::*pfnHas)() const,
			bool (AshEngine::Entity::*pfnSet)(const Component&),
			bool (AshEngine::Entity::*pfnAdd)(const Component&))
		{
			const AshEngine::SceneComponentDesc* pComponentDesc =
				AshEngine::get_scene_component_descriptor(refSnapshot.eType);
			if (!pComponentDesc)
			{
				return false;
			}

			Component component{};
			if (!SceneComponentSerialization::DeserializeComponentPayload(
				refSnapshot.strSerializedValue,
				*pComponentDesc,
				&component))
			{
				return false;
			}

			return (entity.*pfnHas)()
				? (entity.*pfnSet)(component)
				: (entity.*pfnAdd)(component);
		}

		bool RemoveOptionalComponent(AshEngine::Entity entity, const AshEngine::SceneComponentType eType)
		{
			switch (eType)
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

		bool SnapshotHasComponent(const SceneEntitySnapshot& refSnapshot, const AshEngine::SceneComponentType eType)
		{
			for (const SceneComponentSnapshot& refComponentSnapshot : refSnapshot.vecComponents)
			{
				if (refComponentSnapshot.eType == eType)
				{
					return true;
				}
			}
			return false;
		}

		bool ApplyEntitySnapshot(AshEngine::Entity entity, const SceneEntitySnapshot& refSnapshot)
		{
			if (!entity.is_valid())
			{
				return false;
			}

			const AshEngine::SceneComponentType arrOptionalTypes[] =
			{
				AshEngine::SceneComponentType::Camera,
				AshEngine::SceneComponentType::Light,
				AshEngine::SceneComponentType::Mesh,
			};
			for (const AshEngine::SceneComponentType eType : arrOptionalTypes)
			{
				if (!SnapshotHasComponent(refSnapshot, eType) && !RemoveOptionalComponent(entity, eType))
				{
					return false;
				}
			}

			for (const SceneComponentSnapshot& refComponentSnapshot : refSnapshot.vecComponents)
			{
				switch (refComponentSnapshot.eType)
				{
				case AshEngine::SceneComponentType::Name:
					if (!ApplyRequiredComponent<AshEngine::NameComponent>(entity, refComponentSnapshot))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Transform:
					if (!ApplyRequiredComponent<AshEngine::TransformComponent>(entity, refComponentSnapshot))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Camera:
					if (!ApplyOptionalComponent<AshEngine::CameraComponent>(
						entity,
						refComponentSnapshot,
						&AshEngine::Entity::has_camera_component,
						&AshEngine::Entity::set_camera_component,
						&AshEngine::Entity::add_camera_component))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Light:
					if (!ApplyOptionalComponent<AshEngine::LightComponent>(
						entity,
						refComponentSnapshot,
						&AshEngine::Entity::has_light_component,
						&AshEngine::Entity::set_light_component,
						&AshEngine::Entity::add_light_component))
					{
						return false;
					}
					break;
				case AshEngine::SceneComponentType::Mesh:
					if (!ApplyOptionalComponent<AshEngine::MeshComponent>(
						entity,
						refComponentSnapshot,
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

		AshEngine::Entity RestoreSnapshotRecursive(
			SceneService& refSceneService,
			const SceneEntitySnapshot& refSnapshot,
			const SceneEntityId uParentId)
		{
			AshEngine::Entity entity =
				refSceneService.CreateEntityWithId(refSnapshot.uEntityId, "Entity", uParentId, refSnapshot.uSiblingIndex);
			if (!entity.is_valid() || !ApplyEntitySnapshot(entity, refSnapshot))
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
		_activeScene = AshEngine::Scene::create(strName);
		_pathActiveScene.clear();
		CreateDefaultEntities();
		_activeScene.mark_clean();
	}

	bool SceneService::LoadScene(const std::filesystem::path& pathScene)
	{
		std::string strError{};
		AshEngine::Scene loadedScene = AshEngine::Scene::load_from_file(pathScene, &strError);
		if (!loadedScene.is_valid())
		{
			return false;
		}

		_activeScene = std::move(loadedScene);
		_pathActiveScene = pathScene;
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
}

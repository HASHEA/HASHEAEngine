#include "Core/SceneSnapshotUtils.h"
#include "Core/SceneComponentSerialization.h"

namespace AshEditor::SceneSnapshotUtils
{
	namespace
	{
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
			const AshEngine::Scene& refScene,
			const AshEngine::Entity& refEntity)
		{
			SceneEntitySnapshot snapshot{};
			snapshot.uEntityId = refEntity.get_id();
			snapshot.uSiblingIndex = refScene.get_entity_sibling_index(refEntity.get_id());

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
				snapshot.vecChildren.push_back(CaptureSnapshotRecursive(refScene, refChild));
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

		AshEngine::Entity CreateSceneEntityWithId(
			AshEngine::Scene& refScene,
			const SceneEntityId uEntityId,
			const std::string_view svName,
			const SceneEntityId uParentId,
			const uint32_t uSiblingIndex)
		{
			if (uEntityId == 0)
			{
				return {};
			}

			const AshEngine::Entity parent = uParentId != 0 ? refScene.find_entity(uParentId) : AshEngine::Entity{};
			return parent.is_valid()
				? refScene.create_entity_with_id(uEntityId, svName, parent, uSiblingIndex)
				: refScene.create_entity_with_id(uEntityId, svName, {}, uSiblingIndex);
		}

		AshEngine::Entity RestoreSnapshotRecursive(
			AshEngine::Scene& refScene,
			const SceneEntitySnapshot& refSnapshot,
			const SceneEntityId uParentId)
		{
			AshEngine::Entity entity =
				CreateSceneEntityWithId(refScene, refSnapshot.uEntityId, "Entity", uParentId, refSnapshot.uSiblingIndex);
			if (!entity.is_valid() || !ApplyEntitySnapshot(entity, refSnapshot))
			{
				if (entity.is_valid())
				{
					refScene.destroy_entity(entity.get_id());
				}
				return {};
			}

			for (const SceneEntitySnapshot& refChildSnapshot : refSnapshot.vecChildren)
			{
				if (!RestoreSnapshotRecursive(refScene, refChildSnapshot, entity.get_id()).is_valid())
				{
					refScene.destroy_entity(entity.get_id());
					return {};
				}
			}
			return entity;
		}

		AshEngine::Entity CreateSceneEntity(
			AshEngine::Scene& refScene,
			const std::string_view svName,
			const SceneEntityId uParentId,
			const uint32_t uSiblingIndex)
		{
			const AshEngine::Entity parent = uParentId != 0 ? refScene.find_entity(uParentId) : AshEngine::Entity{};
			return parent.is_valid()
				? refScene.create_entity(svName, parent, uSiblingIndex)
				: refScene.create_entity(svName, {}, uSiblingIndex);
		}

		AshEngine::Entity RestoreSnapshotAsCopyRecursive(
			AshEngine::Scene& refScene,
			const SceneEntitySnapshot& refSnapshot,
			const SceneEntityId uParentId,
			const uint32_t uSiblingIndex,
			std::vector<SceneEntityId>* pCreatedEntityIds,
			const char* pRootNameSuffix)
		{
			AshEngine::Entity entity = CreateSceneEntity(refScene, "Entity", uParentId, uSiblingIndex);
			if (!entity.is_valid() || !ApplyEntitySnapshot(entity, refSnapshot))
			{
				if (entity.is_valid())
				{
					refScene.destroy_entity(entity.get_id());
				}
				return {};
			}

			if (pRootNameSuffix && pRootNameSuffix[0] != '\0')
			{
				entity.set_name(entity.get_name() + pRootNameSuffix);
			}
			if (pCreatedEntityIds)
			{
				pCreatedEntityIds->push_back(entity.get_id());
			}

			for (const SceneEntitySnapshot& refChildSnapshot : refSnapshot.vecChildren)
			{
				if (!RestoreSnapshotAsCopyRecursive(
					refScene,
					refChildSnapshot,
					entity.get_id(),
					refChildSnapshot.uSiblingIndex,
					pCreatedEntityIds,
					nullptr).is_valid())
				{
					refScene.destroy_entity(entity.get_id());
					return {};
				}
			}
			return entity;
		}
	}

	std::optional<SceneEntitySnapshot> CaptureEntitySnapshot(
		const AshEngine::Scene& refScene,
		const SceneEntityId uEntityId)
	{
		const AshEngine::Entity entity = refScene.find_entity(uEntityId);
		if (!entity.is_valid())
		{
			return std::nullopt;
		}

		return CaptureSnapshotRecursive(refScene, entity);
	}

	AshEngine::Entity RestoreEntitySnapshot(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		const SceneEntityId uParentId)
	{
		return RestoreSnapshotRecursive(refScene, refSnapshot, uParentId);
	}

	AshEngine::Entity RestoreEntitySnapshotAsCopy(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		const SceneEntityId uParentId,
		const uint32_t uSiblingIndex,
		std::vector<SceneEntityId>* pCreatedEntityIds,
		const char* pRootNameSuffix)
	{
		return RestoreSnapshotAsCopyRecursive(
			refScene,
			refSnapshot,
			uParentId,
			uSiblingIndex,
			pCreatedEntityIds,
			pRootNameSuffix);
	}

	AshEngine::Scene CloneScene(const AshEngine::Scene& refSourceScene)
	{
		if (!refSourceScene.is_valid())
		{
			return {};
		}

		AshEngine::Scene clonedScene = AshEngine::Scene::create(refSourceScene.get_name());
		if (!clonedScene.is_valid())
		{
			return {};
		}

		for (const AshEngine::Entity& refRootEntity : refSourceScene.get_root_entities())
		{
			const std::optional<SceneEntitySnapshot> optSnapshot =
				CaptureEntitySnapshot(refSourceScene, refRootEntity.get_id());
			if (!optSnapshot.has_value())
			{
				return {};
			}

			if (!RestoreEntitySnapshot(clonedScene, *optSnapshot, 0).is_valid())
			{
				return {};
			}
		}

		clonedScene.mark_clean();
		return clonedScene;
	}
}

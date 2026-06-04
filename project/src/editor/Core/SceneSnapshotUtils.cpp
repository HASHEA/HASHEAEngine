#include "Core/SceneSnapshotUtils.h"
#include "Core/SceneSnapshotComponentUtils.h"

namespace AshEditor::SceneSnapshotUtils
{
	namespace
	{
		SceneEntitySnapshot CaptureSnapshotRecursive(
			const AshEngine::Scene& refScene,
			const AshEngine::Entity& refEntity)
		{
			SceneEntitySnapshot snapshot{};
			snapshot.uEntityId = refEntity.get_id();
			snapshot.uSiblingIndex = refScene.get_entity_sibling_index(refEntity.get_id());

			snapshot.vecComponents = SceneSnapshotComponentUtils::CaptureComponentSnapshots(refEntity);

			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				snapshot.vecChildren.push_back(CaptureSnapshotRecursive(refScene, refChild));
			}
			return snapshot;
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
			if (!entity.is_valid() || !SceneSnapshotComponentUtils::ApplyEntitySnapshot(entity, refSnapshot))
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
			if (!entity.is_valid() || !SceneSnapshotComponentUtils::ApplyEntitySnapshot(entity, refSnapshot))
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

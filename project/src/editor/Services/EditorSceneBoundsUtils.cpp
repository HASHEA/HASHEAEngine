#include "Services/EditorSceneBoundsUtils.h"

#include "Services/AssetDatabaseService.h"
#include "Services/SceneService.h"

#include <glm/common.hpp>

namespace AshEditor::EditorSceneBoundsUtils
{
	void ExpandWorldBounds(
		AshEngine::SceneWorldBounds& refBounds,
		const AshEngine::SceneWorldBounds& refAddition)
	{
		if (!refAddition.is_valid)
		{
			return;
		}
		if (!refBounds.is_valid)
		{
			refBounds = refAddition;
			return;
		}

		refBounds.min = glm::min(refBounds.min, refAddition.min);
		refBounds.max = glm::max(refBounds.max, refAddition.max);
		refBounds.center = (refBounds.min + refBounds.max) * 0.5f;
		refBounds.extents = (refBounds.max - refBounds.min) * 0.5f;
		refBounds.is_valid = true;
	}

	bool TryBuildMergedSubtreeWorldBounds(
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const std::vector<SceneEntityId>& vecRootEntityIds,
		AshEngine::SceneWorldBounds& outBounds)
	{
		outBounds = {};
		AshEngine::AssetDatabase& refAssetDatabase =
			const_cast<AshEngine::AssetDatabase&>(refAssetDatabaseService.GetDatabase());

		for (const SceneEntityId uEntityId : vecRootEntityIds)
		{
			AshEngine::SceneWorldBounds entityBounds{};
			if (AshEngine::get_entity_subtree_world_bounds(
				refSceneService.GetActiveScene(),
				refAssetDatabase,
				uEntityId,
				entityBounds))
			{
				ExpandWorldBounds(outBounds, entityBounds);
			}
		}

		return outBounds.is_valid;
	}
}

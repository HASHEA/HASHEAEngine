#pragma once

#include "Core/EditorSceneTypes.h"
#include "Function/Scene/SceneQuery.h"

#include <vector>

namespace AshEditor
{
	class AssetDatabaseService;
	class SceneService;
}

namespace AshEditor::EditorSceneBoundsUtils
{
	void ExpandWorldBounds(
		AshEngine::SceneWorldBounds& refBounds,
		const AshEngine::SceneWorldBounds& refAddition);
	bool TryBuildMergedSubtreeWorldBounds(
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const std::vector<SceneEntityId>& vecRootEntityIds,
		AshEngine::SceneWorldBounds& outBounds);
}

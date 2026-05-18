#pragma once

#include "Core/SceneSnapshotTypes.h"
#include "Function/Scene/Scene.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace AshEditor::SceneSnapshotUtils
{
	std::optional<SceneEntitySnapshot> CaptureEntitySnapshot(
		const AshEngine::Scene& refScene,
		SceneEntityId uEntityId);

	AshEngine::Entity RestoreEntitySnapshot(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		SceneEntityId uParentId = 0);

	AshEngine::Entity RestoreEntitySnapshotAsCopy(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		SceneEntityId uParentId,
		uint32_t uSiblingIndex,
		std::vector<SceneEntityId>* pCreatedEntityIds = nullptr,
		const char* pRootNameSuffix = nullptr);

	AshEngine::Scene CloneScene(const AshEngine::Scene& refSourceScene);
}

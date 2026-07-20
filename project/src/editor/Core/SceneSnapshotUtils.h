#pragma once

#include "Core/SceneSnapshotTypes.h"
#include "Function/Scene/Scene.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace AshEditor::SceneSnapshotUtils
{
	struct SceneSnapshotRestoreRequest
	{
		const SceneEntitySnapshot* pSnapshot = nullptr;
		SceneEntityId uParentId = 0;
		uint32_t uSiblingIndex = AshEngine::k_scene_append_sibling_index;
		const char* pRootNameSuffix = nullptr;
	};

	std::optional<SceneEntitySnapshot> CaptureEntitySnapshot(
		const AshEngine::Scene& refScene,
		SceneEntityId uEntityId);

	bool RestoreEntitySnapshots(
		AshEngine::Scene& refScene,
		const std::vector<SceneSnapshotRestoreRequest>& refRequests,
		std::vector<SceneEntityId>& outRootEntityIds);

	bool RestoreEntitySnapshotsAsCopies(
		AshEngine::Scene& refScene,
		const std::vector<SceneSnapshotRestoreRequest>& refRequests,
		std::vector<SceneEntityId>& outRootEntityIds,
		std::vector<SceneEntityId>* pCreatedEntityIds = nullptr);

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

#pragma once

#include "Core/SceneSnapshotTypes.h"
#include "Function/Scene/Scene.h"

#include <optional>

namespace AshEditor::SceneSnapshotUtils
{
	std::optional<SceneEntitySnapshot> CaptureEntitySnapshot(
		const AshEngine::Scene& refScene,
		SceneEntityId uEntityId);

	AshEngine::Entity RestoreEntitySnapshot(
		AshEngine::Scene& refScene,
		const SceneEntitySnapshot& refSnapshot,
		SceneEntityId uParentId = 0);

	AshEngine::Scene CloneScene(const AshEngine::Scene& refSourceScene);
}

#pragma once

#include "Core/EditorSceneTypes.h"
#include "Function/Scene/SceneComponents.h"

#include <vector>

namespace AshEditor
{
	class SceneService;
	class SelectionService;
}

namespace AshEditor::EditorGizmoSelectionUtils
{
	std::vector<SceneEntityId> BuildSelectedTopLevelEntityIds(
		const SceneService& refSceneService,
		const SelectionService& refSelectionService);
	bool CaptureEntityTransforms(
		const SceneService& refSceneService,
		const std::vector<SceneEntityId>& vecEntityIds,
		std::vector<AshEngine::TransformComponent>& outTransforms);
	void CaptureDragEntityTransforms(
		const SceneService& refSceneService,
		const SelectionService& refSelectionService,
		SceneEntityId uPrimaryEntityId,
		const AshEngine::TransformComponent& refPrimaryTransform,
		std::vector<SceneEntityId>& outEntityIds,
		std::vector<AshEngine::TransformComponent>& outTransforms);
}

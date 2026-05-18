#pragma once

#include "Core/EditorGizmoTypes.h"
#include "Core/EditorSceneTypes.h"
#include "Function/Scene/SceneComponents.h"

#include <glm/fwd.hpp>

namespace AshEditor
{
	class SceneService;
}

namespace AshEditor::EditorGizmoTransform
{
	AshEngine::TransformComponent ComputeMovedTransform(
		const SceneService& refSceneService,
		SceneEntityId uEntityId,
		const AshEngine::TransformComponent& refBeforeTransform,
		const glm::vec3& vecWorldDelta);
	AshEngine::TransformComponent ComputeScaledTransform(
		const AshEngine::TransformComponent& refBeforeTransform,
		const glm::vec3& vecScaleDeltaNormalized,
		const EditorGizmoState& refGizmoState);
	AshEngine::TransformComponent ComputeRotatedTransform(
		const SceneService& refSceneService,
		SceneEntityId uEntityId,
		const AshEngine::TransformComponent& refBeforeTransform,
		const glm::vec3& vecWorldAxis,
		float fDeltaDegrees);
}

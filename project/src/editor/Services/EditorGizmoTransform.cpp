#include "Services/EditorGizmoTransform.h"

#include "Function/Scene/Scene.h"
#include "Services/EditorGizmoMath.h"
#include "Services/SceneService.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace AshEditor::EditorGizmoTransform
{
	AshEngine::TransformComponent ComputeMovedTransform(
		const SceneService& refSceneService,
		const SceneEntityId uEntityId,
		const AshEngine::TransformComponent& refBeforeTransform,
		const glm::vec3& vecWorldDelta)
	{
		AshEngine::TransformComponent result = refBeforeTransform;
		const AshEngine::Entity entity = refSceneService.FindEntity(uEntityId);
		if (!entity.is_valid())
		{
			return result;
		}

		const AshEngine::Entity parent = entity.get_parent();
		if (!parent.is_valid())
		{
			result.position = refBeforeTransform.position + vecWorldDelta;
			return result;
		}

		const glm::mat4 matParentWorld =
			refSceneService.GetActiveScene().get_entity_world_transform(parent.get_id());
		const glm::mat4 matParentInverse = glm::inverse(matParentWorld);
		const glm::vec3 vecTargetWorldPosition =
			EditorGizmoMath::TransformPoint(matParentWorld, refBeforeTransform.position) +
			vecWorldDelta;
		result.position = EditorGizmoMath::TransformPoint(matParentInverse, vecTargetWorldPosition);
		return result;
	}

	AshEngine::TransformComponent ComputeScaledTransform(
		const AshEngine::TransformComponent& refBeforeTransform,
		const glm::vec3& vecScaleDeltaNormalized,
		const EditorGizmoState& refGizmoState)
	{
		AshEngine::TransformComponent result = refBeforeTransform;
		for (glm::length_t iAxisIndex = 0; iAxisIndex < 3; ++iAxisIndex)
		{
			const float fBeforeScale = refBeforeTransform.scale[iAxisIndex];
			const float fSafeBeforeScale = std::max(fBeforeScale, 0.01f);
			float fNewScale = fSafeBeforeScale * (1.0f + vecScaleDeltaNormalized[iAxisIndex]);
			if (refGizmoState.snap.bSnapEnabled && refGizmoState.snap.fScaleSnapStep > 0.0001f)
			{
				fNewScale = std::round(fNewScale / refGizmoState.snap.fScaleSnapStep) * refGizmoState.snap.fScaleSnapStep;
			}
			result.scale[iAxisIndex] = std::max(fNewScale, 0.01f);
		}
		return result;
	}

	AshEngine::TransformComponent ComputeRotatedTransform(
		const SceneService& refSceneService,
		const SceneEntityId uEntityId,
		const AshEngine::TransformComponent& refBeforeTransform,
		const glm::vec3& vecWorldAxis,
		const float fDeltaDegrees)
	{
		AshEngine::TransformComponent result = refBeforeTransform;
		if (std::abs(fDeltaDegrees) <= 0.0001f)
		{
			return result;
		}

		const AshEngine::Entity entity = refSceneService.FindEntity(uEntityId);
		if (!entity.is_valid())
		{
			return result;
		}

		glm::quat quatParentWorld = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		const AshEngine::Entity parent = entity.get_parent();
		if (parent.is_valid())
		{
			quatParentWorld = EditorGizmoMath::ExtractRotationQuaternion(
				refSceneService.GetActiveScene().get_entity_world_transform(parent.get_id()));
		}

		const glm::quat quatBeforeLocal = glm::quat(glm::radians(refBeforeTransform.rotation_euler_degrees));
		const glm::quat quatBeforeWorld = glm::normalize(quatParentWorld * quatBeforeLocal);
		const glm::quat quatDeltaWorld = glm::angleAxis(
			glm::radians(fDeltaDegrees),
			EditorGizmoMath::NormalizeOrFallback(vecWorldAxis, glm::vec3(0.0f, 1.0f, 0.0f)));
		const glm::quat quatAfterWorld = glm::normalize(quatDeltaWorld * quatBeforeWorld);
		const glm::quat quatAfterLocal =
			parent.is_valid()
			? glm::normalize(glm::inverse(quatParentWorld) * quatAfterWorld)
			: quatAfterWorld;

		result.rotation_euler_degrees = glm::degrees(glm::eulerAngles(quatAfterLocal));
		result.rotation_euler_degrees.x = EditorGizmoMath::WrapDegrees(result.rotation_euler_degrees.x);
		result.rotation_euler_degrees.y = EditorGizmoMath::WrapDegrees(result.rotation_euler_degrees.y);
		result.rotation_euler_degrees.z = EditorGizmoMath::WrapDegrees(result.rotation_euler_degrees.z);
		return result;
	}
}

#pragma once

#include "Services/EditorGizmoTypesInternal.h"

#include "Function/Scene/SceneQuery.h"

#include <glm/fwd.hpp>

namespace AshEditor::EditorGizmoViewport
{
	AshEngine::SceneRay BuildViewportRay(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec2& vecMousePosition);
	glm::vec3 ComputeCameraForward(const EditorGizmoInternal::ViewportContext& refViewportContext);
	bool TryProjectWorldToViewport(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec3& vecWorldPosition,
		glm::vec2& outViewportPosition,
		float& outDepth);
	float ComputeAxisWorldLength(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec3& vecOrigin);
}

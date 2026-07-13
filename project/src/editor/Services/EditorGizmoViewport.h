#pragma once

#include "Services/EditorGizmoTypesInternal.h"

#include "Function/Scene/SceneQuery.h"

#include <glm/fwd.hpp>

#include <array>

namespace AshEditor::EditorGizmoViewport
{
	struct PlaneHandleProjectionDesc
	{
		glm::vec3 vecOrigin{ 0.0f };
		glm::vec3 vecAxisU{ 1.0f, 0.0f, 0.0f };
		glm::vec3 vecAxisV{ 0.0f, 1.0f, 0.0f };
		float fWorldLength = 1.0f;
		float fInnerScale = 0.26f;
		float fOuterScale = 0.42f;
	};

	AshEngine::SceneRay BuildViewportRay(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec2& vecMousePosition);
	glm::vec3 ComputeCameraForward(const EditorGizmoInternal::ViewportContext& refViewportContext);
	bool TryProjectWorldToViewport(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec3& vecWorldPosition,
		glm::vec2& outViewportPosition,
		float& outDepth);
	bool TryBuildProjectedPlaneHandle(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const PlaneHandleProjectionDesc& refDesc,
		std::array<glm::vec2, 4>& outScreenCorners);
	float ComputeAxisWorldLength(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec3& vecOrigin);
}

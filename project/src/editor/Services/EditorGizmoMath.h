#pragma once

#include <glm/fwd.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>

namespace AshEditor::EditorGizmoMath
{
	glm::vec3 NormalizeOrFallback(const glm::vec3& refValue, const glm::vec3& refFallback);
	void ExtractViewBasis(
		const glm::mat4& refView,
		glm::vec3& outRight,
		glm::vec3& outUp,
		glm::vec3& outForward);
	glm::vec3 TransformPoint(const glm::mat4& refMatrix, const glm::vec3& refPoint);
	float DistancePointToSegment(
		const glm::vec2& vecPoint,
		const glm::vec2& vecSegmentStart,
		const glm::vec2& vecSegmentEnd);
	bool IsPointInsideConvexQuad(
		const glm::vec2& vecPoint,
		const std::array<glm::vec2, 4>& arrCorners);
	bool TryBuildPerpendicularBasis(
		const glm::vec3& vecAxisDirection,
		const glm::vec3& vecPreferredTangentHint,
		glm::vec3& outTangent,
		glm::vec3& outBitangent);
	float ComputeSignedAngleAroundAxis(
		const glm::vec3& vecFromDirection,
		const glm::vec3& vecToDirection,
		const glm::vec3& vecAxisDirection);
	bool TryIntersectRayPlane(
		const glm::vec3& vecRayOrigin,
		const glm::vec3& vecRayDirection,
		const glm::vec3& vecPlanePoint,
		const glm::vec3& vecPlaneNormal,
		glm::vec3& outHitPoint);
	glm::quat ExtractRotationQuaternion(const glm::mat4& matTransform);
	float WrapDegrees(float fDegrees);
}

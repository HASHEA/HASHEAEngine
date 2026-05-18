#include "Services/EditorGizmoMath.h"

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>

namespace AshEditor::EditorGizmoMath
{
	glm::vec3 NormalizeOrFallback(const glm::vec3& refValue, const glm::vec3& refFallback)
	{
		const float fLength = glm::length(refValue);
		return fLength > 0.0001f ? (refValue / fLength) : refFallback;
	}

	glm::vec3 TransformPoint(const glm::mat4& refMatrix, const glm::vec3& refPoint)
	{
		return glm::vec3(refMatrix * glm::vec4(refPoint, 1.0f));
	}

	float DistancePointToSegment(
		const glm::vec2& vecPoint,
		const glm::vec2& vecSegmentStart,
		const glm::vec2& vecSegmentEnd)
	{
		const glm::vec2 vecSegment = vecSegmentEnd - vecSegmentStart;
		const float fLengthSquared = glm::dot(vecSegment, vecSegment);
		if (fLengthSquared <= 0.0001f)
		{
			return glm::length(vecPoint - vecSegmentStart);
		}

		const float fT = std::clamp(
			glm::dot(vecPoint - vecSegmentStart, vecSegment) / fLengthSquared,
			0.0f,
			1.0f);
		const glm::vec2 vecClosestPoint = vecSegmentStart + vecSegment * fT;
		return glm::length(vecPoint - vecClosestPoint);
	}

	bool TryBuildPerpendicularBasis(
		const glm::vec3& vecAxisDirection,
		const glm::vec3& vecPreferredTangentHint,
		glm::vec3& outTangent,
		glm::vec3& outBitangent)
	{
		const glm::vec3 vecAxis = NormalizeOrFallback(vecAxisDirection, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::vec3 vecTangent = glm::cross(vecPreferredTangentHint, vecAxis);
		if (glm::length2(vecTangent) <= 0.0001f)
		{
			const glm::vec3 vecFallbackHint =
				std::abs(vecAxis.y) < 0.95f
				? glm::vec3(0.0f, 1.0f, 0.0f)
				: glm::vec3(1.0f, 0.0f, 0.0f);
			vecTangent = glm::cross(vecFallbackHint, vecAxis);
			if (glm::length2(vecTangent) <= 0.0001f)
			{
				return false;
			}
		}

		outTangent = NormalizeOrFallback(vecTangent, glm::vec3(1.0f, 0.0f, 0.0f));
		outBitangent = NormalizeOrFallback(glm::cross(vecAxis, outTangent), glm::vec3(0.0f, 0.0f, 1.0f));
		return glm::length2(outBitangent) > 0.0001f;
	}

	float ComputeSignedAngleAroundAxis(
		const glm::vec3& vecFromDirection,
		const glm::vec3& vecToDirection,
		const glm::vec3& vecAxisDirection)
	{
		const glm::vec3 vecAxis = NormalizeOrFallback(vecAxisDirection, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::vec3 vecFrom = vecFromDirection - vecAxis * glm::dot(vecFromDirection, vecAxis);
		glm::vec3 vecTo = vecToDirection - vecAxis * glm::dot(vecToDirection, vecAxis);
		if (glm::length2(vecFrom) <= 0.0001f || glm::length2(vecTo) <= 0.0001f)
		{
			return 0.0f;
		}

		vecFrom = glm::normalize(vecFrom);
		vecTo = glm::normalize(vecTo);
		const float fCosAngle = std::clamp(glm::dot(vecFrom, vecTo), -1.0f, 1.0f);
		const float fSinAngle = glm::dot(vecAxis, glm::cross(vecFrom, vecTo));
		return std::atan2(fSinAngle, fCosAngle);
	}

	bool TryIntersectRayPlane(
		const glm::vec3& vecRayOrigin,
		const glm::vec3& vecRayDirection,
		const glm::vec3& vecPlanePoint,
		const glm::vec3& vecPlaneNormal,
		glm::vec3& outHitPoint)
	{
		const float fDenominator = glm::dot(vecPlaneNormal, vecRayDirection);
		if (std::abs(fDenominator) <= 0.0001f)
		{
			return false;
		}

		const float fDistance = glm::dot(vecPlanePoint - vecRayOrigin, vecPlaneNormal) / fDenominator;
		if (fDistance < 0.0f)
		{
			return false;
		}

		outHitPoint = vecRayOrigin + vecRayDirection * fDistance;
		return true;
	}

	glm::quat ExtractRotationQuaternion(const glm::mat4& matTransform)
	{
		glm::vec3 vecScale{};
		glm::quat quatRotation{};
		glm::vec3 vecTranslation{};
		glm::vec3 vecSkew{};
		glm::vec4 vecPerspective{};
		if (glm::decompose(matTransform, vecScale, quatRotation, vecTranslation, vecSkew, vecPerspective))
		{
			return glm::normalize(quatRotation);
		}

		return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	}

	float WrapDegrees(const float fDegrees)
	{
		float fWrapped = std::fmod(fDegrees, 360.0f);
		if (fWrapped > 180.0f)
		{
			fWrapped -= 360.0f;
		}
		else if (fWrapped < -180.0f)
		{
			fWrapped += 360.0f;
		}
		return fWrapped;
	}
}

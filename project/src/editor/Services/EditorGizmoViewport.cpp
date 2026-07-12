#include "Services/EditorGizmoViewport.h"

#include "Services/EditorGizmoMath.h"

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace AshEditor::EditorGizmoViewport
{
	namespace
	{
		constexpr float kGizmoTargetScreenLength = 104.0f;
		constexpr float kGizmoMinWorldLength = 0.25f;
		constexpr float kGizmoMaxWorldLength = 1000.0f;
		constexpr float kMinProjectedPlaneHandleAreaPixelsSquared = 1.0f;
	}

	AshEngine::SceneRay BuildViewportRay(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec2& vecMousePosition)
	{
		return AshEngine::screen_to_world_ray(
			vecMousePosition.x - refViewportContext.rectContent.x,
			vecMousePosition.y - refViewportContext.rectContent.y,
			refViewportContext.rectContent.width,
			refViewportContext.rectContent.height,
			refViewportContext.matView,
			refViewportContext.matProjection);
	}

	glm::vec3 ComputeCameraForward(const EditorGizmoInternal::ViewportContext& refViewportContext)
	{
		glm::vec3 vecRight{};
		glm::vec3 vecUp{};
		glm::vec3 vecForward{};
		EditorGizmoMath::ExtractViewBasis(refViewportContext.matView, vecRight, vecUp, vecForward);
		return vecForward;
	}

	bool TryProjectWorldToViewport(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec3& vecWorldPosition,
		glm::vec2& outViewportPosition,
		float& outDepth)
	{
		const glm::vec4 vecClip =
			refViewportContext.matProjection *
			refViewportContext.matView *
			glm::vec4(vecWorldPosition, 1.0f);
		if (std::abs(vecClip.w) <= 0.0001f)
		{
			return false;
		}

		const glm::vec3 vecNdc = glm::vec3(vecClip) / vecClip.w;
		if (vecNdc.z < 0.0f || vecNdc.z > 1.0f)
		{
			return false;
		}

		outViewportPosition.x = refViewportContext.rectContent.x + ((vecNdc.x + 1.0f) * 0.5f) * refViewportContext.rectContent.width;
		outViewportPosition.y = refViewportContext.rectContent.y + ((1.0f - vecNdc.y) * 0.5f) * refViewportContext.rectContent.height;
		outDepth = vecNdc.z;
		return true;
	}

	bool TryBuildProjectedPlaneHandle(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const PlaneHandleProjectionDesc& refDesc,
		std::array<glm::vec2, 4>& outScreenCorners)
	{
		outScreenCorners = {};
		if (refDesc.fWorldLength <= 0.0001f ||
			refDesc.fInnerScale < 0.0f ||
			refDesc.fOuterScale <= refDesc.fInnerScale ||
			glm::length(refDesc.vecAxisU) <= 0.0001f ||
			glm::length(refDesc.vecAxisV) <= 0.0001f)
		{
			return false;
		}

		const glm::vec3 vecAxisU = EditorGizmoMath::NormalizeOrFallback(
			refDesc.vecAxisU,
			glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::vec3 vecAxisV = EditorGizmoMath::NormalizeOrFallback(
			refDesc.vecAxisV,
			glm::vec3(0.0f, 1.0f, 0.0f));
		const std::array<glm::vec2, 4> arrScales{
			glm::vec2(refDesc.fInnerScale, refDesc.fInnerScale),
			glm::vec2(refDesc.fOuterScale, refDesc.fInnerScale),
			glm::vec2(refDesc.fOuterScale, refDesc.fOuterScale),
			glm::vec2(refDesc.fInnerScale, refDesc.fOuterScale)
		};
		for (size_t uCornerIndex = 0; uCornerIndex < arrScales.size(); ++uCornerIndex)
		{
			const glm::vec2& refScale = arrScales[uCornerIndex];
			const glm::vec3 vecWorldCorner =
				refDesc.vecOrigin +
				vecAxisU * (refDesc.fWorldLength * refScale.x) +
				vecAxisV * (refDesc.fWorldLength * refScale.y);
			float fDepth = 0.0f;
			if (!TryProjectWorldToViewport(
				refViewportContext,
				vecWorldCorner,
				outScreenCorners[uCornerIndex],
				fDepth))
			{
				outScreenCorners = {};
				return false;
			}
		}

		const glm::vec2 vecEdge01 = outScreenCorners[1] - outScreenCorners[0];
		const glm::vec2 vecEdge02 = outScreenCorners[2] - outScreenCorners[0];
		const glm::vec2 vecEdge03 = outScreenCorners[3] - outScreenCorners[0];
		const float fTwiceProjectedArea =
			(vecEdge01.x * vecEdge02.y - vecEdge01.y * vecEdge02.x) +
			(vecEdge02.x * vecEdge03.y - vecEdge02.y * vecEdge03.x);
		if (std::abs(fTwiceProjectedArea) * 0.5f < kMinProjectedPlaneHandleAreaPixelsSquared)
		{
			outScreenCorners = {};
			return false;
		}

		return true;
	}

	float ComputeAxisWorldLength(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec3& vecOrigin)
	{
		const float fViewportHeight = std::max(refViewportContext.rectContent.height, 1.0f);
		const float fProjectionYScale = std::abs(refViewportContext.matProjection[1][1]);
		if (fProjectionYScale <= 0.0001f)
		{
			return 1.0f;
		}

		const glm::vec3 vecCameraForward = ComputeCameraForward(refViewportContext);
		const float fDepthAlongView =
			glm::dot(vecOrigin - refViewportContext.vecCameraPosition, vecCameraForward);
		const float fDistanceToPivot = glm::length(vecOrigin - refViewportContext.vecCameraPosition);
		const float fReferenceDepth = std::max(std::max(fDepthAlongView, 0.0f), fDistanceToPivot * 0.25f);
		const float fTanHalfFovY = 1.0f / fProjectionYScale;
		const float fWorldUnitsPerPixel = (2.0f * std::max(fReferenceDepth, 0.25f) * fTanHalfFovY) / fViewportHeight;
		return std::clamp(
			kGizmoTargetScreenLength * fWorldUnitsPerPixel,
			kGizmoMinWorldLength,
			kGizmoMaxWorldLength);
	}
}

#include "doctest.h"

#include "Services/EditorGizmoMath.h"
#include "Services/EditorGizmoViewport.h"

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <array>
#include <cmath>

namespace
{
	void CheckVec3Approx(const glm::vec3& refActual, const glm::vec3& refExpected)
	{
		CHECK(refActual.x == doctest::Approx(refExpected.x));
		CHECK(refActual.y == doctest::Approx(refExpected.y));
		CHECK(refActual.z == doctest::Approx(refExpected.z));
	}
}

TEST_CASE("Editor gizmo orientation basis follows the viewport view matrix")
{
	const glm::mat4 matCameraWorld =
		glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 3.0f, -6.0f)) *
		glm::yawPitchRoll(glm::radians(31.0f), glm::radians(-19.0f), glm::radians(7.0f));
	const glm::mat4 matView = glm::inverse(matCameraWorld);

	glm::vec3 vecRight{};
	glm::vec3 vecUp{};
	glm::vec3 vecForward{};
	AshEditor::EditorGizmoMath::ExtractViewBasis(matView, vecRight, vecUp, vecForward);

	CheckVec3Approx(vecRight, glm::normalize(glm::vec3(matCameraWorld[0])));
	CheckVec3Approx(vecUp, glm::normalize(glm::vec3(matCameraWorld[1])));
	CheckVec3Approx(vecForward, glm::normalize(glm::vec3(matCameraWorld[2])));
	CHECK(glm::dot(glm::cross(vecRight, vecUp), vecForward) == doctest::Approx(1.0f));
}

TEST_CASE("Editor gizmo plane handle preserves perspective for drawing and hit testing")
{
	AshEditor::EditorGizmoInternal::ViewportContext viewportContext{};
	viewportContext.rectContent = { 100.0f, 50.0f, 1280.0f, 720.0f };
	viewportContext.vecCameraPosition = { -4.0f, 3.0f, -6.0f };
	viewportContext.matView = glm::lookAtLH(
		viewportContext.vecCameraPosition,
		glm::vec3(0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	viewportContext.matProjection = glm::perspectiveLH_ZO(
		glm::radians(60.0f),
		viewportContext.rectContent.width / viewportContext.rectContent.height,
		0.03f,
		2000.0f);

	AshEditor::EditorGizmoViewport::PlaneHandleProjectionDesc projectionDesc{};
	projectionDesc.vecOrigin = { -1.0f, 0.25f, 0.5f };
	projectionDesc.vecAxisU = { 1.0f, 0.0f, 0.0f };
	projectionDesc.vecAxisV = { 0.0f, 1.0f, 0.0f };
	projectionDesc.fWorldLength = 2.0f;
	projectionDesc.fInnerScale = 0.26f;
	projectionDesc.fOuterScale = 0.42f;

	std::array<glm::vec2, 4> arrScreenCorners{};
	REQUIRE(AshEditor::EditorGizmoViewport::TryBuildProjectedPlaneHandle(
		viewportContext,
		projectionDesc,
		arrScreenCorners));

	const float fInnerEdgeLength = glm::length(arrScreenCorners[1] - arrScreenCorners[0]);
	const float fOuterEdgeLength = glm::length(arrScreenCorners[2] - arrScreenCorners[3]);
	CHECK(std::abs(fInnerEdgeLength - fOuterEdgeLength) > 0.01f);

	const glm::vec2 vecCenter =
		(arrScreenCorners[0] + arrScreenCorners[1] + arrScreenCorners[2] + arrScreenCorners[3]) * 0.25f;
	CHECK(AshEditor::EditorGizmoMath::IsPointInsideConvexQuad(vecCenter, arrScreenCorners));
	CHECK_FALSE(AshEditor::EditorGizmoMath::IsPointInsideConvexQuad(
		glm::vec2(viewportContext.rectContent.x - 20.0f, viewportContext.rectContent.y - 20.0f),
		arrScreenCorners));

	SUBCASE("convex hit testing is winding-independent and rejects AABB-only points")
	{
		const std::array<glm::vec2, 4> arrDiamond{
			glm::vec2(0.0f, -2.0f),
			glm::vec2(2.0f, 0.0f),
			glm::vec2(0.0f, 2.0f),
			glm::vec2(-2.0f, 0.0f)
		};
		const std::array<glm::vec2, 4> arrDiamondReversed{
			arrDiamond[3], arrDiamond[2], arrDiamond[1], arrDiamond[0]
		};
		CHECK(AshEditor::EditorGizmoMath::IsPointInsideConvexQuad(glm::vec2(0.0f), arrDiamond));
		CHECK(AshEditor::EditorGizmoMath::IsPointInsideConvexQuad(glm::vec2(0.0f), arrDiamondReversed));
		CHECK_FALSE(AshEditor::EditorGizmoMath::IsPointInsideConvexQuad(glm::vec2(1.8f, 1.8f), arrDiamond));
	}

	SUBCASE("invalid plane geometry is rejected")
	{
		projectionDesc.fOuterScale = projectionDesc.fInnerScale;
		CHECK_FALSE(AshEditor::EditorGizmoViewport::TryBuildProjectedPlaneHandle(
			viewportContext,
			projectionDesc,
			arrScreenCorners));
	}

	SUBCASE("edge-on plane projections are rejected")
	{
		viewportContext.vecCameraPosition = { 0.0f, 0.0f, -5.0f };
		viewportContext.matView = glm::lookAtLH(
			viewportContext.vecCameraPosition,
			glm::vec3(0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));
		projectionDesc.vecOrigin = { 0.0f, 0.0f, 0.0f };
		projectionDesc.vecAxisU = { 1.0f, 0.0f, 0.0f };
		projectionDesc.vecAxisV = { 0.0f, 0.0f, 1.0f };
		CHECK_FALSE(AshEditor::EditorGizmoViewport::TryBuildProjectedPlaneHandle(
			viewportContext,
			projectionDesc,
			arrScreenCorners));
	}
}

#include "doctest.h"

#include "Services/EditorViewportCameraMath.h"

#include <glm/geometric.hpp>

namespace
{
	void CheckVec3Approx(const glm::vec3& refActual, const glm::vec3& refExpected)
	{
		CHECK(refActual.x == doctest::Approx(refExpected.x));
		CHECK(refActual.y == doctest::Approx(refExpected.y));
		CHECK(refActual.z == doctest::Approx(refExpected.z));
	}
}

TEST_CASE("Editor viewport camera wheel movement stays constant and preserves the orbit offset")
{
	const glm::vec3 vecPosition{ 0.0f, 0.0f, 0.0f };
	const glm::vec3 vecNearTarget{ 0.0f, 0.0f, 10.0f };
	const glm::vec3 vecFarTarget{ 0.0f, 0.0f, 20.0f };
	const glm::vec3 vecForward{ 0.0f, 0.0f, 1.0f };

	const AshEditor::EditorViewportCameraMath::WheelTranslationResult nearResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecNearTarget,
			vecForward,
			1.0f,
			8.0f,
			false);
	const AshEditor::EditorViewportCameraMath::WheelTranslationResult farResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecFarTarget,
			vecForward,
			1.0f,
			8.0f,
			false);
	const AshEditor::EditorViewportCameraMath::WheelTranslationResult shiftedResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecNearTarget,
			vecForward,
			1.0f,
			8.0f,
			true);
	const AshEditor::EditorViewportCameraMath::WheelTranslationResult backwardResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecNearTarget,
			vecForward,
			-1.0f,
			8.0f,
			false);

	const glm::vec3 vecNearMovement = nearResult.vecPosition - vecPosition;
	const glm::vec3 vecFarMovement = farResult.vecPosition - vecPosition;
	CheckVec3Approx(vecNearMovement, vecFarMovement);
	CheckVec3Approx(shiftedResult.vecPosition - vecPosition, vecNearMovement * 2.0f);
	CheckVec3Approx(backwardResult.vecPosition - vecPosition, -vecNearMovement);
	CHECK(glm::length(nearResult.vecOrbitTarget - nearResult.vecPosition) ==
		doctest::Approx(glm::length(vecNearTarget - vecPosition)));
}

TEST_CASE("Editor viewport camera Shift doubles pan and dolly movement")
{
	const glm::vec3 vecPan = AshEditor::EditorViewportCameraMath::ComputePanTranslation(
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		4.0f,
		3.0f,
		0.5f,
		8.0f,
		false);
	const glm::vec3 vecShiftPan = AshEditor::EditorViewportCameraMath::ComputePanTranslation(
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		4.0f,
		3.0f,
		0.5f,
		8.0f,
		true);
	CheckVec3Approx(vecShiftPan, vecPan * 2.0f);

	const float fDolly = AshEditor::EditorViewportCameraMath::ComputeDollyDistanceDelta(
		10.0f,
		-4.0f,
		8.0f,
		false);
	const float fShiftDolly = AshEditor::EditorViewportCameraMath::ComputeDollyDistanceDelta(
		10.0f,
		-4.0f,
		8.0f,
		true);
	CHECK(fShiftDolly == doctest::Approx(fDolly * 2.0f));
}

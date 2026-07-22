#pragma once

#include <glm/vec3.hpp>

namespace AshEditor::EditorViewportCameraMath
{
	struct WheelTranslationResult
	{
		glm::vec3 vecPosition{};
		glm::vec3 vecOrbitTarget{};
	};

	WheelTranslationResult ComputeWheelTranslation(
		const glm::vec3& refPosition,
		const glm::vec3& refOrbitTarget,
		const glm::vec3& refForward,
		float fWheelDelta,
		float fMoveSpeed,
		bool bShiftDown);

	glm::vec3 ComputePanTranslation(
		const glm::vec3& refRight,
		const glm::vec3& refUp,
		float fMouseDeltaX,
		float fMouseDeltaY,
		float fPanUnitsPerPixel,
		float fMoveSpeed,
		bool bShiftDown);

	float ComputeDollyDistanceDelta(
		float fOrbitDistance,
		float fMouseDeltaY,
		float fMoveSpeed,
		bool bShiftDown);
}

#include "Services/EditorViewportCameraMath.h"

#include <algorithm>

namespace AshEditor::EditorViewportCameraMath
{
	namespace
	{
		constexpr float kReferenceMoveSpeed = 8.0f;
		constexpr float kWheelTranslationPerSpeedUnit = 0.12f;
		constexpr float kDragDollySpeed = 0.015f;
		constexpr float kMinOrbitDistance = 0.1f;
		constexpr float kShiftMoveMultiplier = 2.0f;

		float ResolveShiftMultiplier(const bool bShiftDown)
		{
			return bShiftDown ? kShiftMoveMultiplier : 1.0f;
		}
	}

	WheelTranslationResult ComputeWheelTranslation(
		const glm::vec3& refPosition,
		const glm::vec3& refOrbitTarget,
		const glm::vec3& refForward,
		const float fWheelDelta,
		const float fMoveSpeed,
		const bool bShiftDown)
	{
		const glm::vec3 vecTranslation =
			refForward *
			(fWheelDelta * fMoveSpeed * kWheelTranslationPerSpeedUnit * ResolveShiftMultiplier(bShiftDown));
		return { refPosition + vecTranslation, refOrbitTarget + vecTranslation };
	}

	glm::vec3 ComputePanTranslation(
		const glm::vec3& refRight,
		const glm::vec3& refUp,
		const float fMouseDeltaX,
		const float fMouseDeltaY,
		const float fPanUnitsPerPixel,
		const float fMoveSpeed,
		const bool bShiftDown)
	{
		const float fPanScale =
			fPanUnitsPerPixel *
			std::max(0.25f, fMoveSpeed / kReferenceMoveSpeed) *
			ResolveShiftMultiplier(bShiftDown);
		return
			(-refRight * fMouseDeltaX + refUp * fMouseDeltaY) *
			fPanScale;
	}

	float ComputeDollyDistanceDelta(
		const float fOrbitDistance,
		const float fMouseDeltaY,
		const float fMoveSpeed,
		const bool bShiftDown)
	{
		const float fSpeedScale =
			std::max(0.25f, fMoveSpeed / kReferenceMoveSpeed) *
			ResolveShiftMultiplier(bShiftDown);
		return
			std::max(fOrbitDistance * kDragDollySpeed, kMinOrbitDistance) *
			(-fMouseDeltaY) *
			fSpeedScale;
	}
}

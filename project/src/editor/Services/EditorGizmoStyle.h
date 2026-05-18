#pragma once

#include "Function/Gui/UICommon.h"

#include <glm/fwd.hpp>

#include <array>
#include <cstdint>

namespace AshEditor::EditorGizmoStyle
{
	inline constexpr std::array<AshEngine::UIColor, 3> kAxisColors{ {
		{ 1.00f, 0.00f, 0.00f, 0.98f },
		{ 0.00f, 1.00f, 0.00f, 0.98f },
		{ 0.00f, 0.00f, 1.00f, 0.98f }
	} };
	inline constexpr std::array<const char*, 3> kAxisLabels{ {
		"X",
		"Y",
		"Z"
	} };
	inline constexpr float kMoveGizmoPlaneFillAlpha = 0.12f;

	AshEngine::UIColor ScaleColor(const AshEngine::UIColor& refColor, float fScale);
	bool IsPointInsideRect(const AshEngine::UIRect& refRect, const glm::vec2& vecPoint);
	int32_t MakePlaneKey(int32_t iAxisA, int32_t iAxisB);
	AshEngine::UIColor MakePlaneHandleColor(int32_t iAxisA, int32_t iAxisB);
}

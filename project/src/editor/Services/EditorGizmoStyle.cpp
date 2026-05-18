#include "Services/EditorGizmoStyle.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstddef>

namespace AshEditor::EditorGizmoStyle
{
	AshEngine::UIColor ScaleColor(const AshEngine::UIColor& refColor, const float fScale)
	{
		return {
			std::clamp(refColor.r * fScale, 0.0f, 1.0f),
			std::clamp(refColor.g * fScale, 0.0f, 1.0f),
			std::clamp(refColor.b * fScale, 0.0f, 1.0f),
			refColor.a
		};
	}

	bool IsPointInsideRect(const AshEngine::UIRect& refRect, const glm::vec2& vecPoint)
	{
		return
			vecPoint.x >= refRect.x &&
			vecPoint.y >= refRect.y &&
			vecPoint.x <= refRect.x + refRect.width &&
			vecPoint.y <= refRect.y + refRect.height;
	}

	int32_t MakePlaneKey(const int32_t iAxisA, const int32_t iAxisB)
	{
		const int32_t iMinAxis = std::min(iAxisA, iAxisB);
		const int32_t iMaxAxis = std::max(iAxisA, iAxisB);
		return iMinAxis * 4 + iMaxAxis;
	}

	AshEngine::UIColor MakePlaneHandleColor(const int32_t iAxisA, const int32_t iAxisB)
	{
		const AshEngine::UIColor colorA = kAxisColors[static_cast<size_t>(iAxisA)];
		const AshEngine::UIColor colorB = kAxisColors[static_cast<size_t>(iAxisB)];
		return {
			(colorA.r + colorB.r) * 0.5f,
			(colorA.g + colorB.g) * 0.5f,
			(colorA.b + colorB.b) * 0.5f,
			kMoveGizmoPlaneFillAlpha
		};
	}
}

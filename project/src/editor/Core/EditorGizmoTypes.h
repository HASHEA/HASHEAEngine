#pragma once

#include <cstdint>

namespace AshEditor
{
	enum class GizmoMode : uint8_t
	{
		Move = 0,
		Rotate,
		Scale
	};

	enum class GizmoCoordinateSpace : uint8_t
	{
		Local = 0,
		World
	};

	enum class GizmoPivotMode : uint8_t
	{
		Pivot = 0,
		Center
	};

	struct GizmoSnapSettings
	{
		bool bSnapEnabled = false;
		float fMoveSnapStep = 1.0f;
		float fRotateSnapDegrees = 15.0f;
		float fScaleSnapStep = 0.1f;
	};

	struct EditorGizmoState
	{
		GizmoMode eMode = GizmoMode::Move;
		GizmoCoordinateSpace eSpace = GizmoCoordinateSpace::World;
		GizmoPivotMode ePivot = GizmoPivotMode::Pivot;
		GizmoSnapSettings snap{};
	};
}

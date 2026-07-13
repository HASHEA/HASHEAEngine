#pragma once

#include "Core/EditorGizmoTypes.h"
#include "Core/EditorSceneTypes.h"
#include "Function/Gui/UICommon.h"
#include "Function/Scene/SceneComponents.h"
#include "Services/EditorGizmoServiceTypes.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AshEditor::EditorGizmoInternal
{
	using ViewportContext = EditorGizmoViewportContext;
	using InteractionResult = EditorGizmoInteractionResult;

	struct GizmoBasis
	{
		SceneEntityId uEntityId = 0;
		glm::vec3 vecOrigin{ 0.0f };
		std::array<glm::vec3, 3> vecAxes{
			glm::vec3{ 1.0f, 0.0f, 0.0f },
			glm::vec3{ 0.0f, 1.0f, 0.0f },
			glm::vec3{ 0.0f, 0.0f, 1.0f }
		};
		bool bValid = false;
	};

	struct AxisVisual
	{
		glm::vec2 vecStartScreen{ 0.0f };
		glm::vec2 vecEndScreen{ 0.0f };
		glm::vec3 vecDirection{ 0.0f };
		float fWorldLength = 0.0f;
		bool bVisible = false;
	};

	struct PlaneHandleVisual
	{
		std::array<glm::vec2, 4> arrScreenCorners{};
		glm::vec3 vecAxisU{ 0.0f };
		glm::vec3 vecAxisV{ 0.0f };
		bool bVisible = false;
	};

	struct ScreenHandleVisual
	{
		glm::vec2 vecCenterScreen{ 0.0f };
		AshEngine::UIRect rectScreen{};
		glm::vec3 vecAxisU{ 0.0f };
		glm::vec3 vecAxisV{ 0.0f };
		bool bVisible = false;
	};

	struct MoveGizmoVisual
	{
		glm::vec2 vecOriginScreen{ 0.0f };
		std::array<AxisVisual, 3> axes{};
		std::array<PlaneHandleVisual, 3> planes{};
		ScreenHandleVisual screenHandle{};
		bool bOriginVisible = false;
	};

	inline constexpr size_t kRotateRingSampleCount = 49;

	struct RotateRingVisual
	{
		std::array<glm::vec2, kRotateRingSampleCount> vecScreenPoints{};
		std::array<bool, kRotateRingSampleCount> bPointVisible{};
		std::array<bool, kRotateRingSampleCount> bPointFrontFacing{};
		float fWorldRadius = 0.0f;
		bool bAnySegmentVisible = false;
	};

	struct RotateGizmoVisual
	{
		std::array<RotateRingVisual, 3> rings{};
		RotateRingVisual viewRing{};
	};

	enum class HandleKind : uint8_t
	{
		None = 0,
		Axis,
		Plane,
		Screen
	};

	struct HandleHit
	{
		HandleKind eKind = HandleKind::None;
		int32_t iPrimaryAxis = -1;
		int32_t iSecondaryAxis = -1;

		bool IsValid() const
		{
			return eKind != HandleKind::None;
		}
	};

	inline bool IsAxisIndexValid(const int32_t iAxisIndex)
	{
		return iAxisIndex >= 0 && iAxisIndex < 3;
	}

	struct DragSession
	{
		GizmoMode eMode = GizmoMode::Move;
		HandleKind eHandleKind = HandleKind::None;
		SceneEntityId uEntityId = 0;
		std::vector<SceneEntityId> vecEntityIds{};
		std::vector<AshEngine::TransformComponent> vecBeforeTransforms{};
		int32_t iAxisIndex = -1;
		int32_t iSecondaryAxisIndex = -1;
		AshEngine::TransformComponent beforeTransform{};
		glm::vec3 vecAxisDirection{ 0.0f };
		glm::vec3 vecGizmoOrigin{ 0.0f };
		glm::vec3 vecDragPlaneNormal{ 0.0f };
		glm::vec3 vecPlaneAxisU{ 0.0f };
		glm::vec3 vecPlaneAxisV{ 0.0f };
		glm::vec3 vecStartHitPoint{ 0.0f };
		glm::vec3 vecStartPlaneDirection{ 0.0f };
		glm::vec2 vecStartMousePosition{ 0.0f };
		float fStartAxisDistance = 0.0f;
		float fAxisVisualLength = 1.0f;
		float fCurrentRotateDeltaDegrees = 0.0f;
		bool bTransactionOpened = false;
		bool bActive = false;
	};

	struct GizmoDragUpdate
	{
		AshEngine::TransformComponent afterTransform{};
		glm::vec3 vecMoveWorldDelta{ 0.0f };
		glm::vec3 vecScaleDeltaNormalized{ 0.0f };
		float fRotateDeltaDegrees = 0.0f;
		bool bHasMoveWorldDelta = false;
		bool bHasScaleDelta = false;
		bool bHasRotateDelta = false;
		bool bHasTransform = false;
	};
}

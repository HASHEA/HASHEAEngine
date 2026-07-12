#include "Services/MoveScaleGizmoTool.h"

#include "Core/IEditorCommandExecutor.h"
#include "Services/EditorGizmoMath.h"
#include "Services/EditorGizmoSelectionUtils.h"
#include "Services/EditorGizmoStyle.h"
#include "Services/EditorGizmoTransform.h"
#include "Services/EditorGizmoViewport.h"
#include "Services/SceneService.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace AshEditor
{
	namespace
	{
		constexpr float kMoveGizmoLineThickness = 3.25f;
		constexpr float kMoveGizmoHoverLineThickness = 4.5f;
		constexpr float kMoveGizmoActiveLineThickness = 5.75f;
		constexpr float kMoveGizmoHoverThresholdPixels = 10.0f;
		constexpr float kMoveGizmoArrowHeadLength = 13.0f;
		constexpr float kMoveGizmoArrowHeadWidth = 8.0f;
		constexpr float kMoveGizmoPlaneHandleInnerScale = 0.26f;
		constexpr float kMoveGizmoPlaneHandleOuterScale = 0.42f;
		constexpr uint32_t kMoveGizmoPlaneHandleMaxFillSteps = 64u;
		constexpr float kMoveGizmoScreenHandleSizePixels = 18.0f;
		constexpr float kScaleGizmoHandleHalfExtent = 5.5f;

		using EditorGizmoInternal::AxisVisual;
		using EditorGizmoInternal::HandleHit;
		using EditorGizmoInternal::HandleKind;
		using EditorGizmoInternal::IsAxisIndexValid;
		using EditorGizmoInternal::PlaneHandleVisual;
		using EditorGizmoInternal::ScreenHandleVisual;
		using EditorGizmoMath::DistancePointToSegment;
		using EditorGizmoMath::IsPointInsideConvexQuad;
		using EditorGizmoMath::NormalizeOrFallback;
		using EditorGizmoStyle::IsPointInsideRect;
		using EditorGizmoStyle::MakePlaneHandleColor;
		using EditorGizmoStyle::MakePlaneKey;
		using EditorGizmoStyle::ScaleColor;
		using EditorGizmoStyle::kAxisColors;
		using EditorGizmoStyle::kAxisLabels;
		using EditorGizmoSelectionUtils::CaptureDragEntityTransforms;
		using EditorGizmoTransform::ComputeMovedTransform;
		using EditorGizmoTransform::ComputeScaledTransform;
		using EditorGizmoViewport::BuildViewportRay;
		using EditorGizmoViewport::ComputeAxisWorldLength;
		using EditorGizmoViewport::PlaneHandleProjectionDesc;
		using EditorGizmoViewport::TryBuildProjectedPlaneHandle;
		using EditorGizmoViewport::TryProjectWorldToViewport;

		static constexpr std::array<std::pair<int32_t, int32_t>, 3> kPlaneAxes{ {
			{ 0, 1 },
			{ 0, 2 },
			{ 1, 2 }
		} };

		bool ShouldSnapMoveDelta(const EditorGizmoState& refGizmoState)
		{
			return refGizmoState.snap.bSnapEnabled && refGizmoState.snap.fMoveSnapStep > 0.0001f;
		}

		float SnapMoveDistance(const float fDistance, const EditorGizmoState& refGizmoState)
		{
			if (!ShouldSnapMoveDelta(refGizmoState))
			{
				return fDistance;
			}
			return std::round(fDistance / refGizmoState.snap.fMoveSnapStep) * refGizmoState.snap.fMoveSnapStep;
		}

		glm::vec3 SnapMoveDeltaToPlaneAxes(
			const glm::vec3& vecWorldDelta,
			const glm::vec3& vecPlaneAxisU,
			const glm::vec3& vecPlaneAxisV,
			const EditorGizmoState& refGizmoState)
		{
			if (!ShouldSnapMoveDelta(refGizmoState))
			{
				return vecWorldDelta;
			}

			return
				vecPlaneAxisU * SnapMoveDistance(glm::dot(vecWorldDelta, vecPlaneAxisU), refGizmoState) +
				vecPlaneAxisV * SnapMoveDistance(glm::dot(vecWorldDelta, vecPlaneAxisV), refGizmoState);
		}

		void DrawPlaneHandle(
			AshEngine::UIContext& refUi,
			const PlaneHandleVisual& refVisual,
			const AshEngine::UIColor& refFillColor,
			const AshEngine::UIColor& refOutlineColor,
			const float fOutlineThickness)
		{
			const float fMaxFillSpan = std::max(
				glm::length(refVisual.arrScreenCorners[3] - refVisual.arrScreenCorners[0]),
				glm::length(refVisual.arrScreenCorners[2] - refVisual.arrScreenCorners[1]));
			const float fClampedFillSteps = std::isfinite(fMaxFillSpan)
				? std::clamp(
					std::ceil(fMaxFillSpan),
					1.0f,
					static_cast<float>(kMoveGizmoPlaneHandleMaxFillSteps))
				: static_cast<float>(kMoveGizmoPlaneHandleMaxFillSteps);
			const uint32_t uFillSteps = static_cast<uint32_t>(fClampedFillSteps);
			for (uint32_t uStep = 0; uStep <= uFillSteps; ++uStep)
			{
				const float fT = static_cast<float>(uStep) /
					static_cast<float>(uFillSteps);
				const glm::vec2 vecStart =
					refVisual.arrScreenCorners[0] +
					(refVisual.arrScreenCorners[3] - refVisual.arrScreenCorners[0]) * fT;
				const glm::vec2 vecEnd =
					refVisual.arrScreenCorners[1] +
					(refVisual.arrScreenCorners[2] - refVisual.arrScreenCorners[1]) * fT;
				refUi.draw_window_line(
					{ vecStart.x, vecStart.y },
					{ vecEnd.x, vecEnd.y },
					refFillColor,
					1.5f);
			}

			for (size_t uCornerIndex = 0; uCornerIndex < refVisual.arrScreenCorners.size(); ++uCornerIndex)
			{
				const glm::vec2& refStart = refVisual.arrScreenCorners[uCornerIndex];
				const glm::vec2& refEnd =
					refVisual.arrScreenCorners[(uCornerIndex + 1u) % refVisual.arrScreenCorners.size()];
				refUi.draw_window_line(
					{ refStart.x, refStart.y },
					{ refEnd.x, refEnd.y },
					refOutlineColor,
					fOutlineThickness);
			}
		}
	}

	bool MoveScaleGizmoTool::TryBuildVisual(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const EditorGizmoInternal::GizmoBasis& refBasis,
		EditorGizmoInternal::MoveGizmoVisual& outVisual)
	{
		outVisual = {};
		float fOriginDepth = 0.0f;
		if (!TryProjectWorldToViewport(refViewportContext, refBasis.vecOrigin, outVisual.vecOriginScreen, fOriginDepth))
		{
			return false;
		}

		outVisual.bOriginVisible = true;
		for (size_t uAxisIndex = 0; uAxisIndex < refBasis.vecAxes.size(); ++uAxisIndex)
		{
			AxisVisual& refAxisVisual = outVisual.axes[uAxisIndex];
			refAxisVisual.vecDirection = NormalizeOrFallback(refBasis.vecAxes[uAxisIndex], glm::vec3(1.0f, 0.0f, 0.0f));
			refAxisVisual.fWorldLength = ComputeAxisWorldLength(refViewportContext, refBasis.vecOrigin);
			refAxisVisual.vecStartScreen = outVisual.vecOriginScreen;

			const glm::vec3 vecAxisEndWorld =
				refBasis.vecOrigin +
				refAxisVisual.vecDirection * refAxisVisual.fWorldLength;
			float fAxisDepth = 0.0f;
			refAxisVisual.bVisible = TryProjectWorldToViewport(
				refViewportContext,
				vecAxisEndWorld,
				refAxisVisual.vecEndScreen,
				fAxisDepth);
		}

		for (size_t uPlaneIndex = 0; uPlaneIndex < kPlaneAxes.size(); ++uPlaneIndex)
		{
			const int32_t iAxisA = kPlaneAxes[uPlaneIndex].first;
			const int32_t iAxisB = kPlaneAxes[uPlaneIndex].second;
			if (!outVisual.axes[static_cast<size_t>(iAxisA)].bVisible ||
				!outVisual.axes[static_cast<size_t>(iAxisB)].bVisible)
			{
				continue;
			}

			PlaneHandleVisual& refPlaneVisual = outVisual.planes[uPlaneIndex];
			const AxisVisual& refAxisA = outVisual.axes[static_cast<size_t>(iAxisA)];
			const AxisVisual& refAxisB = outVisual.axes[static_cast<size_t>(iAxisB)];
			refPlaneVisual.vecAxisU = refAxisA.vecDirection;
			refPlaneVisual.vecAxisV = refAxisB.vecDirection;
			PlaneHandleProjectionDesc projectionDesc{};
			projectionDesc.vecOrigin = refBasis.vecOrigin;
			projectionDesc.vecAxisU = refPlaneVisual.vecAxisU;
			projectionDesc.vecAxisV = refPlaneVisual.vecAxisV;
			projectionDesc.fWorldLength = std::min(refAxisA.fWorldLength, refAxisB.fWorldLength);
			projectionDesc.fInnerScale = kMoveGizmoPlaneHandleInnerScale;
			projectionDesc.fOuterScale = kMoveGizmoPlaneHandleOuterScale;
			refPlaneVisual.bVisible = TryBuildProjectedPlaneHandle(
				refViewportContext,
				projectionDesc,
				refPlaneVisual.arrScreenCorners);
		}

		ScreenHandleVisual& refScreenHandle = outVisual.screenHandle;
		refScreenHandle.vecAxisU = glm::vec3(glm::inverse(refViewportContext.matView)[0]);
		refScreenHandle.vecAxisV = glm::vec3(glm::inverse(refViewportContext.matView)[1]);
		refScreenHandle.vecCenterScreen = outVisual.vecOriginScreen;
		refScreenHandle.rectScreen = {
			outVisual.vecOriginScreen.x - kMoveGizmoScreenHandleSizePixels * 0.5f,
			outVisual.vecOriginScreen.y - kMoveGizmoScreenHandleSizePixels * 0.5f,
			kMoveGizmoScreenHandleSizePixels,
			kMoveGizmoScreenHandleSizePixels
		};
		refScreenHandle.bVisible = true;

		return true;
	}

	EditorGizmoInternal::HandleHit MoveScaleGizmoTool::HitTestHandle(
		const EditorGizmoInternal::MoveGizmoVisual& refVisual,
		const bool bAllowPlaneHandles,
		const glm::vec2& vecMousePosition)
	{
		HandleHit result{};

		if (bAllowPlaneHandles)
		{
			for (size_t uPlaneIndex = 0; uPlaneIndex < refVisual.planes.size(); ++uPlaneIndex)
			{
				const PlaneHandleVisual& refPlaneVisual = refVisual.planes[uPlaneIndex];
				if (!refPlaneVisual.bVisible)
				{
					continue;
				}
				if (IsPointInsideConvexQuad(vecMousePosition, refPlaneVisual.arrScreenCorners))
				{
					result.eKind = HandleKind::Plane;
					result.iPrimaryAxis = kPlaneAxes[uPlaneIndex].first;
					result.iSecondaryAxis = kPlaneAxes[uPlaneIndex].second;
					return result;
				}
			}

			if (refVisual.screenHandle.bVisible &&
				IsPointInsideRect(refVisual.screenHandle.rectScreen, vecMousePosition))
			{
				result.eKind = HandleKind::Screen;
				return result;
			}
		}

		float fBestDistance = kMoveGizmoHoverThresholdPixels;
		for (size_t uAxisIndex = 0; uAxisIndex < refVisual.axes.size(); ++uAxisIndex)
		{
			const AxisVisual& refAxisVisual = refVisual.axes[uAxisIndex];
			if (!refAxisVisual.bVisible)
			{
				continue;
			}

			const float fDistance = DistancePointToSegment(
				vecMousePosition,
				refAxisVisual.vecStartScreen,
				refAxisVisual.vecEndScreen);
			if (fDistance <= fBestDistance)
			{
				fBestDistance = fDistance;
				result.eKind = HandleKind::Axis;
				result.iPrimaryAxis = static_cast<int32_t>(uAxisIndex);
				result.iSecondaryAxis = -1;
			}
		}

		return result;
	}

	bool MoveScaleGizmoTool::TryBuildDragPlaneNormal(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const glm::vec3& vecOrigin,
		const glm::vec3& vecAxisDirection,
		glm::vec3& outPlaneNormal)
	{
		const glm::vec3 vecAxis = NormalizeOrFallback(vecAxisDirection, glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::vec3 vecToCamera = NormalizeOrFallback(
			refViewportContext.vecCameraPosition - vecOrigin,
			glm::vec3(0.0f, 0.0f, -1.0f));
		const glm::vec3 vecPlaneTangent = glm::cross(vecToCamera, vecAxis);
		const float fTangentLength = glm::length(vecPlaneTangent);
		if (fTangentLength <= 0.0001f)
		{
			const glm::vec3 vecFallbackUp =
				std::abs(vecAxis.y) < 0.95f
				? glm::vec3(0.0f, 1.0f, 0.0f)
				: glm::vec3(1.0f, 0.0f, 0.0f);
			outPlaneNormal = NormalizeOrFallback(glm::cross(vecAxis, vecFallbackUp), glm::vec3(0.0f, 0.0f, 1.0f));
			return glm::length(outPlaneNormal) > 0.0001f;
		}

		outPlaneNormal = NormalizeOrFallback(glm::cross(vecAxis, vecPlaneTangent), glm::vec3(0.0f, 0.0f, 1.0f));
		return glm::length(outPlaneNormal) > 0.0001f;
	}

	void MoveScaleGizmoTool::DrawMove(
		AshEngine::UIContext& refUi,
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const EditorGizmoInternal::GizmoBasis& refBasis,
		const EditorGizmoInternal::MoveGizmoVisual& refVisual,
		const EditorGizmoInternal::HandleHit& refHoveredHandle,
		const EditorGizmoInternal::DragSession& refDragSession)
	{
		(void)refBasis;
		refUi.push_window_clip_rect(refViewportContext.rectContent);
		for (size_t uAxisIndex = 0; uAxisIndex < refVisual.axes.size(); ++uAxisIndex)
		{
			const AxisVisual& refAxisVisual = refVisual.axes[uAxisIndex];
			if (!refAxisVisual.bVisible)
			{
				continue;
			}

			const bool bIsActive =
				refDragSession.bActive &&
				refDragSession.eHandleKind == HandleKind::Axis &&
				refDragSession.iAxisIndex == static_cast<int32_t>(uAxisIndex);
			const bool bIsHovered =
				!bIsActive &&
				refHoveredHandle.eKind == HandleKind::Axis &&
				refHoveredHandle.iPrimaryAxis == static_cast<int32_t>(uAxisIndex);
			const AshEngine::UIColor color =
				bIsActive
				? ScaleColor(kAxisColors[uAxisIndex], 1.15f)
				: (bIsHovered ? ScaleColor(kAxisColors[uAxisIndex], 1.05f) : kAxisColors[uAxisIndex]);
			const float fThickness =
				bIsActive
				? kMoveGizmoActiveLineThickness
				: (bIsHovered ? kMoveGizmoHoverLineThickness : kMoveGizmoLineThickness);

			refUi.draw_window_line(
				{ refAxisVisual.vecStartScreen.x, refAxisVisual.vecStartScreen.y },
				{ refAxisVisual.vecEndScreen.x, refAxisVisual.vecEndScreen.y },
				color,
				fThickness);

			const glm::vec2 vecAxisDelta = refAxisVisual.vecEndScreen - refAxisVisual.vecStartScreen;
			const float fAxisLength = glm::length(vecAxisDelta);
			if (fAxisLength > 0.0001f)
			{
				const glm::vec2 vecAxisDir = vecAxisDelta / fAxisLength;
				const glm::vec2 vecPerp{ -vecAxisDir.y, vecAxisDir.x };
				const glm::vec2 vecArrowBase =
					refAxisVisual.vecEndScreen - vecAxisDir * kMoveGizmoArrowHeadLength;
				refUi.draw_window_line(
					{ refAxisVisual.vecEndScreen.x, refAxisVisual.vecEndScreen.y },
					{ vecArrowBase.x + vecPerp.x * kMoveGizmoArrowHeadWidth, vecArrowBase.y + vecPerp.y * kMoveGizmoArrowHeadWidth },
					color,
					fThickness);
				refUi.draw_window_line(
					{ refAxisVisual.vecEndScreen.x, refAxisVisual.vecEndScreen.y },
					{ vecArrowBase.x - vecPerp.x * kMoveGizmoArrowHeadWidth, vecArrowBase.y - vecPerp.y * kMoveGizmoArrowHeadWidth },
					color,
					fThickness);
			}
			refUi.draw_window_text(
				{ refAxisVisual.vecEndScreen.x + 6.0f, refAxisVisual.vecEndScreen.y - refUi.get_font_size() * 0.5f },
				color,
				kAxisLabels[uAxisIndex]);
		}

		for (size_t uPlaneIndex = 0; uPlaneIndex < refVisual.planes.size(); ++uPlaneIndex)
		{
			const PlaneHandleVisual& refPlaneVisual = refVisual.planes[uPlaneIndex];
			if (!refPlaneVisual.bVisible)
			{
				continue;
			}

			const int32_t iAxisA = kPlaneAxes[uPlaneIndex].first;
			const int32_t iAxisB = kPlaneAxes[uPlaneIndex].second;
			const bool bIsActive =
				refDragSession.bActive &&
				refDragSession.eHandleKind == HandleKind::Plane &&
				MakePlaneKey(refDragSession.iAxisIndex, refDragSession.iSecondaryAxisIndex) == MakePlaneKey(iAxisA, iAxisB);
			const bool bIsHovered =
				!bIsActive &&
				refHoveredHandle.eKind == HandleKind::Plane &&
				MakePlaneKey(refHoveredHandle.iPrimaryAxis, refHoveredHandle.iSecondaryAxis) == MakePlaneKey(iAxisA, iAxisB);
			const AshEngine::UIColor colorFill =
				bIsActive
				? MakePlaneHandleColor(iAxisA, iAxisB)
				: (bIsHovered
					? ScaleColor(MakePlaneHandleColor(iAxisA, iAxisB), 1.25f)
					: MakePlaneHandleColor(iAxisA, iAxisB));
			const AshEngine::UIColor colorOutline{
				(colorFill.r + kAxisColors[static_cast<size_t>(iAxisA)].r + kAxisColors[static_cast<size_t>(iAxisB)].r) / 3.0f,
				(colorFill.g + kAxisColors[static_cast<size_t>(iAxisA)].g + kAxisColors[static_cast<size_t>(iAxisB)].g) / 3.0f,
				(colorFill.b + kAxisColors[static_cast<size_t>(iAxisA)].b + kAxisColors[static_cast<size_t>(iAxisB)].b) / 3.0f,
				bIsActive ? 0.92f : 0.72f
			};
			DrawPlaneHandle(
				refUi,
				refPlaneVisual,
				colorFill,
				colorOutline,
				bIsActive ? 2.0f : 1.0f);
		}

		if (refVisual.bOriginVisible)
		{
			const bool bScreenActive = refDragSession.bActive && refDragSession.eHandleKind == HandleKind::Screen;
			const bool bScreenHovered = !bScreenActive && refHoveredHandle.eKind == HandleKind::Screen;
			const AshEngine::UIRect rectOrigin{
				refVisual.screenHandle.rectScreen.x,
				refVisual.screenHandle.rectScreen.y,
				refVisual.screenHandle.rectScreen.width,
				refVisual.screenHandle.rectScreen.height
			};
			const AshEngine::UIColor colorCenter =
				bScreenActive
				? AshEngine::UIColor{ 0.99f, 0.99f, 1.0f, 0.98f }
				: (bScreenHovered
					? AshEngine::UIColor{ 0.98f, 0.98f, 1.0f, 0.90f }
					: AshEngine::UIColor{ 0.94f, 0.96f, 0.99f, 0.82f });
			refUi.draw_window_rect_filled(rectOrigin, colorCenter, 2.0f);
			refUi.draw_window_rect(rectOrigin, { 0.30f, 0.34f, 0.40f, 0.92f }, 2.0f, bScreenActive ? 2.0f : 1.0f);
		}
		refUi.pop_window_clip_rect();
	}

	void MoveScaleGizmoTool::DrawScale(
		AshEngine::UIContext& refUi,
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const EditorGizmoInternal::GizmoBasis& refBasis,
		const EditorGizmoInternal::MoveGizmoVisual& refVisual,
		const EditorGizmoInternal::HandleHit& refHoveredHandle,
		const EditorGizmoInternal::DragSession& refDragSession)
	{
		(void)refBasis;
		refUi.push_window_clip_rect(refViewportContext.rectContent);
		for (size_t uAxisIndex = 0; uAxisIndex < refVisual.axes.size(); ++uAxisIndex)
		{
			const AxisVisual& refAxisVisual = refVisual.axes[uAxisIndex];
			if (!refAxisVisual.bVisible)
			{
				continue;
			}

			const bool bIsActive =
				refDragSession.bActive &&
				refDragSession.eHandleKind == HandleKind::Axis &&
				refDragSession.iAxisIndex == static_cast<int32_t>(uAxisIndex);
			const bool bIsHovered =
				!bIsActive &&
				refHoveredHandle.eKind == HandleKind::Axis &&
				refHoveredHandle.iPrimaryAxis == static_cast<int32_t>(uAxisIndex);
			const AshEngine::UIColor color =
				bIsActive
				? ScaleColor(kAxisColors[uAxisIndex], 1.15f)
				: (bIsHovered ? ScaleColor(kAxisColors[uAxisIndex], 1.05f) : kAxisColors[uAxisIndex]);
			const float fThickness =
				bIsActive
				? kMoveGizmoActiveLineThickness
				: (bIsHovered ? kMoveGizmoHoverLineThickness : kMoveGizmoLineThickness);

			refUi.draw_window_line(
				{ refAxisVisual.vecStartScreen.x, refAxisVisual.vecStartScreen.y },
				{ refAxisVisual.vecEndScreen.x, refAxisVisual.vecEndScreen.y },
				color,
				fThickness);

			const float fHandleHalfExtent =
				kScaleGizmoHandleHalfExtent +
				(bIsActive ? 2.0f : (bIsHovered ? 1.0f : 0.0f));
			const AshEngine::UIRect rectHandle{
				refAxisVisual.vecEndScreen.x - fHandleHalfExtent,
				refAxisVisual.vecEndScreen.y - fHandleHalfExtent,
				fHandleHalfExtent * 2.0f,
				fHandleHalfExtent * 2.0f
			};
			refUi.draw_window_rect_filled(rectHandle, color, 1.5f);
			refUi.draw_window_rect(
				rectHandle,
				{ 0.10f, 0.12f, 0.16f, 0.90f },
				1.5f,
				bIsActive ? 2.0f : 1.0f);
			refUi.draw_window_text(
				{ refAxisVisual.vecEndScreen.x + 8.0f, refAxisVisual.vecEndScreen.y - refUi.get_font_size() * 0.5f },
				color,
				kAxisLabels[uAxisIndex]);
		}

		for (size_t uPlaneIndex = 0; uPlaneIndex < refVisual.planes.size(); ++uPlaneIndex)
		{
			const PlaneHandleVisual& refPlaneVisual = refVisual.planes[uPlaneIndex];
			if (!refPlaneVisual.bVisible)
			{
				continue;
			}

			const int32_t iAxisA = kPlaneAxes[uPlaneIndex].first;
			const int32_t iAxisB = kPlaneAxes[uPlaneIndex].second;
			const bool bIsActive =
				refDragSession.bActive &&
				refDragSession.eHandleKind == HandleKind::Plane &&
				MakePlaneKey(refDragSession.iAxisIndex, refDragSession.iSecondaryAxisIndex) == MakePlaneKey(iAxisA, iAxisB);
			const bool bIsHovered =
				!bIsActive &&
				refHoveredHandle.eKind == HandleKind::Plane &&
				MakePlaneKey(refHoveredHandle.iPrimaryAxis, refHoveredHandle.iSecondaryAxis) == MakePlaneKey(iAxisA, iAxisB);
			const AshEngine::UIColor colorFill =
				bIsActive
				? AshEngine::UIColor{ 0.96f, 0.98f, 1.0f, 0.32f }
				: (bIsHovered
					? AshEngine::UIColor{ 0.94f, 0.97f, 1.0f, 0.24f }
					: AshEngine::UIColor{ 0.92f, 0.95f, 1.0f, 0.16f });
			const AshEngine::UIColor colorOutline{
				(kAxisColors[static_cast<size_t>(iAxisA)].r + kAxisColors[static_cast<size_t>(iAxisB)].r) * 0.5f,
				(kAxisColors[static_cast<size_t>(iAxisA)].g + kAxisColors[static_cast<size_t>(iAxisB)].g) * 0.5f,
				(kAxisColors[static_cast<size_t>(iAxisA)].b + kAxisColors[static_cast<size_t>(iAxisB)].b) * 0.5f,
				bIsActive ? 0.92f : 0.76f
			};
			DrawPlaneHandle(
				refUi,
				refPlaneVisual,
				colorFill,
				colorOutline,
				bIsActive ? 2.0f : 1.0f);
		}

		if (refVisual.screenHandle.bVisible)
		{
			const bool bScreenActive = refDragSession.bActive && refDragSession.eHandleKind == HandleKind::Screen;
			const bool bScreenHovered = !bScreenActive && refHoveredHandle.eKind == HandleKind::Screen;
			AshEngine::UIRect rectOrigin{
				refVisual.screenHandle.rectScreen.x,
				refVisual.screenHandle.rectScreen.y,
				refVisual.screenHandle.rectScreen.width,
				refVisual.screenHandle.rectScreen.height
			};
			if (bScreenActive)
			{
				rectOrigin.x -= 1.0f;
				rectOrigin.y -= 1.0f;
				rectOrigin.width += 2.0f;
				rectOrigin.height += 2.0f;
			}
			refUi.draw_window_rect_filled(
				rectOrigin,
				bScreenActive
					? AshEngine::UIColor{ 0.99f, 0.99f, 1.0f, 0.98f }
					: (bScreenHovered
						? AshEngine::UIColor{ 0.98f, 0.98f, 1.0f, 0.92f }
						: AshEngine::UIColor{ 0.94f, 0.96f, 0.99f, 0.82f }),
				2.0f);
			refUi.draw_window_rect(rectOrigin, { 0.30f, 0.34f, 0.40f, 0.92f }, 2.0f, bScreenActive ? 2.0f : 1.0f);
		}
		refUi.pop_window_clip_rect();
	}

	bool MoveScaleGizmoTool::TryBuildDragSession(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const SceneService& refSceneService,
		const SelectionService& refSelectionService,
		const EditorGizmoInternal::GizmoBasis& refBasis,
		const EditorGizmoInternal::MoveGizmoVisual& refVisual,
		const EditorGizmoInternal::HandleHit& refHoveredHandle,
		const EditorGizmoState& refGizmoState,
		const glm::vec2& vecMousePosition,
		EditorGizmoInternal::DragSession& outDragSession) const
	{
		outDragSession = {};
		if (refGizmoState.eMode != GizmoMode::Move && refGizmoState.eMode != GizmoMode::Scale)
		{
			return false;
		}

		const AshEngine::Entity entity = refSceneService.FindEntity(refBasis.uEntityId);
		if (!entity.is_valid())
		{
			return false;
		}

		glm::vec3 vecPlaneNormal{ 0.0f };
		glm::vec3 vecPlaneAxisU{ 0.0f };
		glm::vec3 vecPlaneAxisV{ 0.0f };
		if (refHoveredHandle.eKind == HandleKind::Plane)
		{
			if (!IsAxisIndexValid(refHoveredHandle.iPrimaryAxis) ||
				!IsAxisIndexValid(refHoveredHandle.iSecondaryAxis))
			{
				return false;
			}

			vecPlaneAxisU = NormalizeOrFallback(
				refBasis.vecAxes[static_cast<size_t>(refHoveredHandle.iPrimaryAxis)],
				glm::vec3(1.0f, 0.0f, 0.0f));
			vecPlaneAxisV = NormalizeOrFallback(
				refBasis.vecAxes[static_cast<size_t>(refHoveredHandle.iSecondaryAxis)],
				glm::vec3(0.0f, 1.0f, 0.0f));
			vecPlaneNormal = NormalizeOrFallback(
				glm::cross(vecPlaneAxisU, vecPlaneAxisV),
				glm::vec3(0.0f, 0.0f, 1.0f));
		}
		else if (refHoveredHandle.eKind == HandleKind::Screen)
		{
			vecPlaneAxisU = NormalizeOrFallback(refVisual.screenHandle.vecAxisU, glm::vec3(1.0f, 0.0f, 0.0f));
			vecPlaneAxisV = NormalizeOrFallback(refVisual.screenHandle.vecAxisV, glm::vec3(0.0f, 1.0f, 0.0f));
			vecPlaneNormal = NormalizeOrFallback(
				glm::cross(vecPlaneAxisU, vecPlaneAxisV),
				glm::vec3(0.0f, 0.0f, 1.0f));
		}
		else
		{
			if (!IsAxisIndexValid(refHoveredHandle.iPrimaryAxis))
			{
				return false;
			}
			if (!TryBuildDragPlaneNormal(
				refViewportContext,
				refBasis.vecOrigin,
				refBasis.vecAxes[static_cast<size_t>(refHoveredHandle.iPrimaryAxis)],
				vecPlaneNormal))
			{
				return false;
			}
		}

		const AshEngine::SceneRay ray = BuildViewportRay(refViewportContext, vecMousePosition);
		glm::vec3 vecHitPoint{ 0.0f };
		if (!EditorGizmoMath::TryIntersectRayPlane(
			ray.origin,
			ray.direction,
			refBasis.vecOrigin,
			vecPlaneNormal,
			vecHitPoint))
		{
			return false;
		}

		outDragSession.eMode = refGizmoState.eMode;
		outDragSession.eHandleKind = refHoveredHandle.eKind;
		outDragSession.uEntityId = refBasis.uEntityId;
		outDragSession.iAxisIndex = refHoveredHandle.iPrimaryAxis;
		outDragSession.iSecondaryAxisIndex = refHoveredHandle.iSecondaryAxis;
		outDragSession.beforeTransform = entity.get_transform_component();
		CaptureDragEntityTransforms(
			refSceneService,
			refSelectionService,
			refBasis.uEntityId,
			outDragSession.beforeTransform,
			outDragSession.vecEntityIds,
			outDragSession.vecBeforeTransforms);
		outDragSession.vecAxisDirection =
			IsAxisIndexValid(refHoveredHandle.iPrimaryAxis)
			? NormalizeOrFallback(
				refBasis.vecAxes[static_cast<size_t>(refHoveredHandle.iPrimaryAxis)],
				glm::vec3(1.0f, 0.0f, 0.0f))
			: glm::vec3(0.0f);
		outDragSession.vecGizmoOrigin = refBasis.vecOrigin;
		outDragSession.vecDragPlaneNormal = vecPlaneNormal;
		outDragSession.vecPlaneAxisU = vecPlaneAxisU;
		outDragSession.vecPlaneAxisV = vecPlaneAxisV;
		outDragSession.vecStartHitPoint = vecHitPoint;
		outDragSession.vecStartPlaneDirection = NormalizeOrFallback(
			vecHitPoint - refBasis.vecOrigin,
			outDragSession.vecAxisDirection);
		outDragSession.vecStartMousePosition = vecMousePosition;
		outDragSession.fStartAxisDistance =
			refHoveredHandle.eKind == HandleKind::Axis
			? glm::dot(vecHitPoint - refBasis.vecOrigin, outDragSession.vecAxisDirection)
			: 0.0f;
		outDragSession.fAxisVisualLength =
			refHoveredHandle.eKind == HandleKind::Axis
			? std::max(refVisual.axes[static_cast<size_t>(refHoveredHandle.iPrimaryAxis)].fWorldLength, 0.001f)
			: ComputeAxisWorldLength(refViewportContext, refBasis.vecOrigin);
		outDragSession.bActive = true;
		return true;
	}

	void MoveScaleGizmoTool::BeginDragSession(
		const EditorGizmoInternal::DragSession& refDragSession,
		const bool bTransactionOpened)
	{
		_dragSession = refDragSession;
		_dragSession.bTransactionOpened = bTransactionOpened;
		_dragSession.bActive = true;
	}

	bool MoveScaleGizmoTool::TryUpdateDrag(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const SceneService& refSceneService,
		const EditorGizmoState& refGizmoState,
		const glm::vec2& vecMousePosition,
		EditorGizmoInternal::GizmoDragUpdate& outUpdate) const
	{
		outUpdate = {};
		if (!_dragSession.bActive ||
			(_dragSession.eMode != GizmoMode::Move && _dragSession.eMode != GizmoMode::Scale))
		{
			return false;
		}

		const AshEngine::SceneRay ray = BuildViewportRay(refViewportContext, vecMousePosition);
		glm::vec3 vecHitPoint{ 0.0f };
		const bool bUseUniformScreenScale =
			_dragSession.eMode == GizmoMode::Scale &&
			_dragSession.eHandleKind == HandleKind::Screen;
		if (!bUseUniformScreenScale &&
			!EditorGizmoMath::TryIntersectRayPlane(
				ray.origin,
				ray.direction,
				_dragSession.vecGizmoOrigin,
				_dragSession.vecDragPlaneNormal,
				vecHitPoint))
		{
			return false;
		}

		glm::vec3 vecWorldDelta{ 0.0f };
		glm::vec3 vecScaleDeltaNormalized{ 0.0f };
		if (_dragSession.eHandleKind == HandleKind::Plane)
		{
			const glm::vec3 vecPlaneDelta = vecHitPoint - _dragSession.vecStartHitPoint;
			const float fPlaneDeltaU = glm::dot(vecPlaneDelta, _dragSession.vecPlaneAxisU);
			const float fPlaneDeltaV = glm::dot(vecPlaneDelta, _dragSession.vecPlaneAxisV);
			if (_dragSession.eMode == GizmoMode::Scale)
			{
				const float fSafeVisualLength = std::max(_dragSession.fAxisVisualLength, 0.001f);
				if (_dragSession.iAxisIndex >= 0 && _dragSession.iAxisIndex < 3)
				{
					vecScaleDeltaNormalized[static_cast<size_t>(_dragSession.iAxisIndex)] =
						fPlaneDeltaU / fSafeVisualLength;
				}
				if (_dragSession.iSecondaryAxisIndex >= 0 && _dragSession.iSecondaryAxisIndex < 3)
				{
					vecScaleDeltaNormalized[static_cast<size_t>(_dragSession.iSecondaryAxisIndex)] =
						fPlaneDeltaV / fSafeVisualLength;
				}
			}
			else
			{
				vecWorldDelta =
					_dragSession.vecPlaneAxisU * fPlaneDeltaU +
					_dragSession.vecPlaneAxisV * fPlaneDeltaV;
				vecWorldDelta = SnapMoveDeltaToPlaneAxes(
					vecWorldDelta,
					_dragSession.vecPlaneAxisU,
					_dragSession.vecPlaneAxisV,
					refGizmoState);
			}
		}
		else if (_dragSession.eHandleKind == HandleKind::Screen)
		{
			if (_dragSession.eMode == GizmoMode::Scale)
			{
				const glm::vec2 vecMouseDelta = vecMousePosition - _dragSession.vecStartMousePosition;
				const float fUniformDeltaNormalized =
					(vecMouseDelta.x - vecMouseDelta.y) /
					std::max(kTargetScreenLength, 1.0f);
				vecScaleDeltaNormalized = glm::vec3(fUniformDeltaNormalized);
			}
			else
			{
				const glm::vec3 vecPlaneDelta = vecHitPoint - _dragSession.vecStartHitPoint;
				vecWorldDelta =
					_dragSession.vecPlaneAxisU * glm::dot(vecPlaneDelta, _dragSession.vecPlaneAxisU) +
					_dragSession.vecPlaneAxisV * glm::dot(vecPlaneDelta, _dragSession.vecPlaneAxisV);
				vecWorldDelta = SnapMoveDeltaToPlaneAxes(
					vecWorldDelta,
					_dragSession.vecPlaneAxisU,
					_dragSession.vecPlaneAxisV,
					refGizmoState);
			}
		}
		else
		{
			float fAxisDistance =
				glm::dot(vecHitPoint - _dragSession.vecGizmoOrigin, _dragSession.vecAxisDirection) -
				_dragSession.fStartAxisDistance;
			fAxisDistance = SnapMoveDistance(fAxisDistance, refGizmoState);

			if (_dragSession.eMode == GizmoMode::Scale)
			{
				const float fSafeVisualLength = std::max(_dragSession.fAxisVisualLength, 0.001f);
				if (_dragSession.iAxisIndex >= 0 && _dragSession.iAxisIndex < 3)
				{
					vecScaleDeltaNormalized[static_cast<size_t>(_dragSession.iAxisIndex)] =
						fAxisDistance / fSafeVisualLength;
				}
			}
			else
			{
				vecWorldDelta = _dragSession.vecAxisDirection * fAxisDistance;
			}
		}

		if (_dragSession.eMode == GizmoMode::Scale)
		{
			outUpdate.vecScaleDeltaNormalized = vecScaleDeltaNormalized;
			outUpdate.bHasScaleDelta = true;
			outUpdate.afterTransform = ComputeScaledTransform(
				_dragSession.beforeTransform,
				vecScaleDeltaNormalized,
				refGizmoState);
		}
		else
		{
			outUpdate.vecMoveWorldDelta = vecWorldDelta;
			outUpdate.bHasMoveWorldDelta = true;
			outUpdate.afterTransform = ComputeMovedTransform(
				refSceneService,
				_dragSession.uEntityId,
				_dragSession.beforeTransform,
				vecWorldDelta);
		}
		outUpdate.bHasTransform = true;
		return true;
	}

	void MoveScaleGizmoTool::SetHoveredHandle(const EditorGizmoInternal::HandleHit& refHoveredHandle)
	{
		_hoveredHandle = refHoveredHandle;
	}

	void MoveScaleGizmoTool::ClearHoveredHandle()
	{
		_hoveredHandle = {};
	}

	const EditorGizmoInternal::HandleHit& MoveScaleGizmoTool::GetHoveredHandle() const
	{
		return _hoveredHandle;
	}

	const EditorGizmoInternal::DragSession& MoveScaleGizmoTool::GetDragSession() const
	{
		return _dragSession;
	}

	void MoveScaleGizmoTool::ResetInteraction()
	{
		_dragSession = {};
		_hoveredHandle = {};
	}

	void MoveScaleGizmoTool::CancelInteraction(IEditorCommandExecutor& refCommandExecutor)
	{
		if (_dragSession.bTransactionOpened)
		{
			refCommandExecutor.CancelCommandTransaction();
		}
		ResetInteraction();
	}
}

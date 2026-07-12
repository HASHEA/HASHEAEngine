#include "Services/RotateGizmoTool.h"

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
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace AshEditor
{
	namespace
	{
		constexpr float kRotateGizmoRadiusScale = 0.88f;
		constexpr float kRotateViewRingRadiusScale = 1.08f;
		constexpr float kRotateGizmoLineThickness = 3.25f;
		constexpr float kRotateGizmoHoverLineThickness = 4.75f;
		constexpr float kRotateGizmoActiveLineThickness = 6.0f;
		constexpr float kRotateViewRingLineThickness = 2.4f;
		constexpr float kRotateGizmoHoverThresholdPixels = 13.5f;
		constexpr int32_t kRotateLabelSampleIndex = 6;
		constexpr float kRotateRingMarkerHalfExtent = 4.5f;
		constexpr float kRotateRingMarkerOutlineThickness = 1.25f;
		constexpr float kRotateRingShadowExtraThickness = 2.0f;
		constexpr float kRotateActiveWedgeStepDegrees = 6.0f;
		constexpr float kRotateActiveWedgeMaxDisplayDegrees = 360.0f;
		constexpr float kRotateActiveWedgeFillThickness = 1.15f;
		constexpr float kRotateActiveWedgeBoundaryThickness = 2.6f;
		constexpr float kRotateActiveWedgeArrowLengthPixels = 13.0f;
		constexpr float kRotateActiveWedgeArrowWidthPixels = 7.0f;
		constexpr AshEngine::UIColor kRotateViewRingColor{ 0.93f, 0.95f, 0.99f, 0.36f };
		constexpr AshEngine::UIColor kRotateRingShadowColor{ 0.04f, 0.05f, 0.07f, 0.78f };
		constexpr AshEngine::UIColor kRotateActiveLabelFillColor{ 0.04f, 0.05f, 0.07f, 0.88f };
		constexpr AshEngine::UIColor kRotateActiveLabelTextColor{ 0.98f, 0.98f, 0.94f, 0.98f };

		using EditorGizmoInternal::HandleHit;
		using EditorGizmoInternal::HandleKind;
		using EditorGizmoInternal::IsAxisIndexValid;
		using EditorGizmoInternal::RotateRingVisual;
		using EditorGizmoInternal::kRotateRingSampleCount;
		using EditorGizmoMath::DistancePointToSegment;
		using EditorGizmoMath::NormalizeOrFallback;
		using EditorGizmoMath::TryBuildPerpendicularBasis;
		using EditorGizmoStyle::ScaleColor;
		using EditorGizmoStyle::kAxisColors;
		using EditorGizmoStyle::kAxisLabels;
		using EditorGizmoSelectionUtils::CaptureDragEntityTransforms;
		using EditorGizmoTransform::ComputeRotatedTransform;
		using EditorGizmoViewport::BuildViewportRay;
		using EditorGizmoViewport::ComputeAxisWorldLength;
		using EditorGizmoViewport::ComputeCameraForward;
		using EditorGizmoViewport::TryProjectWorldToViewport;

		float SnapRotateDeltaDegrees(const float fDeltaDegrees, const EditorGizmoState& refGizmoState)
		{
			if (!refGizmoState.snap.bSnapEnabled || refGizmoState.snap.fRotateSnapDegrees <= 0.0001f)
			{
				return fDeltaDegrees;
			}

			return std::round(fDeltaDegrees / refGizmoState.snap.fRotateSnapDegrees) *
				refGizmoState.snap.fRotateSnapDegrees;
		}

		bool BuildProjectedRotateRing(
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const glm::vec3& vecOrigin,
			const glm::vec3& vecRingTangent,
			const glm::vec3& vecRingBitangent,
			const float fWorldRadius,
			RotateRingVisual& outRing)
		{
			outRing = {};
			outRing.fWorldRadius = std::max(fWorldRadius, 0.001f);
			const glm::vec3 vecToCamera = NormalizeOrFallback(
				refViewportContext.vecCameraPosition - vecOrigin,
				glm::vec3(0.0f, 0.0f, 1.0f));
			for (size_t uSampleIndex = 0; uSampleIndex < outRing.vecScreenPoints.size(); ++uSampleIndex)
			{
				const float fAngle =
					(static_cast<float>(uSampleIndex) /
					 static_cast<float>(outRing.vecScreenPoints.size() - 1)) *
					glm::two_pi<float>();
				const glm::vec3 vecPointWorld =
					vecOrigin +
					(vecRingTangent * std::cos(fAngle) + vecRingBitangent * std::sin(fAngle)) * outRing.fWorldRadius;
				float fDepth = 0.0f;
				outRing.bPointVisible[uSampleIndex] = TryProjectWorldToViewport(
					refViewportContext,
					vecPointWorld,
					outRing.vecScreenPoints[uSampleIndex],
					fDepth);
				outRing.bPointFrontFacing[uSampleIndex] =
					glm::dot(vecPointWorld - vecOrigin, vecToCamera) >= 0.0f;
			}

			for (size_t uSampleIndex = 0; uSampleIndex + 1 < outRing.vecScreenPoints.size(); ++uSampleIndex)
			{
				if (outRing.bPointVisible[uSampleIndex] && outRing.bPointVisible[uSampleIndex + 1])
				{
					outRing.bAnySegmentVisible = true;
					break;
				}
			}

			return outRing.bAnySegmentVisible;
		}

		bool TryFindVisibleRotateRingSample(
			const RotateRingVisual& refRing,
			const size_t uPreferredIndex,
			const bool bRequireFrontFacing,
			size_t& outIndex)
		{
			const size_t uSampleCount = refRing.vecScreenPoints.size();
			if (uSampleCount == 0)
			{
				return false;
			}

			const size_t uWrappedIndex = uPreferredIndex % uSampleCount;
			if (refRing.bPointVisible[uWrappedIndex] &&
				(!bRequireFrontFacing || refRing.bPointFrontFacing[uWrappedIndex]))
			{
				outIndex = uWrappedIndex;
				return true;
			}

			for (size_t uOffset = 1; uOffset < uSampleCount; ++uOffset)
			{
				const size_t uForward = (uWrappedIndex + uOffset) % uSampleCount;
				if (refRing.bPointVisible[uForward] &&
					(!bRequireFrontFacing || refRing.bPointFrontFacing[uForward]))
				{
					outIndex = uForward;
					return true;
				}

				const size_t uBackward = (uWrappedIndex + uSampleCount - uOffset) % uSampleCount;
				if (refRing.bPointVisible[uBackward] &&
					(!bRequireFrontFacing || refRing.bPointFrontFacing[uBackward]))
				{
					outIndex = uBackward;
					return true;
				}
			}

			return false;
		}

		void DrawRotateRingSegments(
			AshEngine::UIContext& refUi,
			const RotateRingVisual& refRing,
			const AshEngine::UIColor& refColor,
			const float fThickness,
			const bool bFrontPass)
		{
			for (size_t uSampleIndex = 0; uSampleIndex + 1 < refRing.vecScreenPoints.size(); ++uSampleIndex)
			{
				if (!refRing.bPointVisible[uSampleIndex] || !refRing.bPointVisible[uSampleIndex + 1])
				{
					continue;
				}

				const bool bSegmentFrontFacing =
					refRing.bPointFrontFacing[uSampleIndex] || refRing.bPointFrontFacing[uSampleIndex + 1];
				if (bSegmentFrontFacing != bFrontPass)
				{
					continue;
				}

				const AshEngine::UIColor shadowColor{
					kRotateRingShadowColor.r,
					kRotateRingShadowColor.g,
					kRotateRingShadowColor.b,
					bFrontPass ? kRotateRingShadowColor.a : (kRotateRingShadowColor.a * 0.55f)
				};
				const AshEngine::UIColor lineColor{
					refColor.r,
					refColor.g,
					refColor.b,
					bFrontPass ? refColor.a : (refColor.a * 0.38f)
				};
				const float fResolvedThickness =
					bFrontPass ? fThickness : std::max(fThickness - 0.85f, 1.0f);

				refUi.draw_window_line(
					{ refRing.vecScreenPoints[uSampleIndex].x, refRing.vecScreenPoints[uSampleIndex].y },
					{ refRing.vecScreenPoints[uSampleIndex + 1].x, refRing.vecScreenPoints[uSampleIndex + 1].y },
					shadowColor,
					fResolvedThickness + kRotateRingShadowExtraThickness);
				refUi.draw_window_line(
					{ refRing.vecScreenPoints[uSampleIndex].x, refRing.vecScreenPoints[uSampleIndex].y },
					{ refRing.vecScreenPoints[uSampleIndex + 1].x, refRing.vecScreenPoints[uSampleIndex + 1].y },
					lineColor,
					fResolvedThickness);
			}
		}

		glm::vec3 RotateVectorAroundAxis(
			const glm::vec3& vecDirection,
			const glm::vec3& vecAxisDirection,
			const float fDeltaDegrees)
		{
			const glm::vec3 vecAxis = NormalizeOrFallback(vecAxisDirection, glm::vec3(0.0f, 1.0f, 0.0f));
			const float fRadians = glm::radians(fDeltaDegrees);
			const float fCos = std::cos(fRadians);
			const float fSin = std::sin(fRadians);
			return
				vecDirection * fCos +
				glm::cross(vecAxis, vecDirection) * fSin +
				vecAxis * glm::dot(vecAxis, vecDirection) * (1.0f - fCos);
		}

		AshEngine::UIColor MakeWedgeFillColor(const AshEngine::UIColor& refColor)
		{
			return { refColor.r, refColor.g, refColor.b, 0.14f };
		}

		AshEngine::UIColor MakeWedgeBoundaryColor(const AshEngine::UIColor& refColor)
		{
			return { refColor.r, refColor.g, refColor.b, 0.88f };
		}

		bool TryProjectRotateDirection(
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const EditorGizmoInternal::DragSession& refDragSession,
			const float fDeltaDegrees,
			glm::vec2& outScreenPosition)
		{
			const glm::vec3 vecDirection = NormalizeOrFallback(
				RotateVectorAroundAxis(
					refDragSession.vecStartPlaneDirection,
					refDragSession.vecAxisDirection,
					fDeltaDegrees),
				refDragSession.vecStartPlaneDirection);
			const glm::vec3 vecWorldPosition =
				refDragSession.vecGizmoOrigin +
				vecDirection * std::max(refDragSession.fAxisVisualLength, 0.001f);
			float fDepth = 0.0f;
			return TryProjectWorldToViewport(refViewportContext, vecWorldPosition, outScreenPosition, fDepth);
		}

		float ClampRotateWedgeDisplayDegrees(const float fRotateDeltaDegrees)
		{
			return std::clamp(
				fRotateDeltaDegrees,
				-kRotateActiveWedgeMaxDisplayDegrees,
				kRotateActiveWedgeMaxDisplayDegrees);
		}

		void DrawRotateWedgeArrow(
			AshEngine::UIContext& refUi,
			const glm::vec2& vecCenterScreen,
			const glm::vec2& vecCurrentScreen,
			const float fRotateDeltaDegrees,
			const AshEngine::UIColor& refColor)
		{
			const glm::vec2 vecRadial = vecCurrentScreen - vecCenterScreen;
			const float fRadialLength = glm::length(vecRadial);
			if (fRadialLength <= 0.001f)
			{
				return;
			}

			const glm::vec2 vecForward = vecRadial / fRadialLength;
			const float fDirectionSign = fRotateDeltaDegrees >= 0.0f ? 1.0f : -1.0f;
			const glm::vec2 vecTangent{ -vecForward.y * fDirectionSign, vecForward.x * fDirectionSign };
			const glm::vec2 vecTip = vecCurrentScreen + vecTangent * kRotateActiveWedgeArrowLengthPixels;
			const glm::vec2 vecLeft =
				vecCurrentScreen -
				vecTangent * kRotateActiveWedgeArrowWidthPixels +
				vecForward * kRotateActiveWedgeArrowWidthPixels;
			const glm::vec2 vecRight =
				vecCurrentScreen -
				vecTangent * kRotateActiveWedgeArrowWidthPixels -
				vecForward * kRotateActiveWedgeArrowWidthPixels;

			refUi.draw_window_line(
				{ vecTip.x, vecTip.y },
				{ vecLeft.x, vecLeft.y },
				refColor,
				kRotateActiveWedgeBoundaryThickness);
			refUi.draw_window_line(
				{ vecTip.x, vecTip.y },
				{ vecRight.x, vecRight.y },
				refColor,
				kRotateActiveWedgeBoundaryThickness);
		}

		void DrawRotateWedgeLabel(
			AshEngine::UIContext& refUi,
			const glm::vec2& vecCenterScreen,
			const glm::vec2& vecLabelAnchorScreen,
			const float fRotateDeltaDegrees,
			const AshEngine::UIColor& refColor)
		{
			char pLabel[64]{};
			std::snprintf(
				pLabel,
				sizeof(pLabel),
				"%+.1f deg",
				static_cast<double>(fRotateDeltaDegrees));

			const glm::vec2 vecLabelOffset = vecLabelAnchorScreen - vecCenterScreen;
			const float fLabelOffsetLength = glm::length(vecLabelOffset);
			const glm::vec2 vecAnchorDirection =
				fLabelOffsetLength > 0.001f
				? vecLabelOffset / fLabelOffsetLength
				: glm::vec2(1.0f, 0.0f);
			const AshEngine::UIVec2 vecTextSize = refUi.calc_text_size(pLabel);
			const glm::vec2 vecLabelPosition{
				vecLabelAnchorScreen.x + vecAnchorDirection.x * 14.0f,
				vecLabelAnchorScreen.y + vecAnchorDirection.y * 14.0f
			};
			const AshEngine::UIRect rectLabel{
				vecLabelPosition.x - 7.0f,
				vecLabelPosition.y - (vecTextSize.y * 0.5f) - 4.0f,
				vecTextSize.x + 14.0f,
				vecTextSize.y + 8.0f
			};

			refUi.draw_window_rect_filled(rectLabel, kRotateActiveLabelFillColor, 4.0f);
			refUi.draw_window_rect(rectLabel, refColor, 4.0f, 1.35f);
			refUi.draw_window_text(
				{ rectLabel.x + 7.0f, rectLabel.y + 4.0f },
				kRotateActiveLabelTextColor,
				pLabel);
		}

		void DrawActiveRotateAngleOverlay(
			AshEngine::UIContext& refUi,
			const EditorGizmoInternal::ViewportContext& refViewportContext,
			const EditorGizmoInternal::DragSession& refDragSession,
			const AshEngine::UIColor& refAxisColor)
		{
			if (!refDragSession.bActive || refDragSession.eMode != GizmoMode::Rotate)
			{
				return;
			}

			glm::vec2 vecCenterScreen{ 0.0f };
			float fCenterDepth = 0.0f;
			if (!TryProjectWorldToViewport(
				refViewportContext,
				refDragSession.vecGizmoOrigin,
				vecCenterScreen,
				fCenterDepth))
			{
				return;
			}

			const float fDisplayDeltaDegrees =
				ClampRotateWedgeDisplayDegrees(refDragSession.fCurrentRotateDeltaDegrees);
			if (std::abs(fDisplayDeltaDegrees) <= 0.001f)
			{
				return;
			}

			glm::vec2 vecPreviousScreen{ 0.0f };
			if (!TryProjectRotateDirection(refViewportContext, refDragSession, 0.0f, vecPreviousScreen))
			{
				return;
			}

			const AshEngine::UIColor colorFill = MakeWedgeFillColor(refAxisColor);
			const AshEngine::UIColor colorBoundary = MakeWedgeBoundaryColor(refAxisColor);
			const int32_t iStepCount = std::max(
				2,
				static_cast<int32_t>(
					std::ceil(std::abs(fDisplayDeltaDegrees) / kRotateActiveWedgeStepDegrees)));
			for (int32_t iStepIndex = 1; iStepIndex <= iStepCount; ++iStepIndex)
			{
				const float fStepDeltaDegrees =
					fDisplayDeltaDegrees *
					(static_cast<float>(iStepIndex) / static_cast<float>(iStepCount));
				glm::vec2 vecCurrentScreen{ 0.0f };
				if (!TryProjectRotateDirection(
					refViewportContext,
					refDragSession,
					fStepDeltaDegrees,
					vecCurrentScreen))
				{
					continue;
				}

				refUi.draw_window_line(
					{ vecCenterScreen.x, vecCenterScreen.y },
					{ vecCurrentScreen.x, vecCurrentScreen.y },
					colorFill,
					kRotateActiveWedgeFillThickness);
				refUi.draw_window_line(
					{ vecPreviousScreen.x, vecPreviousScreen.y },
					{ vecCurrentScreen.x, vecCurrentScreen.y },
					{ refAxisColor.r, refAxisColor.g, refAxisColor.b, 0.42f },
					2.2f);
				vecPreviousScreen = vecCurrentScreen;
			}

			glm::vec2 vecStartScreen{ 0.0f };
			glm::vec2 vecCurrentScreen{ 0.0f };
			glm::vec2 vecLabelAnchorScreen{ 0.0f };
			if (!TryProjectRotateDirection(refViewportContext, refDragSession, 0.0f, vecStartScreen) ||
				!TryProjectRotateDirection(refViewportContext, refDragSession, fDisplayDeltaDegrees, vecCurrentScreen) ||
				!TryProjectRotateDirection(refViewportContext, refDragSession, fDisplayDeltaDegrees * 0.5f, vecLabelAnchorScreen))
			{
				return;
			}

			refUi.draw_window_line(
				{ vecCenterScreen.x, vecCenterScreen.y },
				{ vecStartScreen.x, vecStartScreen.y },
				{ 0.98f, 0.98f, 0.92f, 0.88f },
				kRotateActiveWedgeBoundaryThickness);
			refUi.draw_window_line(
				{ vecCenterScreen.x, vecCenterScreen.y },
				{ vecCurrentScreen.x, vecCurrentScreen.y },
				colorBoundary,
				kRotateActiveWedgeBoundaryThickness + 0.65f);
			DrawRotateWedgeArrow(
				refUi,
				vecCenterScreen,
				vecCurrentScreen,
				refDragSession.fCurrentRotateDeltaDegrees,
				colorBoundary);
			DrawRotateWedgeLabel(
				refUi,
				vecCenterScreen,
				vecLabelAnchorScreen,
				refDragSession.fCurrentRotateDeltaDegrees,
				colorBoundary);
		}
	}

	bool RotateGizmoTool::TryBuildVisual(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const EditorGizmoInternal::GizmoBasis& refBasis,
		EditorGizmoInternal::RotateGizmoVisual& outVisual)
	{
		outVisual = {};
		const glm::vec3 vecCameraHint = NormalizeOrFallback(
			refViewportContext.vecCameraPosition - refBasis.vecOrigin,
			glm::vec3(0.0f, 0.0f, 1.0f));
		for (size_t uAxisIndex = 0; uAxisIndex < refBasis.vecAxes.size(); ++uAxisIndex)
		{
			RotateRingVisual& refRing = outVisual.rings[uAxisIndex];
			const glm::vec3 vecAxisDirection = NormalizeOrFallback(
				refBasis.vecAxes[uAxisIndex],
				glm::vec3(0.0f, 1.0f, 0.0f));
			glm::vec3 vecRingTangent{ 0.0f };
			glm::vec3 vecRingBitangent{ 0.0f };
			if (!TryBuildPerpendicularBasis(
				vecAxisDirection,
				vecCameraHint,
				vecRingTangent,
				vecRingBitangent))
			{
				continue;
			}

			BuildProjectedRotateRing(
				refViewportContext,
				refBasis.vecOrigin,
				vecRingTangent,
				vecRingBitangent,
				ComputeAxisWorldLength(refViewportContext, refBasis.vecOrigin) * kRotateGizmoRadiusScale,
				refRing);
		}

		const glm::vec3 vecCameraForward = ComputeCameraForward(refViewportContext);
		glm::vec3 vecViewRingTangent{ 0.0f };
		glm::vec3 vecViewRingBitangent{ 0.0f };
		if (TryBuildPerpendicularBasis(
			vecCameraForward,
			glm::vec3(0.0f, 1.0f, 0.0f),
			vecViewRingTangent,
			vecViewRingBitangent))
		{
			BuildProjectedRotateRing(
				refViewportContext,
				refBasis.vecOrigin,
				vecViewRingTangent,
				vecViewRingBitangent,
				ComputeAxisWorldLength(refViewportContext, refBasis.vecOrigin) * kRotateViewRingRadiusScale,
				outVisual.viewRing);
		}

		for (const RotateRingVisual& refRing : outVisual.rings)
		{
			if (refRing.bAnySegmentVisible)
			{
				return true;
			}
		}
		return outVisual.viewRing.bAnySegmentVisible;
	}

	EditorGizmoInternal::HandleHit RotateGizmoTool::HitTestHandle(
		const EditorGizmoInternal::RotateGizmoVisual& refVisual,
		const glm::vec2& vecMousePosition)
	{
		HandleHit result{};
		float fBestDistance = kRotateGizmoHoverThresholdPixels;
		for (size_t uAxisIndex = 0; uAxisIndex < refVisual.rings.size(); ++uAxisIndex)
		{
			const RotateRingVisual& refRing = refVisual.rings[uAxisIndex];
			if (!refRing.bAnySegmentVisible)
			{
				continue;
			}

			for (size_t uSampleIndex = 0; uSampleIndex + 1 < refRing.vecScreenPoints.size(); ++uSampleIndex)
			{
				if (!refRing.bPointVisible[uSampleIndex] || !refRing.bPointVisible[uSampleIndex + 1])
				{
					continue;
				}

				const float fDistance = DistancePointToSegment(
					vecMousePosition,
					refRing.vecScreenPoints[uSampleIndex],
					refRing.vecScreenPoints[uSampleIndex + 1]);
				if (fDistance <= fBestDistance)
				{
					fBestDistance = fDistance;
					result.eKind = HandleKind::Axis;
					result.iPrimaryAxis = static_cast<int32_t>(uAxisIndex);
					result.iSecondaryAxis = -1;
				}
			}
		}

		if (refVisual.viewRing.bAnySegmentVisible)
		{
			for (size_t uSampleIndex = 0; uSampleIndex + 1 < refVisual.viewRing.vecScreenPoints.size(); ++uSampleIndex)
			{
				if (!refVisual.viewRing.bPointVisible[uSampleIndex] || !refVisual.viewRing.bPointVisible[uSampleIndex + 1])
				{
					continue;
				}

				const float fDistance = DistancePointToSegment(
					vecMousePosition,
					refVisual.viewRing.vecScreenPoints[uSampleIndex],
					refVisual.viewRing.vecScreenPoints[uSampleIndex + 1]);
				if (fDistance <= fBestDistance)
				{
					fBestDistance = fDistance;
					result.eKind = HandleKind::Screen;
					result.iPrimaryAxis = -1;
					result.iSecondaryAxis = -1;
				}
			}
		}

		return result;
	}

	void RotateGizmoTool::Draw(
		AshEngine::UIContext& refUi,
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const EditorGizmoInternal::RotateGizmoVisual& refVisual,
		const EditorGizmoInternal::HandleHit& refHoveredHandle,
		const EditorGizmoInternal::DragSession& refDragSession)
	{
		refUi.push_window_clip_rect(refViewportContext.rectContent);
		if (refVisual.viewRing.bAnySegmentVisible)
		{
			const bool bViewActive = refDragSession.bActive && refDragSession.eHandleKind == HandleKind::Screen;
			const bool bViewHovered = !bViewActive && refHoveredHandle.eKind == HandleKind::Screen;
			const AshEngine::UIColor viewRingColor =
				bViewActive
				? AshEngine::UIColor{ 0.98f, 0.99f, 1.0f, 0.92f }
				: (bViewHovered
					? AshEngine::UIColor{ 0.96f, 0.98f, 1.0f, 0.72f }
					: kRotateViewRingColor);
			const float fViewRingThickness =
				bViewActive
				? (kRotateViewRingLineThickness + 1.4f)
				: (bViewHovered ? (kRotateViewRingLineThickness + 0.8f) : kRotateViewRingLineThickness);
			if (bViewActive)
			{
				DrawActiveRotateAngleOverlay(refUi, refViewportContext, refDragSession, viewRingColor);
			}
			DrawRotateRingSegments(refUi, refVisual.viewRing, viewRingColor, fViewRingThickness, false);

			static constexpr std::array<size_t, 4> kViewMarkerSamples{
				0,
				kRotateRingSampleCount / 4,
				kRotateRingSampleCount / 2,
				(kRotateRingSampleCount * 3) / 4
			};
			for (const size_t uMarkerSample : kViewMarkerSamples)
			{
				size_t uVisibleIndex = 0;
				if (!TryFindVisibleRotateRingSample(refVisual.viewRing, uMarkerSample, true, uVisibleIndex))
				{
					continue;
				}

				const glm::vec2 vecMarkerPosition = refVisual.viewRing.vecScreenPoints[uVisibleIndex];
				const float fMarkerHalfExtent =
					kRotateRingMarkerHalfExtent +
					(bViewActive ? 1.5f : (bViewHovered ? 0.75f : 0.0f));
				const AshEngine::UIRect rectMarker{
					vecMarkerPosition.x - fMarkerHalfExtent,
					vecMarkerPosition.y - fMarkerHalfExtent,
					fMarkerHalfExtent * 2.0f,
					fMarkerHalfExtent * 2.0f
				};
				refUi.draw_window_rect_filled(
					rectMarker,
					bViewActive
						? AshEngine::UIColor{ 0.98f, 0.99f, 1.0f, 0.92f }
						: AshEngine::UIColor{ 0.94f, 0.96f, 0.99f, bViewHovered ? 0.78f : 0.56f },
					2.0f);
				refUi.draw_window_rect(
					rectMarker,
					{ 0.08f, 0.10f, 0.14f, 0.92f },
					2.0f,
					kRotateRingMarkerOutlineThickness);
			}

			DrawRotateRingSegments(refUi, refVisual.viewRing, viewRingColor, fViewRingThickness, true);
		}

		struct AxisDrawState
		{
			const RotateRingVisual* pRing = nullptr;
			AshEngine::UIColor color{};
			float fThickness = 0.0f;
			bool bIsActive = false;
			bool bIsHovered = false;
			size_t uAxisIndex = 0;
		};
		std::array<AxisDrawState, 3> axisStates{};
		for (size_t uAxisIndex = 0; uAxisIndex < refVisual.rings.size(); ++uAxisIndex)
		{
			const RotateRingVisual& refRing = refVisual.rings[uAxisIndex];
			if (!refRing.bAnySegmentVisible)
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
				? kRotateGizmoActiveLineThickness
				: (bIsHovered ? kRotateGizmoHoverLineThickness : kRotateGizmoLineThickness);
			if (bIsActive)
			{
				DrawActiveRotateAngleOverlay(refUi, refViewportContext, refDragSession, color);
			}
			axisStates[uAxisIndex] = { &refRing, color, fThickness, bIsActive, bIsHovered, uAxisIndex };
		}

		for (const AxisDrawState& refAxisState : axisStates)
		{
			if (refAxisState.pRing == nullptr)
			{
				continue;
			}
			DrawRotateRingSegments(
				refUi,
				*refAxisState.pRing,
				refAxisState.color,
				refAxisState.fThickness,
				false);
		}

		for (const AxisDrawState& refAxisState : axisStates)
		{
			if (refAxisState.pRing == nullptr)
			{
				continue;
			}

			const RotateRingVisual& refRing = *refAxisState.pRing;

			static constexpr std::array<size_t, 4> kAxisMarkerSamples{
				0,
				kRotateRingSampleCount / 4,
				kRotateRingSampleCount / 2,
				(kRotateRingSampleCount * 3) / 4
			};
			for (const size_t uMarkerSample : kAxisMarkerSamples)
			{
				size_t uVisibleIndex = 0;
				if (!TryFindVisibleRotateRingSample(refRing, uMarkerSample, true, uVisibleIndex))
				{
					continue;
				}

				const glm::vec2 vecMarkerPosition = refRing.vecScreenPoints[uVisibleIndex];
				const float fMarkerHalfExtent =
					kRotateRingMarkerHalfExtent +
					(refAxisState.bIsActive ? 1.5f : (refAxisState.bIsHovered ? 0.75f : 0.0f));
				const AshEngine::UIRect rectMarker{
					vecMarkerPosition.x - fMarkerHalfExtent,
					vecMarkerPosition.y - fMarkerHalfExtent,
					fMarkerHalfExtent * 2.0f,
					fMarkerHalfExtent * 2.0f
				};
				refUi.draw_window_rect_filled(rectMarker, refAxisState.color, 2.0f);
				refUi.draw_window_rect(
					rectMarker,
					{ 0.08f, 0.10f, 0.14f, 0.92f },
					2.0f,
					kRotateRingMarkerOutlineThickness);
			}

			DrawRotateRingSegments(refUi, refRing, refAxisState.color, refAxisState.fThickness, true);

			size_t uLabelIndex = 0;
			if (TryFindVisibleRotateRingSample(
				refRing,
				std::min<size_t>(kRotateLabelSampleIndex, refRing.vecScreenPoints.size() - 1),
				true,
				uLabelIndex))
			{
				const AshEngine::UIVec2 vecTextSize = refUi.calc_text_size(kAxisLabels[refAxisState.uAxisIndex]);
				const glm::vec2 vecLabelPosition{
					refRing.vecScreenPoints[uLabelIndex].x + 10.0f,
					refRing.vecScreenPoints[uLabelIndex].y - (vecTextSize.y * 0.5f) - 3.0f
				};
				const AshEngine::UIRect rectLabel{
					vecLabelPosition.x - 5.0f,
					vecLabelPosition.y - 2.0f,
					vecTextSize.x + 10.0f,
					vecTextSize.y + 6.0f
				};
				refUi.draw_window_rect_filled(
					rectLabel,
					{ 0.05f, 0.06f, 0.09f, refAxisState.bIsActive ? 0.92f : 0.82f },
					3.0f);
				refUi.draw_window_rect(
					rectLabel,
					refAxisState.color,
					3.0f,
					refAxisState.bIsActive ? 1.75f : 1.25f);
				refUi.draw_window_text(
					{ vecLabelPosition.x, vecLabelPosition.y + 1.0f },
					refAxisState.color,
					kAxisLabels[refAxisState.uAxisIndex]);
			}
		}
		refUi.pop_window_clip_rect();
	}

	bool RotateGizmoTool::TryBuildDragSession(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const SceneService& refSceneService,
		const SelectionService& refSelectionService,
		const EditorGizmoInternal::GizmoBasis& refBasis,
		const EditorGizmoInternal::RotateGizmoVisual& refVisual,
		const EditorGizmoInternal::HandleHit& refHoveredHandle,
		const EditorGizmoState& refGizmoState,
		const glm::vec2& vecMousePosition,
		EditorGizmoInternal::DragSession& outDragSession) const
	{
		outDragSession = {};
		if (refGizmoState.eMode != GizmoMode::Rotate)
		{
			return false;
		}

		const AshEngine::Entity entity = refSceneService.FindEntity(refBasis.uEntityId);
		if (!entity.is_valid())
		{
			return false;
		}

		glm::vec3 vecPlaneNormal{ 0.0f };
		if (refHoveredHandle.eKind == HandleKind::Screen)
		{
			vecPlaneNormal = ComputeCameraForward(refViewportContext);
		}
		else if (refHoveredHandle.eKind == HandleKind::Axis &&
			IsAxisIndexValid(refHoveredHandle.iPrimaryAxis))
		{
			vecPlaneNormal = NormalizeOrFallback(
				refBasis.vecAxes[static_cast<size_t>(refHoveredHandle.iPrimaryAxis)],
				glm::vec3(0.0f, 1.0f, 0.0f));
		}
		else
		{
			return false;
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
			refHoveredHandle.eKind == HandleKind::Screen
			? ComputeCameraForward(refViewportContext)
			: NormalizeOrFallback(
				refBasis.vecAxes[static_cast<size_t>(refHoveredHandle.iPrimaryAxis)],
				glm::vec3(1.0f, 0.0f, 0.0f));
		outDragSession.vecGizmoOrigin = refBasis.vecOrigin;
		outDragSession.vecDragPlaneNormal = vecPlaneNormal;
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
			refHoveredHandle.eKind == HandleKind::Screen
			? std::max(refVisual.viewRing.fWorldRadius, 0.001f)
			: std::max(refVisual.rings[static_cast<size_t>(refHoveredHandle.iPrimaryAxis)].fWorldRadius, 0.001f);
		outDragSession.bActive = true;
		return true;
	}

	void RotateGizmoTool::BeginDragSession(
		const EditorGizmoInternal::DragSession& refDragSession,
		const bool bTransactionOpened)
	{
		_dragSession = refDragSession;
		_dragSession.bTransactionOpened = bTransactionOpened;
		_dragSession.bActive = true;
	}

	bool RotateGizmoTool::TryUpdateDrag(
		const EditorGizmoInternal::ViewportContext& refViewportContext,
		const SceneService& refSceneService,
		const EditorGizmoState& refGizmoState,
		const glm::vec2& vecMousePosition,
		EditorGizmoInternal::GizmoDragUpdate& outUpdate)
	{
		outUpdate = {};
		if (!_dragSession.bActive || _dragSession.eMode != GizmoMode::Rotate)
		{
			return false;
		}

		const AshEngine::SceneRay ray = BuildViewportRay(refViewportContext, vecMousePosition);
		glm::vec3 vecHitPoint{ 0.0f };
		if (!EditorGizmoMath::TryIntersectRayPlane(
			ray.origin,
			ray.direction,
			_dragSession.vecGizmoOrigin,
			_dragSession.vecDragPlaneNormal,
			vecHitPoint))
		{
			return false;
		}

		const glm::vec3 vecCurrentPlaneDirection = NormalizeOrFallback(
			vecHitPoint - _dragSession.vecGizmoOrigin,
			_dragSession.vecStartPlaneDirection);
		float fRotateDeltaDegrees = glm::degrees(EditorGizmoMath::ComputeSignedAngleAroundAxis(
			_dragSession.vecStartPlaneDirection,
			vecCurrentPlaneDirection,
			_dragSession.vecAxisDirection));
		fRotateDeltaDegrees = SnapRotateDeltaDegrees(fRotateDeltaDegrees, refGizmoState);
		_dragSession.fCurrentRotateDeltaDegrees = fRotateDeltaDegrees;

		outUpdate.fRotateDeltaDegrees = fRotateDeltaDegrees;
		outUpdate.bHasRotateDelta = true;
		outUpdate.afterTransform = ComputeRotatedTransform(
			refSceneService,
			_dragSession.uEntityId,
			_dragSession.beforeTransform,
			_dragSession.vecAxisDirection,
			fRotateDeltaDegrees);
		outUpdate.bHasTransform = true;
		return true;
	}

	void RotateGizmoTool::SetHoveredHandle(const EditorGizmoInternal::HandleHit& refHoveredHandle)
	{
		_hoveredHandle = refHoveredHandle;
	}

	void RotateGizmoTool::ClearHoveredHandle()
	{
		_hoveredHandle = {};
	}

	const EditorGizmoInternal::HandleHit& RotateGizmoTool::GetHoveredHandle() const
	{
		return _hoveredHandle;
	}

	const EditorGizmoInternal::DragSession& RotateGizmoTool::GetDragSession() const
	{
		return _dragSession;
	}

	void RotateGizmoTool::ResetInteraction()
	{
		_dragSession = {};
		_hoveredHandle = {};
	}

	void RotateGizmoTool::CancelInteraction(IEditorCommandExecutor& refCommandExecutor)
	{
		if (_dragSession.bTransactionOpened)
		{
			refCommandExecutor.CancelCommandTransaction();
		}
		ResetInteraction();
	}
}

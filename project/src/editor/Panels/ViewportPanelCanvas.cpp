#include "Panels/ViewportPanelCanvas.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/ViewportPanelInteraction.h"
#include "Panels/ViewportPanelSceneSupportInternal.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/EditorGizmoMath.h"
#include "Services/EditorViewportService.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/ViewportAxisIndicator.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kSceneSurfaceUnavailableMessage = "Scene surface is not available yet.";
		constexpr const char* kViewportWaitingForUiMessage = "Viewport preview is waiting for the engine UIContext.";

		void DrawViewportStatusMessage(AshEngine::UIContext& refUi, const char* pMessage)
		{
			refUi.text_wrapped(pMessage);
		}

		bool IsViewportContentHovered(AshEngine::UIContext& refUi, const AshEngine::UIRect& rectContent)
		{
			const AshEngine::UIVec2 vecMousePosition = refUi.get_mouse_pos();
			return
				ViewportPanelSupport::IsPointInRect(rectContent, vecMousePosition) &&
				(refUi.is_window_hovered_with_children() || refUi.is_window_focused_with_children());
		}

		bool ShouldDrawAxisIndicator(const EditorViewportPresentation* pPresentation)
		{
			return !pPresentation || pPresentation->eKind != EditorViewportKind::Game;
		}

		bool TryBuildAxisIndicatorParams(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& refRectContent,
			ViewportAxisIndicatorParams& outParams)
		{
			outParams = {};
			ViewportPanelSupport::Detail::SceneViewportProjectionContext projectionContext{};
			if (!ViewportPanelSupport::Detail::TryBuildSceneViewportProjectionContext(
				refDeps,
				strViewportId,
				refRectContent,
				projectionContext))
			{
				return false;
			}

			glm::vec3 vecRight{};
			glm::vec3 vecUp{};
			glm::vec3 vecForward{};
			EditorGizmoMath::ExtractViewBasis(
				projectionContext.matView,
				vecRight,
				vecUp,
				vecForward);
			outParams.viewRightX = vecRight.x;
			outParams.viewRightY = vecRight.y;
			outParams.viewRightZ = vecRight.z;
			outParams.viewUpX = vecUp.x;
			outParams.viewUpY = vecUp.y;
			outParams.viewUpZ = vecUp.z;
			outParams.viewForwardX = vecForward.x;
			outParams.viewForwardY = vecForward.y;
			outParams.viewForwardZ = vecForward.z;
			return true;
		}

		void DrawViewportOverlay(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport)
		{
			if (!refFrameContext.pUiContext || !refDeps.pViewportService)
			{
				return;
			}

			const EditorViewportPresentation* pPresentation = refDeps.pViewportService->GetPresentation(strViewportId);
			const EditorViewportRenderState* pRenderState = refDeps.pViewportService->GetRenderState(strViewportId);
			if (!pPresentation || (!pPresentation->bShowOverlays && !pPresentation->bShowStats))
			{
				return;
			}

			AshEngine::SceneViewStats sceneStats{};
			const AshEngine::SceneViewStats* pSceneStats = nullptr;
			if (refDeps.pViewportService->TryGetSceneViewStats(strViewportId, sceneStats))
			{
				pSceneStats = &sceneStats;
			}

			const std::vector<std::string> vecLines = ViewportPanelSupport::MakeOverlayLines(
				refViewport,
				*pPresentation,
				pRenderState,
				pSceneStats,
				refDeps.pViewportService->IsPrimaryViewport(refViewport.strId));
			if (vecLines.empty())
			{
				return;
			}

			const AshEngine::UIRect rectItem = refFrameContext.pUiContext->get_item_rect();
			const float fPadding = 8.0f;
			const float fLineSpacing = 2.0f;
			float fMaxWidth = 0.0f;
			float fTotalHeight = fPadding * 2.0f;
			for (const std::string& strLine : vecLines)
			{
				const AshEngine::UIVec2 vecLineSize = refFrameContext.pUiContext->calc_text_size(strLine.c_str());
				fMaxWidth = std::max(fMaxWidth, vecLineSize.x);
				fTotalHeight += vecLineSize.y;
			}
			fTotalHeight += std::max(0.0f, static_cast<float>(vecLines.size() - 1)) * fLineSpacing;

			const float fMargin = 10.0f;
			const float fOverlayWidth = fMaxWidth + fPadding * 2.0f;
			float fOverlayX = rectItem.x + fMargin;
			float fOverlayY = rectItem.y + fMargin;
			if (fOverlayX + fOverlayWidth > rectItem.x + rectItem.width - fMargin)
			{
				fOverlayX = rectItem.x + rectItem.width - fOverlayWidth - fMargin;
			}
			if (fOverlayX < rectItem.x + fMargin)
			{
				fOverlayX = rectItem.x + fMargin;
			}
			if (fOverlayY + fTotalHeight > rectItem.y + rectItem.height - fMargin)
			{
				fOverlayY = rectItem.y + rectItem.height - fTotalHeight - fMargin;
			}
			if (fOverlayY < rectItem.y + fMargin)
			{
				fOverlayY = rectItem.y + fMargin;
			}

			const AshEngine::UIRect rectOverlay{ fOverlayX, fOverlayY, fOverlayWidth, fTotalHeight };
			refFrameContext.pUiContext->draw_window_rect_filled(
				rectOverlay,
				GetEditorOverlayBackgroundColor(*refFrameContext.pUiContext),
				8.0f);
			refFrameContext.pUiContext->draw_window_rect(
				rectOverlay,
				GetEditorOverlayBorderColor(*refFrameContext.pUiContext),
				8.0f);

			float fTextY = rectOverlay.y + fPadding;
			for (size_t uIndex = 0; uIndex < vecLines.size(); ++uIndex)
			{
				const AshEngine::UIColor colorText =
					uIndex == 0
					? GetEditorHeadingTextColor(*refFrameContext.pUiContext)
					: GetEditorMutedTextColor(*refFrameContext.pUiContext);
				refFrameContext.pUiContext->draw_window_text(
					{ rectOverlay.x + fPadding, fTextY },
					colorText,
					vecLines[uIndex].c_str());
				fTextY += refFrameContext.pUiContext->calc_text_size(vecLines[uIndex].c_str()).y + fLineSpacing;
			}
		}
	}

	namespace ViewportPanelCanvas
	{
		ViewportPanelCanvasDrawResult Draw(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			EditorViewportInstance& refViewport)
		{
			ViewportPanelCanvasDrawResult drawResult{};
			if (!refFrameContext.pUiContext)
			{
				return drawResult;
			}

			AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
			const AshEngine::UIVec2 vecAvailableSize = refUi.get_content_region_avail();
			if (refDeps.pViewportService)
			{
				refDeps.pViewportService->UpdateRequestedSize(
					strViewportId,
					vecAvailableSize.x > 1.0f ? static_cast<uint32_t>(vecAvailableSize.x) : 0u,
					vecAvailableSize.y > 1.0f ? static_cast<uint32_t>(vecAvailableSize.y) : 0u,
					refUi.get_time_seconds());
			}

			if (!refViewport.surface.is_valid())
			{
				DrawViewportStatusMessage(refUi, kSceneSurfaceUnavailableMessage);
				return drawResult;
			}

			if (vecAvailableSize.x <= 2.0f || vecAvailableSize.y <= 2.0f)
			{
				DrawViewportStatusMessage(refUi, kViewportWaitingForUiMessage);
				return drawResult;
			}

			bool bPreserveAspect = false;
			if (const EditorViewportPresentation* pPresentation =
				refDeps.pViewportService ? refDeps.pViewportService->GetPresentation(strViewportId) : nullptr)
			{
				bPreserveAspect = pPresentation->bPreserveAspect;
			}

			const bool bOutputSizePending =
				refViewport.state.uRequestedWidth > 0u &&
				refViewport.state.uRequestedHeight > 0u &&
				(refViewport.state.uWidth != refViewport.state.uRequestedWidth ||
				 refViewport.state.uHeight != refViewport.state.uRequestedHeight);
			if (refViewport.state.uWidth == 0u || refViewport.state.uHeight == 0u)
			{
				DrawViewportStatusMessage(refUi, kSceneSurfaceUnavailableMessage);
				return drawResult;
			}

			if (bOutputSizePending)
			{
				refUi.dummy(vecAvailableSize);
				const AshEngine::UIRect rectContent = refUi.get_item_rect();
				refUi.draw_window_rect_filled(
					rectContent,
					GetEditorOverlayBackgroundColor(refUi),
					0.0f);
				refUi.draw_window_text(
					{ rectContent.x + 14.0f, rectContent.y + 12.0f },
					GetEditorMutedTextColor(refUi),
					"Updating viewport...");
				return drawResult;
			}

			refFrameContext.pUiContext->draw_surface_fill_available(refViewport.surface, bPreserveAspect);
			drawResult.rectContent = refFrameContext.pUiContext->get_item_rect();
			drawResult.bContentHovered = IsViewportContentHovered(refUi, drawResult.rectContent);
			drawResult.bHasViewportContent = true;

			refViewport.state.bContentHovered = drawResult.bContentHovered;
			refViewport.state.rectContent = drawResult.rectContent;
			return drawResult;
		}

		void DrawDecorations(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport,
			const ViewportPanelCanvasDrawResult& refDrawResult,
			const ViewportPanelSceneSelectionState& refSceneSelectionState)
		{
			if (!refFrameContext.pUiContext || !refDrawResult.bHasViewportContent)
			{
				return;
			}

			AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
			const AshEngine::UIRect& rectContent = refDrawResult.rectContent;
			const EditorViewportPresentation* pPresentation =
				refDeps.pViewportService ? refDeps.pViewportService->GetPresentation(strViewportId) : nullptr;

			DrawViewportOverlay(refFrameContext, refDeps, strViewportId, refViewport);

			if (pPresentation && pPresentation->eKind == EditorViewportKind::Scene)
			{
				ViewportPanelSupport::UpdateSceneViewportOverlayHelpers(
					refDeps,
					*pPresentation,
					strViewportId,
					rectContent);

				ViewportPanelSupport::DrawSceneGizmoOverlay(
					refDeps,
					refUi,
					pPresentation,
					strViewportId,
					rectContent);
				ViewportPanelInteraction::DrawSceneBoxSelectionOverlay(
					refUi,
					rectContent,
					refSceneSelectionState);
			}

			if (ShouldDrawAxisIndicator(pPresentation))
			{
				ViewportAxisIndicatorParams axisIndicatorParams{};
				if (TryBuildAxisIndicatorParams(
					refDeps,
					strViewportId,
					rectContent,
					axisIndicatorParams))
				{
					DrawViewportAxisIndicator(
						refUi,
						rectContent.x, rectContent.y,
						rectContent.width, rectContent.height,
						axisIndicatorParams);
				}
			}
			if (strViewportId == EditorViewportIds::Scene &&
				(refViewport.state.bFocused || refDrawResult.bContentHovered))
			{
				ViewportPanelSupport::DrawOperationHints(
					refUi,
					rectContent.x, rectContent.y,
					rectContent.width, rectContent.height);
			}
		}
	}
}

#include "Panels/ViewportPanelCanvas.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/EditorViewportService.h"
#include "Widgets/ViewportAxisIndicator.h"

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

			const std::vector<std::string> vecLines = ViewportPanelSupport::MakeOverlayLines(
				refViewport,
				*pPresentation,
				pRenderState,
				refDeps.pViewportService->IsPrimaryViewport(refViewport.strId));
			if (vecLines.empty())
			{
				return;
			}

			const AshEngine::UIRect rectItem = refFrameContext.pUiContext->get_item_rect();
			const float fPadding = 10.0f;
			const float fLineSpacing = 4.0f;
			float fMaxWidth = 0.0f;
			float fTotalHeight = fPadding * 2.0f;
			for (const std::string& strLine : vecLines)
			{
				const AshEngine::UIVec2 vecLineSize = refFrameContext.pUiContext->calc_text_size(strLine.c_str());
				fMaxWidth = std::max(fMaxWidth, vecLineSize.x);
				fTotalHeight += vecLineSize.y;
			}
			fTotalHeight += std::max(0.0f, static_cast<float>(vecLines.size() - 1)) * fLineSpacing;

			const float fMargin = 12.0f;
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
				{ 0.12f, 0.14f, 0.18f, 0.72f },
				6.0f);
			refFrameContext.pUiContext->draw_window_rect(
				rectOverlay,
				{ 0.64f, 0.72f, 0.84f, 0.32f },
				6.0f);

			float fTextY = rectOverlay.y + fPadding;
			const AshEngine::UIColor colorText =
				refFrameContext.pUiContext->get_style_color(AshEngine::UIStyleColorKind::Text);
			for (const std::string& strLine : vecLines)
			{
				refFrameContext.pUiContext->draw_window_text(
					{ rectOverlay.x + fPadding, fTextY },
					colorText,
					strLine.c_str());
				fTextY += refFrameContext.pUiContext->calc_text_size(strLine.c_str()).y + fLineSpacing;
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
				refUi.draw_window_rect_filled(rectContent, { 0.05f, 0.06f, 0.08f, 1.0f }, 0.0f);
				refUi.draw_window_text(
					{ rectContent.x + 14.0f, rectContent.y + 12.0f },
					{ 0.72f, 0.76f, 0.82f, 0.92f },
					"Updating viewport...");
				return drawResult;
			}

			refFrameContext.pUiContext->draw_surface_fill_available(refViewport.surface, bPreserveAspect);
			drawResult.rectContent = refFrameContext.pUiContext->get_item_rect();
			drawResult.bContentHovered = refUi.is_item_hovered();
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
			const ViewportPanelSceneBoxSelectionState& refSceneBoxSelectionState)
		{
			if (!refFrameContext.pUiContext || !refDrawResult.bHasViewportContent)
			{
				return;
			}

			AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
			const AshEngine::UIRect& rectContent = refDrawResult.rectContent;

			DrawViewportOverlay(refFrameContext, refDeps, strViewportId, refViewport);

			if (const EditorViewportPresentation* pPresentation =
				refDeps.pViewportService ? refDeps.pViewportService->GetPresentation(strViewportId) : nullptr)
			{
				if (pPresentation->eKind == EditorViewportKind::Scene)
				{
					if (pPresentation->bShowOverlays &&
						ViewportPanelSupport::HasSceneViewportOverlayHelpersEnabled(*pPresentation))
					{
						ViewportPanelSupport::DrawSceneViewportOverlayHelpers(
							refDeps,
							refUi,
							*pPresentation,
							strViewportId,
							rectContent);
					}

					ViewportPanelSupport::DrawSceneGizmoOverlay(
						refDeps,
						refUi,
						pPresentation,
						rectContent);
					ViewportPanelInteraction::DrawSceneBoxSelectionOverlay(
						refUi,
						rectContent,
						refSceneBoxSelectionState);
				}
			}

			DrawViewportAxisIndicator(
				refUi,
				rectContent.x, rectContent.y,
				rectContent.width, rectContent.height);
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

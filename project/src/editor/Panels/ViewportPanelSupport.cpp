#include "Panels/ViewportPanelSupport.h"

#include "Base/input/Input.h"
#include "Function/Gui/UIContext.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorViewportCameraService.h"

#include <algorithm>
#include <string>
#include <vector>

namespace AshEditor
{
	namespace
	{
		void AppendInlineStatus(std::string& strLine, const char* pToken)
		{
			if (!pToken || *pToken == '\0')
			{
				return;
			}

			if (!strLine.empty())
			{
				strLine += "  ";
			}
			strLine += pToken;
		}

		std::string BuildViewportSizeLabel(uint32_t uWidth, uint32_t uHeight)
		{
			return std::to_string(uWidth) + " x " + std::to_string(uHeight);
		}

		bool IsSceneViewportPresentation(const EditorViewportPresentation& refPresentation)
		{
			return refPresentation.eKind == EditorViewportKind::Scene;
		}
	}

	namespace ViewportPanelSupport
	{
		bool IsPointInRect(const AshEngine::UIRect& refRect, const AshEngine::UIVec2& refPoint)
		{
			return
				refPoint.x >= refRect.x &&
				refPoint.y >= refRect.y &&
				refPoint.x <= (refRect.x + refRect.width) &&
				refPoint.y <= (refRect.y + refRect.height);
		}

		AshEngine::UIRect MakeRectFromPoints(
			const AshEngine::UIVec2& refFirstPoint,
			const AshEngine::UIVec2& refSecondPoint)
		{
			const float fMinX = std::min(refFirstPoint.x, refSecondPoint.x);
			const float fMinY = std::min(refFirstPoint.y, refSecondPoint.y);
			const float fMaxX = std::max(refFirstPoint.x, refSecondPoint.x);
			const float fMaxY = std::max(refFirstPoint.y, refSecondPoint.y);
			return { fMinX, fMinY, fMaxX - fMinX, fMaxY - fMinY };
		}

		bool RectsIntersect(const AshEngine::UIRect& refLeftRect, const AshEngine::UIRect& refRightRect)
		{
			return
				refLeftRect.x <= refRightRect.x + refRightRect.width &&
				refLeftRect.x + refLeftRect.width >= refRightRect.x &&
				refLeftRect.y <= refRightRect.y + refRightRect.height &&
				refLeftRect.y + refLeftRect.height >= refRightRect.y;
		}

		float DistanceSquared(const AshEngine::UIVec2& refFirstPoint, const AshEngine::UIVec2& refSecondPoint)
		{
			const float fDeltaX = refFirstPoint.x - refSecondPoint.x;
			const float fDeltaY = refFirstPoint.y - refSecondPoint.y;
			return fDeltaX * fDeltaX + fDeltaY * fDeltaY;
		}

		AshEngine::UIVec2 GetMouseScreenPosition(const AshEngine::InputState& refInput)
		{
			return {
				static_cast<float>(refInput.get_mouse_x()),
				static_cast<float>(refInput.get_mouse_y())
			};
		}

		void DrawViewportDisplayOptionsMenu(
			AshEngine::UIContext& refUi,
			EditorViewportPresentation& refPresentation)
		{
			if (!refUi.begin_menu("Display"))
			{
				return;
			}

			refUi.menu_item("Show Overlay", nullptr, &refPresentation.bShowOverlays, true);
			refUi.menu_item("Show Stats", nullptr, &refPresentation.bShowStats, true);
			refUi.menu_item("Preserve Aspect", nullptr, &refPresentation.bPreserveAspect, true);
			refUi.end_menu();
		}

		void DrawSceneViewportHelperOptionsMenu(
			AshEngine::UIContext& refUi,
			EditorViewportPresentation& refPresentation)
		{
			if (!refUi.begin_menu("Helpers"))
			{
				return;
			}

			refUi.menu_item("Show Selection Helpers", nullptr, &refPresentation.bShowSelectionHelpers, true);
			refUi.menu_item("Show Selection Pivot", nullptr, &refPresentation.bShowSelectionPivot, true);
			refUi.separator();
			refUi.menu_item("Show Grid", nullptr, &refPresentation.bShowReferenceGrid, true);
			refUi.menu_item("Show World Origin", nullptr, &refPresentation.bShowReferenceOrigin, true);
			refUi.separator();
			refUi.menu_item("Show Camera Helpers", nullptr, &refPresentation.bShowCameraHelpers, true);
			refUi.menu_item("Show Light Helpers", nullptr, &refPresentation.bShowLightHelpers, true);
			refUi.end_menu();
		}

		void DrawViewportInteractionOptionsMenu(
			const ViewportPanelDeps& refDeps,
			AshEngine::UIContext& refUi,
			const std::string& strViewportId,
			EditorViewportPresentation& refPresentation)
		{
			if (!refUi.begin_menu("Interaction"))
			{
				return;
			}

			refUi.menu_item("Accept Input", nullptr, &refPresentation.bAcceptsInput, true);
			if (IsSceneViewportPresentation(refPresentation) && refDeps.pViewportCameraService)
			{
				refUi.separator();
				float fMoveSpeed = refDeps.pViewportCameraService->GetMoveSpeed(strViewportId);
				refUi.set_next_item_width(140.0f);
				if (refUi.drag_float(
					"Camera Speed",
					fMoveSpeed,
					0.1f,
					EditorViewportCameraService::kMinMoveSpeed,
					EditorViewportCameraService::kMaxMoveSpeed,
					"%.2f"))
				{
					refDeps.pViewportCameraService->SetMoveSpeed(strViewportId, fMoveSpeed);
					if (refDeps.pSettingsService)
					{
						refDeps.pSettingsService->GetSettings().fSceneViewportCameraSpeed = fMoveSpeed;
					}
				}
			}
			refUi.end_menu();
		}

		std::vector<std::string> MakeOverlayLines(
			const EditorViewportInstance& refViewport,
			const EditorViewportPresentation& refPresentation,
			const EditorViewportRenderState* pRenderState,
			bool bIsPrimary)
		{
			std::vector<std::string> vecLines{};

			if (refPresentation.bShowOverlays)
			{
				std::string strSummary = BuildViewportSizeLabel(
					refViewport.state.uWidth,
					refViewport.state.uHeight);
				if (bIsPrimary)
				{
					AppendInlineStatus(strSummary, "Primary");
				}
				if (refPresentation.bAcceptsInput)
				{
					AppendInlineStatus(strSummary, "Input");
				}
				if (refPresentation.bPreserveAspect)
				{
					AppendInlineStatus(strSummary, "Aspect");
				}
				vecLines.push_back(std::move(strSummary));
			}

			if (refPresentation.bShowStats)
			{
				vecLines.push_back(
					"Requested " +
					BuildViewportSizeLabel(
						refViewport.state.uRequestedWidth,
						refViewport.state.uRequestedHeight));

				std::string strInteraction =
					"Focus " +
					std::string(refViewport.state.bFocused ? "yes" : "no") +
					"  Hover " +
					std::string(refViewport.state.bContentHovered ? "yes" : "no");
				if (pRenderState && pRenderState->bPendingSync)
				{
					AppendInlineStatus(strInteraction, "Sync pending");
				}
				vecLines.push_back(std::move(strInteraction));
			}

			return vecLines;
		}

		void DrawOperationHints(
			AshEngine::UIContext& refUi,
			float fContentOriginX,
			float fContentOriginY,
			float fContentWidth,
			float fContentHeight)
		{
			static const char* kHintLine =
				"RMB Rotate  |  Wheel Zoom  |  WASD Move  |  Shift Fast  |  F Focus";
			const float fPaddingX = 8.0f;
			const float fPaddingY = 5.0f;
			const float fMargin = 12.0f;
			const AshEngine::UIVec2 vecHintSize = refUi.calc_text_size(kHintLine);
			const float fBoxWidth = vecHintSize.x + fPaddingX * 2.0f;
			const float fBoxHeight = vecHintSize.y + fPaddingY * 2.0f;
			const float fBoxX = fContentOriginX + fContentWidth - fBoxWidth - fMargin;
			const float fBoxY = fContentOriginY + fContentHeight - fBoxHeight - fMargin;

			if (fBoxX < fContentOriginX + fMargin || fBoxY < fContentOriginY + fMargin)
			{
				return;
			}

			const AshEngine::UIRect rectBg{ fBoxX, fBoxY, fBoxWidth, fBoxHeight };
			refUi.draw_window_rect_filled(rectBg, { 0.10f, 0.12f, 0.16f, 0.48f }, 5.0f);
			refUi.draw_window_rect(rectBg, { 0.48f, 0.56f, 0.66f, 0.24f }, 5.0f);

			const AshEngine::UIColor colorHint{ 0.72f, 0.77f, 0.82f, 0.86f };
			refUi.draw_window_text({ fBoxX + fPaddingX, fBoxY + fPaddingY }, colorHint, kHintLine);
		}
	}
}

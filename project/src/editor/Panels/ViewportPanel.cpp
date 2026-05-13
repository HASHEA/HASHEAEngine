#include "Panels/ViewportPanel.h"

#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorGizmoTypes.h"
#include "Core/EditorIds.h"
#include "Core/EditorSelection.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Services/DragDropTransferService.h"
#include "Services/EditorViewportCameraService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorButtonWidgets.h"
#include "Widgets/ViewportAxisIndicator.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
		const char* GetViewportKindLabel(EditorViewportKind eKind)
		{
			switch (eKind)
			{
			case EditorViewportKind::Scene:
				return EditorWindowTitles::Scene;
			case EditorViewportKind::Game:
				return EditorWindowTitles::Game;
			default:
				return "Aux";
			}
		}

		const char* GetGizmoModeLabel(GizmoMode eMode)
		{
			switch (eMode)
			{
			case GizmoMode::Move:   return "W:Move";
			case GizmoMode::Rotate: return "E:Rotate";
			case GizmoMode::Scale:  return "R:Scale";
			default:                return "?";
			}
		}

		bool DrawGizmoModeButton(AshEngine::UIContext& refUi, const char* pLabel, bool bActive)
		{
			if (bActive)
			{
				PushEditorButtonVisuals(refUi);
			}
			const bool bClicked = refUi.small_button(pLabel);
			if (bActive)
			{
				PopEditorButtonVisuals(refUi);
			}
			return bClicked;
		}

		std::vector<std::string> MakeOverlayLines(
			const EditorViewportInstance& refViewport,
			const EditorViewportPresentation& refPresentation,
			const EditorViewportRenderState* pRenderState,
			bool bIsPrimary)
		{
			std::vector<std::string> vecLines{};
			std::string strHeader = GetViewportKindLabel(refPresentation.eKind);
			if (bIsPrimary)
			{
				strHeader += " | Primary";
			}
			if (refPresentation.bAcceptsInput)
			{
				strHeader += " | Input";
			}
			if (refPresentation.bPreserveAspect)
			{
				strHeader += " | Aspect";
			}
			vecLines.push_back(std::move(strHeader));

			vecLines.push_back(
				"Output " +
				std::to_string(refViewport.state.uWidth) +
				"x" +
				std::to_string(refViewport.state.uHeight) +
				"  Req " +
				std::to_string(refViewport.state.uRequestedWidth) +
				"x" +
				std::to_string(refViewport.state.uRequestedHeight));

			if (refPresentation.bShowStats)
			{
				vecLines.push_back(
					"Focused " +
					std::string(refViewport.state.bFocused ? "yes" : "no") +
					"  Hovered " +
					std::string(refViewport.state.bHovered ? "yes" : "no") +
					"  PendingSync " +
					std::string(pRenderState && pRenderState->bPendingSync ? "yes" : "no"));
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
			const float fPadding = 6.0f;
			const float fMargin = 12.0f;
			const float fLineSpacing = 2.0f;

			static const char* const kHints[] = {
				"RMB+Drag: Rotate",
				"Scroll: Zoom",
				"WASD: Move",
				"Shift: Fast",
				"F: Focus"
			};
			static const int kHintCount = 5;

			float fMaxWidth = 0.0f;
			float fTotalHeight = fPadding * 2.0f;
			float lineHeights[kHintCount] = {};
			for (int i = 0; i < kHintCount; ++i)
			{
				const AshEngine::UIVec2 vecSize = refUi.calc_text_size(kHints[i]);
				lineHeights[i] = vecSize.y;
				if (vecSize.x > fMaxWidth) fMaxWidth = vecSize.x;
				fTotalHeight += vecSize.y;
			}
			fTotalHeight += static_cast<float>(kHintCount - 1) * fLineSpacing;

			const float fBoxWidth = fMaxWidth + fPadding * 2.0f;
			const float fBoxX = fContentOriginX + fContentWidth - fBoxWidth - fMargin;
			const float fBoxY = fContentOriginY + fContentHeight - fTotalHeight - fMargin;

			if (fBoxX < fContentOriginX + fMargin || fBoxY < fContentOriginY + fMargin)
			{
				return;
			}

			const AshEngine::UIRect rectBg{ fBoxX, fBoxY, fBoxWidth, fTotalHeight };
			refUi.draw_window_rect_filled(rectBg, { 0.10f, 0.12f, 0.16f, 0.55f }, 4.0f);

			const AshEngine::UIColor colorHint{ 0.70f, 0.75f, 0.80f, 0.85f };
			float fTextY = fBoxY + fPadding;
			for (int i = 0; i < kHintCount; ++i)
			{
				refUi.draw_window_text({ fBoxX + fPadding, fTextY }, colorHint, kHints[i]);
				fTextY += lineHeights[i] + fLineSpacing;
			}
		}
	}

	ViewportPanel::ViewportPanel(
		std::string strViewportId,
		std::string strPanelId,
		std::string strTitle,
		ViewportPanelDeps deps)
		: EditorPanel(std::move(strPanelId), std::move(strTitle))
		, _deps(deps)
		, _strViewportId(std::move(strViewportId))
	{
	}

	void ViewportPanel::BindEventBus(EditorEventBus* pEventBus)
	{
		if (_eventBindings.IsBoundTo(pEventBus))
		{
			return;
		}

		_eventBindings.Bind(pEventBus);
		if (!pEventBus)
		{
			return;
		}

		_eventBindings.Subscribe<EditorViewportLayoutResetEvent>(
			[this](const EditorViewportLayoutResetEvent&)
			{
				ResetRuntimeViewportState();
			});
	}

	EditorViewportInstance* ViewportPanel::ResolveViewport()
	{
		return _deps.pViewportService ? _deps.pViewportService->FindViewport(_strViewportId) : nullptr;
	}

	const EditorViewportInstance* ViewportPanel::ResolveViewport() const
	{
		return _deps.pViewportService ? _deps.pViewportService->FindViewport(_strViewportId) : nullptr;
	}

	void ViewportPanel::ClearDeps()
	{
		_deps = {};
	}

	void ViewportPanel::OnAttach()
	{
		if (_deps.pViewportService)
		{
			_deps.pViewportService->EnsureViewport(_strViewportId, GetTitle());
		}
	}

	void ViewportPanel::OnDetach()
	{
		UnsubscribeEvents();
		ClearDeps();
	}

	void ViewportPanel::UnsubscribeEvents()
	{
		_eventBindings.Clear();
	}

	void ViewportPanel::ResetRuntimeViewportState()
	{
		EditorViewportInstance* pViewport = ResolveViewport();
		if (!pViewport)
		{
			return;
		}

		pViewport->state.bFocused = false;
		pViewport->state.bHovered = false;
		pViewport->state.uRequestedWidth = 0;
		pViewport->state.uRequestedHeight = 0;
	}

	void ViewportPanel::OnUpdate()
	{
		EditorViewportInstance* pViewport = ResolveViewport();
		if (!pViewport)
		{
			return;
		}

		const EditorViewportRenderState* pRenderState =
			_deps.pViewportService ? _deps.pViewportService->GetRenderState(_strViewportId) : nullptr;
		if (pRenderState)
		{
			pViewport->state.uWidth = pRenderState->uOutputWidth;
			pViewport->state.uHeight = pRenderState->uOutputHeight;
			return;
		}

		pViewport->state.uWidth = 0u;
		pViewport->state.uHeight = 0u;
	}

		void ViewportPanel::DrawToolbar(const EditorFrameContext& refFrameContext, EditorViewportInstance& refViewport)
	{
		if (!refFrameContext.pUiContext || !_deps.pViewportService)
		{
			return;
		}

		EditorViewportPresentation* pPresentation = _deps.pViewportService->GetPresentation(_strViewportId);
		if (!pPresentation || !pPresentation->bShowToolbar)
		{
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		refUi.text_unformatted(GetViewportKindLabel(pPresentation->eKind));
		refUi.same_line();
		const bool bIsPrimary = _deps.pViewportService->IsPrimaryViewport(refViewport.strId);
		if (refUi.small_button(bIsPrimary ? "Primary" : "Set Primary"))
		{
			_deps.pViewportService->SetPrimaryViewport(_strViewportId);
		}

		refUi.same_line();
		DrawEditorToggleButton(refUi, "Aspect", pPresentation->bPreserveAspect);
		refUi.same_line();
		DrawEditorToggleButton(refUi, "Input", pPresentation->bAcceptsInput);

		// Keep the toolbar compact. Less-frequent flags live behind an options popup.
		refUi.same_line();
		if (refUi.small_button("Options"))
		{
			refUi.open_popup("ViewportOptions");
		}
		if (refUi.begin_popup("ViewportOptions"))
		{
			refUi.menu_item("Show Stats", nullptr, &pPresentation->bShowStats, true);
			refUi.menu_item("Show Overlay", nullptr, &pPresentation->bShowOverlays, true);
			refUi.separator();
			refUi.menu_item("Preserve Aspect", nullptr, &pPresentation->bPreserveAspect, true);
			refUi.menu_item("Accept Input", nullptr, &pPresentation->bAcceptsInput, true);
			refUi.end_popup();
		}

		// Gizmo toolbar — only for Scene viewports.
		if (_deps.pGizmoState && pPresentation->eKind == EditorViewportKind::Scene)
		{
			EditorGizmoState& refGizmo = *_deps.pGizmoState;

			refUi.same_line();
			refUi.separator();
			refUi.same_line();

			if (DrawGizmoModeButton(refUi, "W:Move", refGizmo.eMode == GizmoMode::Move))
			{
				refGizmo.eMode = GizmoMode::Move;
			}
			refUi.same_line();
			if (DrawGizmoModeButton(refUi, "E:Rotate", refGizmo.eMode == GizmoMode::Rotate))
			{
				refGizmo.eMode = GizmoMode::Rotate;
			}
			refUi.same_line();
			if (DrawGizmoModeButton(refUi, "R:Scale", refGizmo.eMode == GizmoMode::Scale))
			{
				refGizmo.eMode = GizmoMode::Scale;
			}

			refUi.same_line();
			refUi.separator();
			refUi.same_line();

			const bool bIsLocal = refGizmo.eSpace == GizmoCoordinateSpace::Local;
			if (refUi.small_button(bIsLocal ? "Local" : "World"))
			{
				refGizmo.eSpace = bIsLocal ? GizmoCoordinateSpace::World : GizmoCoordinateSpace::Local;
			}

			refUi.same_line();
			const bool bIsPivot = refGizmo.ePivot == GizmoPivotMode::Pivot;
			if (refUi.small_button(bIsPivot ? "Pivot" : "Center"))
			{
				refGizmo.ePivot = bIsPivot ? GizmoPivotMode::Center : GizmoPivotMode::Pivot;
			}

			refUi.same_line();
			DrawEditorToggleButton(refUi, "Snap", refGizmo.snap.bSnapEnabled);
		}
	}

	void ViewportPanel::DrawOverlay(const EditorFrameContext& refFrameContext, const EditorViewportInstance& refViewport) const
	{
		if (!refFrameContext.pUiContext || !_deps.pViewportService)
		{
			return;
		}

		const EditorViewportPresentation* pPresentation = _deps.pViewportService->GetPresentation(_strViewportId);
		const EditorViewportRenderState* pRenderState = _deps.pViewportService->GetRenderState(_strViewportId);
		if (!pPresentation || (!pPresentation->bShowOverlays && !pPresentation->bShowStats))
		{
			return;
		}

		const std::vector<std::string> vecLines = MakeOverlayLines(
			refViewport,
			*pPresentation,
			pRenderState,
			_deps.pViewportService->IsPrimaryViewport(refViewport.strId));
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

		// Keep overlay inside the viewport rect so it doesn't get clipped by other docked windows.
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

	void ViewportPanel::HandleViewportInput(
		const EditorFrameContext& refFrameContext,
		const EditorViewportInstance& refViewport,
		const AshEngine::UIRect& rectContent,
		bool bContentHovered)
	{
		if (!refFrameContext.pUiContext || !_deps.pViewportService || !_deps.pViewportCameraService || !_deps.pSceneService)
		{
			return;
		}

		const EditorViewportPresentation* pPresentation = _deps.pViewportService->GetPresentation(_strViewportId);
		if (!pPresentation || pPresentation->eKind != EditorViewportKind::Scene || !pPresentation->bAcceptsInput)
		{
			return;
		}

		AshEngine::Application* pApplication = AshEngine::Application::get();
		if (!pApplication)
		{
			return;
		}

		EditorViewportCameraInputContext inputContext{};
		inputContext.strViewportId = _strViewportId;
		inputContext.rectContent = rectContent;
		inputContext.bViewportFocused = refViewport.state.bFocused || bContentHovered;
		inputContext.bViewportHovered = bContentHovered;
		inputContext.bAcceptsInput = pPresentation->bAcceptsInput;
		if (_deps.pSelectionService)
		{
			const EditorSelection& refSelection = _deps.pSelectionService->GetSelection();
			inputContext.uFocusEntityId =
				refSelection.eKind == EditorSelectionKind::Entity
				? refSelection.uId
				: 0;
		}

		_deps.pViewportCameraService->UpdateViewportInput(
			*_deps.pSceneService,
			AshEngine::Application::get_input(),
			refFrameContext.pUiContext->get_time_seconds(),
			inputContext);
	}

	void ViewportPanel::OnGui(const EditorFrameContext& frameContext)
	{
		EditorViewportInstance* pViewport = ResolveViewport();
		const bool bWindowVisible = BeginPanelWindow(frameContext, AshEngine::UIWindowFlagBits::MenuBar);
		if (_deps.pViewportService)
		{
			_deps.pViewportService->SetPanelOpen(_strViewportId, IsOpen());
		}

		if (!bWindowVisible)
		{
			EndPanelWindow(frameContext);
			return;
		}

		if (!frameContext.pUiContext)
		{
			EndPanelWindow(frameContext);
			return;
		}

		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		if (pViewport)
		{
			pViewport->state.bFocused = refUi.is_window_focused();
			pViewport->state.bHovered = refUi.is_window_hovered();
			if (_deps.pViewportService)
			{
				if (pViewport->state.bFocused)
				{
					_deps.pViewportService->SetPrimaryViewport(_strViewportId);
				}
			}
		}

		if (refUi.begin_menu_bar())
		{
			if (pViewport)
			{
				DrawToolbar(frameContext, *pViewport);
			}
			refUi.end_menu_bar();
		}

		const AshEngine::UIVec2 vecAvailableSize = refUi.get_content_region_avail();
		if (pViewport)
		{
			if (_deps.pViewportService)
			{
				_deps.pViewportService->UpdateRequestedSize(
					_strViewportId,
					vecAvailableSize.x > 1.0f ? static_cast<uint32_t>(vecAvailableSize.x) : 0u,
					vecAvailableSize.y > 1.0f ? static_cast<uint32_t>(vecAvailableSize.y) : 0u);
			}
		}

		if (pViewport && pViewport->surface.is_valid() && frameContext.pUiContext && vecAvailableSize.x > 2.0f && vecAvailableSize.y > 2.0f)
		{
			bool bPreserveAspect = false;
			if (const EditorViewportPresentation* pPresentation =
				_deps.pViewportService ? _deps.pViewportService->GetPresentation(_strViewportId) : nullptr)
			{
				bPreserveAspect = pPresentation->bPreserveAspect;
			}
			const bool bOutputSizePending =
				pViewport->state.uRequestedWidth > 0u &&
				pViewport->state.uRequestedHeight > 0u &&
				(pViewport->state.uWidth != pViewport->state.uRequestedWidth ||
				 pViewport->state.uHeight != pViewport->state.uRequestedHeight);
			if (pViewport->state.uWidth == 0u || pViewport->state.uHeight == 0u)
			{
				refUi.text_wrapped("Scene surface is not available yet.");
			}
			else if (bOutputSizePending)
			{
				// Skip drawing the stale frame instead of stretching it to the new viewport extent.
				refUi.dummy(vecAvailableSize);
				const AshEngine::UIRect rectContent = refUi.get_item_rect();
				refUi.draw_window_rect_filled(rectContent, { 0.05f, 0.06f, 0.08f, 1.0f }, 0.0f);
				refUi.draw_window_text(
					{ rectContent.x + 14.0f, rectContent.y + 12.0f },
					{ 0.72f, 0.76f, 0.82f, 0.92f },
					"Updating viewport...");
			}
			else
			{
				frameContext.pUiContext->draw_surface_fill_available(pViewport->surface, bPreserveAspect);
				const bool bContentHovered = refUi.is_item_hovered();
				DrawOverlay(frameContext, *pViewport);

				// Axis indicator (top-right corner) and operation hints (bottom-right corner).
				const AshEngine::UIRect rectContent = frameContext.pUiContext->get_item_rect();
				HandleViewportInput(frameContext, *pViewport, rectContent, bContentHovered);
				DrawViewportAxisIndicator(
					refUi,
					rectContent.x, rectContent.y,
					rectContent.width, rectContent.height);
				DrawOperationHints(
					refUi,
					rectContent.x, rectContent.y,
					rectContent.width, rectContent.height);
			}
		}
		else if (!pViewport || !pViewport->surface.is_valid())
		{
			refUi.text_wrapped("Scene surface is not available yet.");
		}
		else
		{
			refUi.text_wrapped("Viewport preview is waiting for the engine UIContext.");
		}

		// Accept scene entity drag from Hierarchy to select it.
		if (_deps.pDragDropTransferService && _deps.pSelectionService && refUi.begin_drag_drop_target())
		{
			const AshEngine::UIDragDropPayload payload =
				refUi.accept_drag_drop_payload(EditorDragPayloadTypes::SceneEntity);
			if (payload.is_delivery && payload.data && payload.data_size == sizeof(DragDropTransferId))
			{
				DragDropTransferId uTransferId = 0;
				std::memcpy(&uTransferId, payload.data, sizeof(DragDropTransferId));
				const DragDropTransferData* pData = _deps.pDragDropTransferService->Resolve(uTransferId);
				if (pData && !pData->vecEntityIds.empty())
				{
					EditorSelection sel{};
					sel.eKind = EditorSelectionKind::Entity;
					sel.uId = pData->vecEntityIds[0];
					_deps.pSelectionService->Select(std::move(sel));
				}
			}
			refUi.end_drag_drop_target();
		}

		EndPanelWindow(frameContext);
	}
}

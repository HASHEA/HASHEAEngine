#include "Panels/ViewportPanel.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Services/EditorViewportService.h"
#include "Widgets/EditorButtonWidgets.h"

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

		bool ShouldTraceViewportPanel()
		{
			static uint32_t uLoggedFrames = 0u;
			++uLoggedFrames;
			return uLoggedFrames <= 2u;
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
		HLogInfo("ViewportPanel attached.");
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
		refUi.same_line();
		DrawEditorToggleButton(refUi, "Stats", pPresentation->bShowStats);
		refUi.same_line();
		DrawEditorToggleButton(refUi, "Overlay", pPresentation->bShowOverlays);
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

		const AshEngine::UIRect rectOverlay{
			rectItem.x + 12.0f,
			rectItem.y + 12.0f,
			fMaxWidth + fPadding * 2.0f,
			fTotalHeight
		};
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

	void ViewportPanel::OnGui(const EditorFrameContext& frameContext)
	{
		EditorViewportInstance* pViewport = ResolveViewport();
		const bool bTraceThisFrame = ShouldTraceViewportPanel();
		if (bTraceThisFrame)
		{
			HLogInfo(
				"ViewportPanel::OnGui begin. surface={}, ui_ready={}, requested={}x{}.",
				pViewport ? pViewport->surface.value : 0u,
				frameContext.bGuiRendererReady,
				pViewport ? pViewport->state.uRequestedWidth : 0u,
				pViewport ? pViewport->state.uRequestedHeight : 0u);
		}

		const bool bWindowVisible = BeginPanelWindow(frameContext, AshEngine::UIWindowFlagBits::MenuBar);
		if (_deps.pViewportService)
		{
			_deps.pViewportService->SetPanelOpen(_strViewportId, IsOpen());
		}

		if (!bWindowVisible)
		{
			if (bTraceThisFrame)
			{
				HLogWarning("ViewportPanel::OnGui skipped because Begin returned false.");
			}
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
			if (pViewport->state.uWidth == 0u || pViewport->state.uHeight == 0u)
			{
				if (bTraceThisFrame)
				{
					HLogWarning("ViewportPanel::OnGui is waiting for a synchronized scene surface.");
				}
				refUi.text_wrapped("Scene surface is not available yet.");
			}
			else
			{
				if (bTraceThisFrame)
				{
					HLogInfo(
						"ViewportPanel::OnGui drawing scene surface {} with available size {}x{}.",
						pViewport->surface.value,
						vecAvailableSize.x,
						vecAvailableSize.y);
				}
				frameContext.pUiContext->draw_surface_fill_available(pViewport->surface, bPreserveAspect);
				DrawOverlay(frameContext, *pViewport);
				if (bTraceThisFrame)
				{
					HLogInfo("ViewportPanel::OnGui finished drawing scene surface.");
				}
			}
		}
		else if (!pViewport || !pViewport->surface.is_valid())
		{
			if (bTraceThisFrame)
			{
				HLogWarning("ViewportPanel::OnGui has no scene surface to display.");
			}
			refUi.text_wrapped("Scene surface is not available yet.");
		}
		else
		{
			if (bTraceThisFrame)
			{
				HLogWarning("ViewportPanel::OnGui is waiting for UIContext or sufficient panel size.");
			}
			refUi.text_wrapped("Viewport preview is waiting for the engine UIContext.");
		}

		EndPanelWindow(frameContext);
		if (bTraceThisFrame)
		{
			HLogInfo("ViewportPanel::OnGui end.");
		}
	}
}

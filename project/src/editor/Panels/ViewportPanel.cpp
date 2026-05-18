#include "Panels/ViewportPanel.h"
#include "Panels/ViewportPanelCanvas.h"
#include "Panels/ViewportPanelInteraction.h"
#include "Panels/ViewportPanelToolbar.h"

#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Function/Gui/UIContext.h"
#include "Services/EditorViewportService.h"

#include <utility>

namespace AshEditor
{
	namespace
	{
		void ResetViewportRuntimeState(
			EditorViewportInstance* pViewport,
			ViewportPanelSceneBoxSelectionState& refSceneBoxSelection,
			bool bResetRequestedSize)
		{
			refSceneBoxSelection = {};
			if (!pViewport)
			{
				return;
			}

			pViewport->state.bFocused = false;
			pViewport->state.bHovered = false;
			pViewport->state.bContentHovered = false;
			pViewport->state.rectContent = {};
			if (bResetRequestedSize)
			{
				pViewport->state.uRequestedWidth = 0;
				pViewport->state.uRequestedHeight = 0;
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
		ResetViewportRuntimeState(ResolveViewport(), _sceneBoxSelection, true);
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
			ResetViewportRuntimeState(pViewport, _sceneBoxSelection, false);
			EndPanelWindow(frameContext);
			return;
		}

		if (!frameContext.pUiContext)
		{
			ResetViewportRuntimeState(pViewport, _sceneBoxSelection, false);
			EndPanelWindow(frameContext);
			return;
		}

		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		if (pViewport)
		{
			pViewport->state.bFocused = refUi.is_window_focused();
			pViewport->state.bHovered = refUi.is_window_hovered();
			pViewport->state.bContentHovered = false;
			pViewport->state.rectContent = {};
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
				ViewportPanelToolbar::Draw(frameContext, _deps, _strViewportId, *pViewport);
			}
			refUi.end_menu_bar();
		}

		ViewportPanelCanvasDrawResult drawResult{};
		if (pViewport)
		{
			drawResult = ViewportPanelCanvas::Draw(frameContext, _deps, _strViewportId, *pViewport);
		}

		if (!pViewport)
		{
			_sceneBoxSelection = {};
			refUi.text_wrapped("Scene surface is not available yet.");
		}
		else if (drawResult.bHasViewportContent)
		{
			ViewportPanelInteraction::HandleViewportInput(
				frameContext,
				_deps,
				_strViewportId,
				*pViewport,
				drawResult.rectContent,
				drawResult.bContentHovered,
				_sceneBoxSelection);
			ViewportPanelCanvas::DrawDecorations(
				frameContext,
				_deps,
				_strViewportId,
				*pViewport,
				drawResult,
				_sceneBoxSelection);
		}

		ViewportPanelInteraction::HandleDragDropTarget(
			refUi,
			_deps,
			_strViewportId,
			drawResult.rectContent,
			drawResult.bHasViewportContent);

		EndPanelWindow(frameContext);
	}
}

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
			ViewportPanelSceneSelectionState& refSceneSelection,
			bool bResetRequestedSize)
		{
			refSceneSelection = {};
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

		void SyncViewportPanelOpenState(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			bool bPanelOpen)
		{
			if (refDeps.pViewportService)
			{
				refDeps.pViewportService->SetPanelOpen(strViewportId, bPanelOpen);
			}
		}

		void UpdateViewportWindowState(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			AshEngine::UIContext& refUi,
			EditorViewportInstance& refViewport)
		{
			refViewport.state.bFocused = refUi.is_window_focused_with_children();
			refViewport.state.bHovered = refUi.is_window_hovered_with_children();
			refViewport.state.bContentHovered = false;
			refViewport.state.rectContent = {};

			if (!refDeps.pViewportService)
			{
				return;
			}

			const EditorViewportPresentation* pPresentation =
				refDeps.pViewportService->GetPresentation(strViewportId);
			const bool bAutoPromoteToPrimary =
				!pPresentation || pPresentation->eKind != EditorViewportKind::Game;
			if (refViewport.state.bFocused && bAutoPromoteToPrimary)
			{
				refDeps.pViewportService->SetPrimaryViewport(strViewportId);
			}
		}

		void DrawViewportMenuBar(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance* pViewport)
		{
			if (!refFrameContext.pUiContext)
			{
				return;
			}

			AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
			if (!refUi.begin_menu_bar())
			{
				return;
			}

			if (pViewport)
			{
				ViewportPanelToolbar::Draw(refFrameContext, refDeps, strViewportId, *pViewport);
			}
			refUi.end_menu_bar();
		}

		void DrawViewportContent(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			EditorViewportInstance* pViewport,
			const ViewportPanelCanvasDrawResult& refDrawResult,
			ViewportPanelSceneSelectionState& refSceneSelection)
		{
			if (!refFrameContext.pUiContext)
			{
				return;
			}

			AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
			if (!pViewport)
			{
				refSceneSelection = {};
				refUi.text_wrapped("Scene surface is not available yet.");
				return;
			}
			if (!refDrawResult.bHasViewportContent)
			{
				return;
			}

			ViewportPanelInteraction::HandleViewportInput(
				refFrameContext,
				refDeps,
				strViewportId,
				*pViewport,
				refDrawResult.rectContent,
				refDrawResult.bContentHovered,
				refSceneSelection);
			ViewportPanelCanvas::DrawDecorations(
				refFrameContext,
				refDeps,
				strViewportId,
				*pViewport,
				refDrawResult,
				refSceneSelection);
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
		ResetViewportRuntimeState(ResolveViewport(), _sceneSelection, true);
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
		SyncViewportPanelOpenState(_deps, _strViewportId, IsOpen());

		if (!bWindowVisible)
		{
			ResetViewportRuntimeState(pViewport, _sceneSelection, false);
			EndPanelWindow(frameContext);
			return;
		}

		if (!frameContext.pUiContext)
		{
			ResetViewportRuntimeState(pViewport, _sceneSelection, false);
			EndPanelWindow(frameContext);
			return;
		}

		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		if (pViewport)
		{
			UpdateViewportWindowState(_deps, _strViewportId, refUi, *pViewport);
		}
		DrawViewportMenuBar(frameContext, _deps, _strViewportId, pViewport);

		ViewportPanelCanvasDrawResult drawResult{};
		if (pViewport)
		{
			drawResult = ViewportPanelCanvas::Draw(frameContext, _deps, _strViewportId, *pViewport);
		}

		DrawViewportContent(
			frameContext,
			_deps,
			_strViewportId,
			pViewport,
			drawResult,
			_sceneSelection);

		ViewportPanelInteraction::HandleDragDropTarget(
			refUi,
			_deps,
			_strViewportId,
			drawResult.rectContent,
			drawResult.bHasViewportContent);

		EndPanelWindow(frameContext);
	}
}

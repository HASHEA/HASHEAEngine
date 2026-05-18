#include "Panels/ViewportPanelToolbar.h"

#include "Core/EditorGizmoTypes.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/EditorViewportService.h"
#include "Widgets/EditorButtonWidgets.h"

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

		bool DrawGizmoModeButton(AshEngine::UIContext& refUi, const char* pLabel, bool bActive)
		{
			if (bActive)
			{
				PushEditorPrimaryButtonVisuals(refUi);
			}

			const bool bClicked = refUi.small_button(pLabel);

			if (bActive)
			{
				PopEditorButtonVisuals(refUi);
			}

			return bClicked;
		}
	}

	namespace ViewportPanelToolbar
	{
		void Draw(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport)
		{
			if (!refFrameContext.pUiContext || !refDeps.pViewportService)
			{
				return;
			}

			EditorViewportPresentation* pPresentation = refDeps.pViewportService->GetPresentation(strViewportId);
			if (!pPresentation || !pPresentation->bShowToolbar)
			{
				return;
			}

			AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
			refUi.text_unformatted(GetViewportKindLabel(pPresentation->eKind));
			refUi.same_line();

			const bool bIsPrimary = refDeps.pViewportService->IsPrimaryViewport(refViewport.strId);
			if (refUi.small_button(bIsPrimary ? "Primary" : "Set Primary"))
			{
				refDeps.pViewportService->SetPrimaryViewport(strViewportId);
			}

			refUi.same_line();
			DrawEditorToggleButton(refUi, "Aspect", pPresentation->bPreserveAspect);
			refUi.same_line();
			DrawEditorToggleButton(refUi, "Input", pPresentation->bAcceptsInput);

			refUi.same_line();
			if (refUi.small_button("Options"))
			{
				refUi.open_popup("ViewportOptions");
			}
			if (refUi.begin_popup("ViewportOptions"))
			{
				ViewportPanelSupport::DrawViewportDisplayOptionsMenu(refUi, *pPresentation);
				if (pPresentation->eKind == EditorViewportKind::Scene)
				{
					ViewportPanelSupport::DrawSceneViewportHelperOptionsMenu(refUi, *pPresentation);
				}
				ViewportPanelSupport::DrawViewportInteractionOptionsMenu(refDeps, refUi, strViewportId, *pPresentation);
				refUi.end_popup();
			}

			if (!refDeps.pGizmoState || pPresentation->eKind != EditorViewportKind::Scene)
			{
				return;
			}

			EditorGizmoState& refGizmo = *refDeps.pGizmoState;
			refUi.same_line();
			refUi.separator();
			refUi.same_line();

			if (DrawGizmoModeButton(refUi, "W:Move", refGizmo.eMode == GizmoMode::Move))
			{
				refGizmo.eMode = GizmoMode::Move;
			}
			refUi.same_line();
			if (DrawGizmoModeButton(refUi, "E:Scale", refGizmo.eMode == GizmoMode::Scale))
			{
				refGizmo.eMode = GizmoMode::Scale;
			}
			refUi.same_line();
			if (DrawGizmoModeButton(refUi, "R:Rotate", refGizmo.eMode == GizmoMode::Rotate))
			{
				refGizmo.eMode = GizmoMode::Rotate;
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
}

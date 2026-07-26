#include "Panels/ViewportPanelToolbar.h"

#include "Core/EditorGizmoTypes.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/EditorSettingsService.h"
#include "Services/EditorViewportCameraService.h"
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

		bool DrawToolbarSegmentButton(AshEngine::UIContext& refUi, const char* pLabel, bool bActive)
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

		void DrawToolbarDivider(AshEngine::UIContext& refUi)
		{
			refUi.same_line(0.0f, 10.0f);
			refUi.separator();
			refUi.same_line(0.0f, 10.0f);
		}

		void DrawViewportOptionsPopup(
			AshEngine::UIContext& refUi,
			EditorViewportPresentation& refMutablePresentation)
		{
			if (refUi.small_button("View"))
			{
				refUi.open_popup("ViewportOptions");
			}
			if (!refUi.begin_popup("ViewportOptions"))
			{
				return;
			}

			ViewportPanelSupport::DrawViewportDisplayOptionsMenu(refUi, refMutablePresentation);
			if (refMutablePresentation.eKind == EditorViewportKind::Scene)
			{
				ViewportPanelSupport::DrawSceneViewportHelperOptionsMenu(refUi, refMutablePresentation);
			}
			ViewportPanelSupport::DrawViewportInteractionOptionsMenu(refUi, refMutablePresentation);
			refUi.end_popup();
		}

		void DrawSceneCameraSpeedControl(
			AshEngine::UIContext& refUi,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId)
		{
			const float fDragWidth = 120.0f;
			const float fLabelWidth = refUi.calc_text_size("CameraSpeed").x;
			const float fRightAlignedOffset =
				refUi.get_window_width() - (fDragWidth + fLabelWidth + 20.0f);
			refUi.same_line(fRightAlignedOffset);

			float fMoveSpeed = refDeps.pViewportCameraService->GetMoveSpeed(strViewportId);
			refUi.set_next_item_width(fDragWidth);
			if (refUi.drag_float(
				"CameraSpeed",
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

		void DrawSceneGizmoModeControls(AshEngine::UIContext& refUi, EditorGizmoState& refGizmo)
		{
			if (DrawToolbarSegmentButton(refUi, "Move", refGizmo.eMode == GizmoMode::Move))
			{
				refGizmo.eMode = GizmoMode::Move;
			}
			refUi.same_line();
			if (DrawToolbarSegmentButton(refUi, "Scale", refGizmo.eMode == GizmoMode::Scale))
			{
				refGizmo.eMode = GizmoMode::Scale;
			}
			refUi.same_line();
			if (DrawToolbarSegmentButton(refUi, "Rotate", refGizmo.eMode == GizmoMode::Rotate))
			{
				refGizmo.eMode = GizmoMode::Rotate;
			}
		}

		void DrawSceneGizmoSpaceControls(AshEngine::UIContext& refUi, EditorGizmoState& refGizmo)
		{
			const bool bIsLocal = refGizmo.eSpace == GizmoCoordinateSpace::Local;
			if (DrawToolbarSegmentButton(refUi, bIsLocal ? "Local" : "World", bIsLocal))
			{
				refGizmo.eSpace = bIsLocal ? GizmoCoordinateSpace::World : GizmoCoordinateSpace::Local;
			}

			refUi.same_line();
			const bool bIsPivot = refGizmo.ePivot == GizmoPivotMode::Pivot;
			if (DrawToolbarSegmentButton(refUi, bIsPivot ? "Pivot" : "Center", bIsPivot))
			{
				refGizmo.ePivot = bIsPivot ? GizmoPivotMode::Center : GizmoPivotMode::Pivot;
			}

			refUi.same_line();
			DrawEditorToggleButton(refUi, "Snap", refGizmo.snap.bSnapEnabled);
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
			const bool bSceneViewport = pPresentation->eKind == EditorViewportKind::Scene;
			refUi.push_style_var(AshEngine::UIStyleVarKind::FramePadding, { 8.0f, 4.0f });
			refUi.push_style_var(AshEngine::UIStyleVarKind::ItemSpacing, { 6.0f, 4.0f });

			refUi.push_font(AshEngine::UIFontRole::Strong);
			refUi.text_unformatted(GetViewportKindLabel(pPresentation->eKind));
			refUi.pop_font();

			refUi.same_line();

			const bool bIsPrimary = refDeps.pViewportService->IsPrimaryViewport(refViewport.strId);
			if (DrawToolbarSegmentButton(refUi, bIsPrimary ? "Primary" : "Make Primary", bIsPrimary))
			{
				refDeps.pViewportService->SetPrimaryViewport(strViewportId);
			}

			DrawToolbarDivider(refUi);
			DrawEditorToggleButton(refUi, "Aspect", pPresentation->bPreserveAspect);
			if (!bSceneViewport)
			{
				refUi.same_line();
				DrawEditorToggleButton(refUi, "Input", pPresentation->bAcceptsInput);
			}

			refUi.same_line(0.0f, 10.0f);
			DrawViewportOptionsPopup(refUi, *pPresentation);

			if (refDeps.pGizmoState && bSceneViewport)
			{
				EditorGizmoState& refGizmo = *refDeps.pGizmoState;
				DrawToolbarDivider(refUi);
				DrawSceneGizmoModeControls(refUi, refGizmo);
				DrawToolbarDivider(refUi);
				DrawSceneGizmoSpaceControls(refUi, refGizmo);
			}

			if (bSceneViewport && refDeps.pViewportCameraService)
			{
				DrawSceneCameraSpeedControl(refUi, refDeps, strViewportId);
			}

			refUi.pop_style_var(2);
		}
	}
}

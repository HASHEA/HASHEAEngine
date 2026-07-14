#include "Panels/ViewportPanelToolbar.h"

#include "Core/EditorGizmoTypes.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/EditorViewportService.h"
#include "Services/TerrainEditorService.h"
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
			const EditorViewportPresentation& refPresentation,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
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
			if (refPresentation.eKind == EditorViewportKind::Scene)
			{
				ViewportPanelSupport::DrawSceneViewportHelperOptionsMenu(refUi, refMutablePresentation);
			}
			ViewportPanelSupport::DrawViewportInteractionOptionsMenu(
				refDeps,
				refUi,
				strViewportId,
				refMutablePresentation);
			refUi.end_popup();
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
			DrawViewportOptionsPopup(refUi, *pPresentation, refDeps, strViewportId, *pPresentation);

			if (refDeps.pGizmoState && bSceneViewport)
			{
				EditorGizmoState& refGizmo = *refDeps.pGizmoState;
				bool terrainOwnsSceneTools = false;
				if (strViewportId == EditorViewportIds::Scene &&
					bIsPrimary &&
					refDeps.pTerrainEditorService)
				{
					const TerrainEditorMode mode =
						refDeps.pTerrainEditorService->GetAuthoringConfig().mode;
					terrainOwnsSceneTools =
						mode == TerrainEditorMode::Sculpt ||
						mode == TerrainEditorMode::Paint;
				}
				DrawToolbarDivider(refUi);
				refUi.begin_disabled(terrainOwnsSceneTools);
				DrawSceneGizmoModeControls(refUi, refGizmo);
				refUi.end_disabled();
				DrawToolbarDivider(refUi);
				DrawSceneGizmoSpaceControls(refUi, refGizmo);
			}

			refUi.pop_style_var(2);
		}
	}
}

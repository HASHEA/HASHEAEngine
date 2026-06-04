#include "Panels/ViewportPanelInteraction.h"

#include "Core/EditorViewportInputState.h"
#include "Core/EditorIds.h"
#include "Core/EditorSelection.h"
#include "Function/Gui/UIContext.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/AssetDatabaseService.h"
#include "Services/DragDropTransferService.h"
#include "Services/EditorGizmoService.h"
#include "Services/EditorViewportCameraService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorThemeColors.h"

#include <cstring>
#include <utility>

namespace AshEditor
{
	namespace
	{
		constexpr float kSceneBoxSelectionDragThresholdPixels = 5.0f;
		constexpr double kSceneClickPickFallbackDelaySeconds = 0.2;

		void ClearSceneBoxSelectionTracking(ViewportPanelSceneSelectionState& refSceneBoxSelectionState)
		{
			refSceneBoxSelectionState.bTracking = false;
			refSceneBoxSelectionState.bDragging = false;
			refSceneBoxSelectionState.vecStartScreen = {};
			refSceneBoxSelectionState.vecCurrentScreen = {};
			refSceneBoxSelectionState.uStartModifiers = AshEngine::UIModifierFlagBits::None;
		}

		void ClearScenePendingPick(ViewportPanelSceneSelectionState& refSceneBoxSelectionState)
		{
			refSceneBoxSelectionState.bPendingPick = false;
			refSceneBoxSelectionState.strPendingPickViewportId.clear();
			refSceneBoxSelectionState.pendingPickBinding = {};
			refSceneBoxSelectionState.rectPendingPickContent = {};
			refSceneBoxSelectionState.vecPendingPickScreen = {};
			refSceneBoxSelectionState.uPendingPickModifiers = AshEngine::UIModifierFlagBits::None;
			refSceneBoxSelectionState.dPendingPickRequestTimeSeconds = 0.0;
		}

		void UpdateSceneViewportPendingClickSelection(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			double dTimeSeconds,
			ViewportPanelSceneSelectionState& refSceneBoxSelectionState)
		{
			if (!refSceneBoxSelectionState.bPendingPick ||
				refSceneBoxSelectionState.strPendingPickViewportId != strViewportId)
			{
				return;
			}

			AshEngine::ScenePickResult pickResult{};
			if (ViewportPanelSupport::PollSceneViewportClickPick(
				refSceneBoxSelectionState.pendingPickBinding,
				pickResult))
			{
				ViewportPanelSupport::ApplySceneViewportPickResultSelection(
					refDeps,
					pickResult,
					refSceneBoxSelectionState.uPendingPickModifiers);
				ClearScenePendingPick(refSceneBoxSelectionState);
				return;
			}

			if (dTimeSeconds - refSceneBoxSelectionState.dPendingPickRequestTimeSeconds < kSceneClickPickFallbackDelaySeconds)
			{
				return;
			}

			ViewportPanelSupport::ApplySceneViewportClickSelection(
				refDeps,
				refSceneBoxSelectionState.strPendingPickViewportId,
				refSceneBoxSelectionState.rectPendingPickContent,
				refSceneBoxSelectionState.vecPendingPickScreen,
				refSceneBoxSelectionState.uPendingPickModifiers);
			ClearScenePendingPick(refSceneBoxSelectionState);
		}

		void BeginSceneViewportPendingClickSelection(
			ViewportPanelSceneSelectionState& refSceneBoxSelectionState,
			const std::string& strViewportId,
			AshEngine::SceneViewBindingHandle pickBinding,
			const AshEngine::UIRect& rectContent,
			const AshEngine::UIVec2& vecMousePosition,
			AshEngine::UIModifierFlags uModifiers,
			double dTimeSeconds)
		{
			refSceneBoxSelectionState.bPendingPick = true;
			refSceneBoxSelectionState.strPendingPickViewportId = strViewportId;
			refSceneBoxSelectionState.pendingPickBinding = pickBinding;
			refSceneBoxSelectionState.rectPendingPickContent = rectContent;
			refSceneBoxSelectionState.vecPendingPickScreen = vecMousePosition;
			refSceneBoxSelectionState.uPendingPickModifiers = uModifiers;
			refSceneBoxSelectionState.dPendingPickRequestTimeSeconds = dTimeSeconds;
		}

		void HandleSceneViewportModeShortcuts(
			AshEngine::UIContext& refUi,
			const EditorViewportInputState& refInput,
			const bool bViewportHovered,
			EditorGizmoState* pGizmoState)
		{
			if (!pGizmoState || !bViewportHovered || refUi.is_any_item_active() || refUi.wants_text_input())
			{
				return;
			}

			const bool bHasModifiers =
				refInput.uModifiers != AshEngine::UIModifierFlagBits::None;
			if (bHasModifiers)
			{
				return;
			}

			if (refInput.WasKeyPressed(AshEngine::UIKey::W))
			{
				pGizmoState->eMode = GizmoMode::Move;
			}
			else if (refInput.WasKeyPressed(AshEngine::UIKey::E))
			{
				pGizmoState->eMode = GizmoMode::Scale;
			}
			else if (refInput.WasKeyPressed(AshEngine::UIKey::R))
			{
				pGizmoState->eMode = GizmoMode::Rotate;
			}
		}

		EditorViewportInputState BuildViewportInputState(AshEngine::UIContext& refUi)
		{
			EditorViewportInputState input{};
			input.vecMouseScreenPosition = refUi.get_mouse_pos();
			input.vecMouseWheelDelta = refUi.get_mouse_wheel_delta();
			input.uModifiers = refUi.get_key_modifiers();

			static constexpr AshEngine::UIMouseButton kMouseButtons[] = {
				AshEngine::UIMouseButton::Left,
				AshEngine::UIMouseButton::Right,
				AshEngine::UIMouseButton::Middle
			};
			for (const AshEngine::UIMouseButton button : kMouseButtons)
			{
				input.arrMouseDown[static_cast<size_t>(button)] = refUi.is_mouse_down(button);
				input.arrMousePressed[static_cast<size_t>(button)] = refUi.is_mouse_clicked(button, false);
				input.arrMouseReleased[static_cast<size_t>(button)] = refUi.is_mouse_released(button);
			}

			static constexpr AshEngine::UIKey kKeys[] = {
				AshEngine::UIKey::F,
				AshEngine::UIKey::W,
				AshEngine::UIKey::E,
				AshEngine::UIKey::R
			};
			for (const AshEngine::UIKey key : kKeys)
			{
				input.arrKeyDown[static_cast<size_t>(key)] = refUi.is_key_down(key);
				input.arrKeyPressed[static_cast<size_t>(key)] = refUi.is_key_pressed(key, false);
			}

			return input;
		}

		void UpdateSceneViewportSelectionInput(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			AshEngine::UIContext& refUi,
			const EditorViewportInputState& refInput,
			const EditorViewportInstance& refViewport,
			bool bGizmoConsumesMouseLeft,
			ViewportPanelSceneSelectionState& refSceneBoxSelectionState)
		{
			if (!refDeps.pSelectionService ||
				!refDeps.pSceneService ||
				!refDeps.pAssetDatabaseService)
			{
				refSceneBoxSelectionState = {};
				return;
			}

			UpdateSceneViewportPendingClickSelection(
				refDeps,
				strViewportId,
				refUi.get_time_seconds(),
				refSceneBoxSelectionState);

			const bool bCanStartSelection =
				refViewport.state.bContentHovered &&
				!bGizmoConsumesMouseLeft &&
				!refInput.IsModifierDown(AshEngine::UIModifierFlagBits::Alt) &&
				!refInput.IsMouseDown(AshEngine::UIMouseButton::Middle) &&
				!refInput.IsMouseDown(AshEngine::UIMouseButton::Right) &&
				!refUi.has_drag_drop_payload();
			const AshEngine::UIVec2 vecMousePosition = refInput.vecMouseScreenPosition;

			if (refInput.WasMousePressed(AshEngine::UIMouseButton::Left))
			{
				ClearScenePendingPick(refSceneBoxSelectionState);
				if (bCanStartSelection &&
					ViewportPanelSupport::IsPointInRect(refViewport.state.rectContent, vecMousePosition))
				{
					refSceneBoxSelectionState.bTracking = true;
					refSceneBoxSelectionState.bDragging = false;
					refSceneBoxSelectionState.vecStartScreen = vecMousePosition;
					refSceneBoxSelectionState.vecCurrentScreen = vecMousePosition;
					refSceneBoxSelectionState.uStartModifiers = refInput.uModifiers;
				}
				else
				{
					ClearSceneBoxSelectionTracking(refSceneBoxSelectionState);
				}
			}

			if (refSceneBoxSelectionState.bTracking && refInput.IsMouseDown(AshEngine::UIMouseButton::Left))
			{
				refSceneBoxSelectionState.vecCurrentScreen = vecMousePosition;
				const float fDragDistanceSquared = ViewportPanelSupport::DistanceSquared(
					refSceneBoxSelectionState.vecStartScreen,
					refSceneBoxSelectionState.vecCurrentScreen);
				if (fDragDistanceSquared >
					kSceneBoxSelectionDragThresholdPixels * kSceneBoxSelectionDragThresholdPixels)
				{
					refSceneBoxSelectionState.bDragging = true;
				}
			}

			if (!refSceneBoxSelectionState.bTracking ||
				!refInput.WasMouseReleased(AshEngine::UIMouseButton::Left))
			{
				return;
			}

			const AshEngine::UIRect rectSelection = ViewportPanelSupport::MakeRectFromPoints(
				refSceneBoxSelectionState.vecStartScreen,
				refSceneBoxSelectionState.vecCurrentScreen);
			if (refSceneBoxSelectionState.bDragging)
			{
				ViewportPanelSupport::ApplySceneViewportBoxSelection(
					refDeps,
					strViewportId,
					refViewport.state.rectContent,
					rectSelection,
					refSceneBoxSelectionState.uStartModifiers);
			}
			else
			{
				AshEngine::SceneViewBindingHandle pickBinding{};
				if (ViewportPanelSupport::RequestSceneViewportClickPick(
					refDeps,
					strViewportId,
					refViewport.state.rectContent,
					refSceneBoxSelectionState.vecStartScreen,
					pickBinding))
				{
					BeginSceneViewportPendingClickSelection(
						refSceneBoxSelectionState,
						strViewportId,
						pickBinding,
						refViewport.state.rectContent,
						refSceneBoxSelectionState.vecStartScreen,
						refSceneBoxSelectionState.uStartModifiers,
						refUi.get_time_seconds());
				}
				else
				{
					ViewportPanelSupport::ApplySceneViewportClickSelection(
						refDeps,
						strViewportId,
						refViewport.state.rectContent,
						refSceneBoxSelectionState.vecStartScreen,
						refSceneBoxSelectionState.uStartModifiers);
				}
			}

			ClearSceneBoxSelectionTracking(refSceneBoxSelectionState);
		}
	}

	namespace ViewportPanelInteraction
	{
		void HandleViewportInput(
			const EditorFrameContext& refFrameContext,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const EditorViewportInstance& refViewport,
			const AshEngine::UIRect& rectContent,
			bool bContentHovered,
			ViewportPanelSceneSelectionState& refSceneBoxSelectionState)
		{
			if (!refFrameContext.pUiContext ||
				!refDeps.pViewportService ||
				!refDeps.pViewportCameraService ||
				!refDeps.pSceneService ||
				!refDeps.pAssetDatabaseService)
			{
				return;
			}

			const EditorViewportPresentation* pPresentation = refDeps.pViewportService->GetPresentation(strViewportId);
			if (!pPresentation || pPresentation->eKind != EditorViewportKind::Scene)
			{
				refSceneBoxSelectionState = {};
				return;
			}
			if (rectContent.width <= 1.0f || rectContent.height <= 1.0f)
			{
				refSceneBoxSelectionState = {};
				return;
			}

			EditorViewportInputState inputState = BuildViewportInputState(*refFrameContext.pUiContext);

			EditorViewportCameraInputContext inputContext{};
			inputContext.strViewportId = strViewportId;
			inputContext.rectContent = rectContent;
			inputContext.bViewportFocused = refViewport.state.bFocused || bContentHovered;
			inputContext.bViewportHovered = bContentHovered;
			inputContext.bAcceptsInput = pPresentation->bAcceptsInput;
			if (refDeps.pSelectionService)
			{
				const EditorSelection& refSelection = refDeps.pSelectionService->GetSelection();
				inputContext.uFocusEntityId =
					refSelection.eKind == EditorSelectionKind::Entity
					? refSelection.uId
					: 0;
			}

			refDeps.pViewportCameraService->UpdateViewportInput(
				*refDeps.pSceneService,
				*refDeps.pAssetDatabaseService,
				inputState,
				refFrameContext.pUiContext->get_time_seconds(),
				inputContext);

			EditorGizmoService::InteractionResult gizmoInteraction{};
			if (pPresentation->bAcceptsInput && strViewportId == EditorViewportIds::Scene)
			{
				HandleSceneViewportModeShortcuts(
					*refFrameContext.pUiContext,
					inputState,
					bContentHovered || refViewport.state.bFocused,
					refDeps.pGizmoState);
				gizmoInteraction = ViewportPanelSupport::UpdateSceneGizmoInteraction(
					refDeps,
					*refFrameContext.pUiContext,
					inputState,
					bContentHovered,
					rectContent);
			}

			if (pPresentation->bAcceptsInput)
			{
				UpdateSceneViewportSelectionInput(
					refDeps,
					strViewportId,
					*refFrameContext.pUiContext,
					inputState,
					refViewport,
					gizmoInteraction.bConsumesMouseLeft,
					refSceneBoxSelectionState);
				return;
			}

			refSceneBoxSelectionState = {};
		}

		void DrawSceneBoxSelectionOverlay(
			AshEngine::UIContext& refUi,
			const AshEngine::UIRect& rectContent,
			const ViewportPanelSceneSelectionState& refSceneBoxSelectionState)
		{
			if (!refSceneBoxSelectionState.bTracking || !refSceneBoxSelectionState.bDragging)
			{
				return;
			}

			const AshEngine::UIRect rectSelection = ViewportPanelSupport::MakeRectFromPoints(
				refSceneBoxSelectionState.vecStartScreen,
				refSceneBoxSelectionState.vecCurrentScreen);
			if (!ViewportPanelSupport::RectsIntersect(rectContent, rectSelection))
			{
				return;
			}

			refUi.push_window_clip_rect(rectContent);
			refUi.draw_window_rect_filled(
				rectSelection,
				GetEditorRowSelectedFillColor(refUi),
				2.0f);
			refUi.draw_window_rect(
				rectSelection,
				GetEditorRowSelectedOutlineColor(refUi),
				2.0f,
				1.4f);
			refUi.pop_window_clip_rect();
		}

		void HandleDragDropTarget(
			AshEngine::UIContext& refUi,
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			const AshEngine::UIRect& rectViewportContent,
			bool bHasViewportContent)
		{
			if (!refDeps.pDragDropTransferService ||
				!bHasViewportContent ||
				!refUi.begin_drag_drop_target())
			{
				return;
			}

			const AshEngine::UIDragDropPayload payload =
				refUi.accept_drag_drop_payload(EditorDragPayloadTypes::SceneEntity);
			if (refDeps.pSelectionService &&
				payload.is_delivery &&
				payload.data &&
				payload.data_size == sizeof(DragDropTransferId))
			{
				DragDropTransferId uTransferId = 0;
				std::memcpy(&uTransferId, payload.data, sizeof(DragDropTransferId));
				const DragDropTransferData* pData = refDeps.pDragDropTransferService->Resolve(uTransferId);
				if (pData && !pData->vecEntityIds.empty())
				{
					EditorSelection sel{};
					sel.eKind = EditorSelectionKind::Entity;
					sel.uId = pData->vecEntityIds[0];
					refDeps.pSelectionService->Select(std::move(sel));
				}
			}

			if (strViewportId == EditorViewportIds::Scene)
			{
				const AshEngine::UIDragDropPayload assetPayload =
					refUi.accept_drag_drop_payload(EditorDragPayloadTypes::Asset);
				if (assetPayload.is_delivery &&
					assetPayload.data &&
					assetPayload.data_size == sizeof(DragDropTransferId))
				{
					DragDropTransferId uTransferId = 0;
					std::memcpy(&uTransferId, assetPayload.data, sizeof(DragDropTransferId));
					if (const DragDropTransferData* pData = refDeps.pDragDropTransferService->Resolve(uTransferId))
					{
						ViewportPanelSupport::HandleAssetDropInstantiate(
							refDeps,
							strViewportId,
							rectViewportContent,
							refUi.get_mouse_pos(),
							*pData);
					}
				}
			}

			refUi.end_drag_drop_target();
		}
	}
}

#include "Panels/ViewportPanelInteraction.h"

#include "Base/input/Input.h"
#include "Core/EditorIds.h"
#include "Core/EditorSelection.h"
#include "Function/Application.h"
#include "Function/Gui/UIContext.h"
#include "Panels/ViewportPanelSupport.h"
#include "Services/AssetDatabaseService.h"
#include "Services/DragDropTransferService.h"
#include "Services/EditorGizmoService.h"
#include "Services/EditorViewportCameraService.h"
#include "Services/EditorViewportService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <utility>

namespace AshEditor
{
	namespace
	{
		constexpr float kSceneBoxSelectionDragThresholdPixels = 5.0f;

		void HandleSceneViewportModeShortcuts(
			AshEngine::UIContext& refUi,
			const AshEngine::InputState& refInput,
			const bool bViewportHovered,
			EditorGizmoState* pGizmoState)
		{
			if (!pGizmoState || !bViewportHovered || refUi.is_any_item_active() || refUi.wants_text_input())
			{
				return;
			}

			const bool bHasModifiers =
				refInput.is_key_down(GLFW_KEY_LEFT_CONTROL) ||
				refInput.is_key_down(GLFW_KEY_RIGHT_CONTROL) ||
				refInput.is_key_down(GLFW_KEY_LEFT_SHIFT) ||
				refInput.is_key_down(GLFW_KEY_RIGHT_SHIFT) ||
				refInput.is_key_down(GLFW_KEY_LEFT_ALT) ||
				refInput.is_key_down(GLFW_KEY_RIGHT_ALT) ||
				refInput.is_key_down(GLFW_KEY_LEFT_SUPER) ||
				refInput.is_key_down(GLFW_KEY_RIGHT_SUPER);
			if (bHasModifiers)
			{
				return;
			}

			if (refInput.was_key_pressed(GLFW_KEY_W))
			{
				pGizmoState->eMode = GizmoMode::Move;
			}
			else if (refInput.was_key_pressed(GLFW_KEY_E))
			{
				pGizmoState->eMode = GizmoMode::Scale;
			}
			else if (refInput.was_key_pressed(GLFW_KEY_R))
			{
				pGizmoState->eMode = GizmoMode::Rotate;
			}
		}

		void UpdateSceneViewportSelectionInput(
			const ViewportPanelDeps& refDeps,
			const std::string& strViewportId,
			AshEngine::UIContext& refUi,
			const AshEngine::InputState& refInput,
			const EditorViewportInstance& refViewport,
			bool bGizmoConsumesMouseLeft,
			ViewportPanelSceneBoxSelectionState& refSceneBoxSelectionState)
		{
			if (!refDeps.pSelectionService ||
				!refDeps.pSceneService ||
				!refDeps.pAssetDatabaseService)
			{
				refSceneBoxSelectionState = {};
				return;
			}

			const bool bCanStartSelection =
				refViewport.state.bContentHovered &&
				!bGizmoConsumesMouseLeft &&
				!refInput.is_key_down(GLFW_KEY_LEFT_ALT) &&
				!refInput.is_key_down(GLFW_KEY_RIGHT_ALT) &&
				!refInput.is_mouse_button_down(GLFW_MOUSE_BUTTON_MIDDLE) &&
				!refInput.is_mouse_button_down(GLFW_MOUSE_BUTTON_RIGHT) &&
				!refUi.has_drag_drop_payload();
			const AshEngine::UIVec2 vecMousePosition = ViewportPanelSupport::GetMouseScreenPosition(refInput);

			if (refInput.was_mouse_button_pressed(GLFW_MOUSE_BUTTON_LEFT))
			{
				if (bCanStartSelection &&
					ViewportPanelSupport::IsPointInRect(refViewport.state.rectContent, vecMousePosition))
				{
					refSceneBoxSelectionState.bTracking = true;
					refSceneBoxSelectionState.bDragging = false;
					refSceneBoxSelectionState.vecStartScreen = vecMousePosition;
					refSceneBoxSelectionState.vecCurrentScreen = vecMousePosition;
					refSceneBoxSelectionState.uStartModifiers = refUi.get_key_modifiers();
				}
				else
				{
					refSceneBoxSelectionState = {};
				}
			}

			if (refSceneBoxSelectionState.bTracking && refInput.is_mouse_button_down(GLFW_MOUSE_BUTTON_LEFT))
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
				!refInput.was_mouse_button_released(GLFW_MOUSE_BUTTON_LEFT))
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
				ViewportPanelSupport::ApplySceneViewportClickSelection(
					refDeps,
					strViewportId,
					refViewport.state.rectContent,
					refSceneBoxSelectionState.vecStartScreen,
					refSceneBoxSelectionState.uStartModifiers);
			}

			refSceneBoxSelectionState = {};
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
			ViewportPanelSceneBoxSelectionState& refSceneBoxSelectionState)
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

			if (!AshEngine::Application::get())
			{
				return;
			}

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
				AshEngine::Application::get_input(),
				refFrameContext.pUiContext->get_time_seconds(),
				inputContext);

			const AshEngine::InputState& refInput = AshEngine::Application::get_input();
			EditorGizmoService::InteractionResult gizmoInteraction{};
			if (pPresentation->bAcceptsInput && strViewportId == EditorViewportIds::Scene)
			{
				HandleSceneViewportModeShortcuts(
					*refFrameContext.pUiContext,
					refInput,
					bContentHovered || refViewport.state.bFocused,
					refDeps.pGizmoState);
				gizmoInteraction = ViewportPanelSupport::UpdateSceneGizmoInteraction(
					refDeps,
					*refFrameContext.pUiContext,
					refInput,
					bContentHovered,
					rectContent);
			}

			if (pPresentation->bAcceptsInput)
			{
				UpdateSceneViewportSelectionInput(
					refDeps,
					strViewportId,
					*refFrameContext.pUiContext,
					refInput,
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
			const ViewportPanelSceneBoxSelectionState& refSceneBoxSelectionState)
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
				{ 0.28f, 0.56f, 0.94f, 0.16f },
				2.0f);
			refUi.draw_window_rect(
				rectSelection,
				{ 0.56f, 0.78f, 1.0f, 0.78f },
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

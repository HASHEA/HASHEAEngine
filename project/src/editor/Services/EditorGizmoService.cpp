#include "Services/EditorGizmoService.h"

#include "Base/input/Input.h"
#include "Core/EditorComponentComparison.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/AssetDatabaseService.h"
#include "Services/EditorGizmoMath.h"
#include "Services/EditorGizmoSelectionUtils.h"
#include "Services/EditorGizmoTransform.h"
#include "Services/EditorGizmoTypesInternal.h"
#include "Services/EditorSceneBoundsUtils.h"
#include "Services/MoveScaleGizmoTool.h"
#include "Services/RotateGizmoTool.h"
#include "Services/SceneService.h"
#include "Services/SelectionOverlayRenderer.h"
#include "Services/SelectionService.h"

#include <glm/glm.hpp>

#include <GLFW/glfw3.h>

#include <memory>
#include <vector>

namespace AshEditor
{
	namespace
	{
		using EditorGizmoInternal::DragSession;
		using EditorGizmoInternal::GizmoBasis;
		using EditorGizmoInternal::HandleHit;
		using EditorGizmoInternal::HandleKind;
		using EditorGizmoInternal::MoveGizmoVisual;
		using EditorGizmoInternal::RotateGizmoVisual;
		using EditorGizmoMath::NormalizeOrFallback;
		using EditorGizmoTransform::ComputeMovedTransform;
		using EditorGizmoTransform::ComputeRotatedTransform;
		using EditorGizmoTransform::ComputeScaledTransform;
		using EditorGizmoSelectionUtils::BuildSelectedTopLevelEntityIds;

		bool ExecuteDragUpdateCommand(
			const SceneService& refSceneService,
			IEditorCommandExecutor& refCommandExecutor,
			const EditorGizmoState& refGizmoState,
			const EditorGizmoInternal::DragSession& refDragSession,
			const EditorGizmoInternal::GizmoDragUpdate& refDragUpdate)
		{
			if (refDragSession.vecEntityIds.size() > 1u &&
				(refDragUpdate.bHasMoveWorldDelta ||
				 refDragUpdate.bHasScaleDelta ||
				 refDragUpdate.bHasRotateDelta))
			{
				std::vector<AshEngine::TransformComponent> vecAfterTransforms{};
				vecAfterTransforms.reserve(refDragSession.vecEntityIds.size());
				for (size_t uIndex = 0; uIndex < refDragSession.vecEntityIds.size(); ++uIndex)
				{
					if (refDragUpdate.bHasMoveWorldDelta)
					{
						vecAfterTransforms.push_back(ComputeMovedTransform(
							refSceneService,
							refDragSession.vecEntityIds[uIndex],
							refDragSession.vecBeforeTransforms[uIndex],
							refDragUpdate.vecMoveWorldDelta));
					}
					else if (refDragUpdate.bHasScaleDelta)
					{
						vecAfterTransforms.push_back(ComputeScaledTransform(
							refDragSession.vecBeforeTransforms[uIndex],
							refDragUpdate.vecScaleDeltaNormalized,
							refGizmoState));
					}
					else
					{
						vecAfterTransforms.push_back(ComputeRotatedTransform(
							refSceneService,
							refDragSession.vecEntityIds[uIndex],
							refDragSession.vecBeforeTransforms[uIndex],
							refDragSession.vecAxisDirection,
							refDragUpdate.fRotateDeltaDegrees));
					}
				}

				return refCommandExecutor.ExecuteCommand(
					std::make_unique<TransformEntitiesCommand>(
						refDragSession.vecEntityIds,
						refDragSession.vecBeforeTransforms,
						vecAfterTransforms));
			}

			return refCommandExecutor.ExecuteCommand(
				std::make_unique<TransformEntityCommand>(
					refDragSession.uEntityId,
					refDragSession.beforeTransform,
					refDragUpdate.afterTransform));
		}
	}

	EditorGizmoService::EditorGizmoService()
		: _upMoveScaleTool(std::make_unique<MoveScaleGizmoTool>())
		, _upRotateTool(std::make_unique<RotateGizmoTool>())
	{
	}

	EditorGizmoService::~EditorGizmoService() = default;

	EditorGizmoService::InteractionResult EditorGizmoService::UpdateSceneViewportInteraction(
		AshEngine::UIContext& refUi,
		const AshEngine::InputState& refInput,
		const bool bViewportHovered,
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const SelectionService& refSelectionService,
		IEditorCommandExecutor& refCommandExecutor,
		const EditorGizmoState& refGizmoState,
		const ViewportContext& refViewportContext)
	{
		InteractionResult result{};
		if (refGizmoState.eMode != GizmoMode::Move &&
			refGizmoState.eMode != GizmoMode::Scale &&
			refGizmoState.eMode != GizmoMode::Rotate)
		{
			CancelInteraction(refCommandExecutor);
			return result;
		}

		const bool bRotateMode = refGizmoState.eMode == GizmoMode::Rotate;
		if (bRotateMode)
		{
			_upMoveScaleTool->CancelInteraction(refCommandExecutor);
		}
		else
		{
			_upRotateTool->CancelInteraction(refCommandExecutor);
		}

		const DragSession& refDragSession =
			bRotateMode
			? _upRotateTool->GetDragSession()
			: _upMoveScaleTool->GetDragSession();
		if (refDragSession.bActive && refDragSession.eMode != refGizmoState.eMode)
		{
			CancelInteraction(refCommandExecutor);
			return result;
		}

		GizmoBasis basis{};
		if (!TryBuildGizmoBasis(
			refSceneService,
			refAssetDatabaseService,
			refSelectionService,
			refGizmoState,
			basis))
		{
			CancelInteraction(refCommandExecutor);
			return result;
		}

		MoveGizmoVisual moveVisual{};
		RotateGizmoVisual rotateVisual{};
		const bool bHasVisual =
			bRotateMode
			? RotateGizmoTool::TryBuildVisual(refViewportContext, basis, rotateVisual)
			: MoveScaleGizmoTool::TryBuildVisual(refViewportContext, basis, moveVisual);
		if (!bHasVisual)
		{
			CancelInteraction(refCommandExecutor);
			return result;
		}

		if (!bViewportHovered && !refDragSession.bActive)
		{
			if (bRotateMode)
			{
				_upRotateTool->ClearHoveredHandle();
			}
			else
			{
				_upMoveScaleTool->ClearHoveredHandle();
			}
			return result;
		}

		const glm::vec2 vecMousePosition{
			static_cast<float>(refInput.get_mouse_x()),
			static_cast<float>(refInput.get_mouse_y())
		};
		const bool bLeftPressed = refInput.was_mouse_button_pressed(GLFW_MOUSE_BUTTON_LEFT);
		const bool bLeftReleased = refInput.was_mouse_button_released(GLFW_MOUSE_BUTTON_LEFT);
		const bool bLeftDown = refInput.is_mouse_button_down(GLFW_MOUSE_BUTTON_LEFT);
		const bool bRightDown = refInput.is_mouse_button_down(GLFW_MOUSE_BUTTON_RIGHT);

		if (refDragSession.bActive)
		{
			result.bConsumesMouseLeft = true;
			result.bInteractionActive = true;
			result.bHovered = true;

			if (refDragSession.uEntityId != basis.uEntityId)
			{
				CancelInteraction(refCommandExecutor);
				return result;
			}

			if (bLeftReleased || !bLeftDown)
			{
				if (refDragSession.bTransactionOpened)
				{
					refCommandExecutor.CommitCommandTransaction();
				}
				ResetInteraction();
				return result;
			}

			EditorGizmoInternal::GizmoDragUpdate dragUpdate{};
			const bool bDragUpdated =
				bRotateMode
				? _upRotateTool->TryUpdateDrag(
					refViewportContext,
					refSceneService,
					refGizmoState,
					vecMousePosition,
					dragUpdate)
				: _upMoveScaleTool->TryUpdateDrag(
					refViewportContext,
					refSceneService,
					refGizmoState,
					vecMousePosition,
					dragUpdate);
			if (!bDragUpdated || !dragUpdate.bHasTransform)
			{
				return result;
			}

			if (TransformComponentsEqual(refDragSession.beforeTransform, dragUpdate.afterTransform))
			{
				return result;
			}

			if (!ExecuteDragUpdateCommand(
				refSceneService,
				refCommandExecutor,
				refGizmoState,
				refDragSession,
				dragUpdate))
			{
				CancelInteraction(refCommandExecutor);
			}
			return result;
		}

		HandleHit hoveredHandle =
			!refUi.has_drag_drop_payload() && !bRightDown
			? (bRotateMode
				? RotateGizmoTool::HitTestHandle(rotateVisual, vecMousePosition)
				: MoveScaleGizmoTool::HitTestHandle(moveVisual, true, vecMousePosition))
			: HandleHit{};
		if (hoveredHandle.eKind == HandleKind::Axis && hoveredHandle.iPrimaryAxis < 0)
		{
			hoveredHandle = {};
		}
		if (bRotateMode)
		{
			_upRotateTool->SetHoveredHandle(hoveredHandle);
		}
		else
		{
			_upMoveScaleTool->SetHoveredHandle(hoveredHandle);
		}
		result.bHovered = hoveredHandle.IsValid();
		result.bConsumesMouseLeft = result.bHovered;

		if (!bLeftPressed || !hoveredHandle.IsValid())
		{
			return result;
		}

		DragSession nextDragSession{};
		const bool bCanBeginDrag =
			bRotateMode
			? _upRotateTool->TryBuildDragSession(
				refViewportContext,
				refSceneService,
				refSelectionService,
				basis,
				rotateVisual,
				hoveredHandle,
				refGizmoState,
				vecMousePosition,
				nextDragSession)
			: _upMoveScaleTool->TryBuildDragSession(
				refViewportContext,
				refSceneService,
				refSelectionService,
				basis,
				moveVisual,
				hoveredHandle,
				refGizmoState,
				vecMousePosition,
				nextDragSession);
		if (!bCanBeginDrag)
		{
			return result;
		}

		if (!refCommandExecutor.BeginCommandTransaction(
			refGizmoState.eMode == GizmoMode::Scale
			? "Scale Gizmo"
			: (refGizmoState.eMode == GizmoMode::Rotate ? "Rotate Gizmo" : "Move Gizmo")))
		{
			result.bConsumesMouseLeft = true;
			return result;
		}

		if (bRotateMode)
		{
			_upRotateTool->BeginDragSession(nextDragSession, true);
		}
		else
		{
			_upMoveScaleTool->BeginDragSession(nextDragSession, true);
		}

		result.bConsumesMouseLeft = true;
		result.bInteractionActive = true;
		return result;
	}

	void EditorGizmoService::DrawSceneViewportGizmo(
		AshEngine::UIContext& refUi,
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const SelectionService& refSelectionService,
		const EditorGizmoState& refGizmoState,
		const bool bDrawSelectionHelpers,
		const ViewportContext& refViewportContext)
	{
		GizmoBasis basis{};
		if (!TryBuildGizmoBasis(
			refSceneService,
			refAssetDatabaseService,
			refSelectionService,
			refGizmoState,
			basis))
		{
			return;
		}

		if (bDrawSelectionHelpers)
		{
			SelectionOverlayRenderer::Draw(
				refUi,
				refSceneService,
				refAssetDatabaseService,
				refSelectionService,
				refViewportContext);
		}

		const HandleHit hoveredHandle =
			refGizmoState.eMode == GizmoMode::Rotate
			? _upRotateTool->GetHoveredHandle()
			: ((refGizmoState.eMode == GizmoMode::Move || refGizmoState.eMode == GizmoMode::Scale)
				? _upMoveScaleTool->GetHoveredHandle()
				: HandleHit{});
		const DragSession& refToolDragSession =
			refGizmoState.eMode == GizmoMode::Rotate
			? _upRotateTool->GetDragSession()
			: _upMoveScaleTool->GetDragSession();
		const DragSession activeDragSession =
			refToolDragSession.bActive &&
			refToolDragSession.uEntityId == basis.uEntityId &&
			refToolDragSession.eMode == refGizmoState.eMode
			? refToolDragSession
			: DragSession{};

		switch (refGizmoState.eMode)
		{
		case GizmoMode::Move:
		{
			MoveGizmoVisual visual{};
			if (!MoveScaleGizmoTool::TryBuildVisual(refViewportContext, basis, visual))
			{
				return;
			}
			MoveScaleGizmoTool::DrawMove(refUi, refViewportContext, basis, visual, hoveredHandle, activeDragSession);
			break;
		}
		case GizmoMode::Scale:
		{
			MoveGizmoVisual visual{};
			if (!MoveScaleGizmoTool::TryBuildVisual(refViewportContext, basis, visual))
			{
				return;
			}
			MoveScaleGizmoTool::DrawScale(refUi, refViewportContext, basis, visual, hoveredHandle, activeDragSession);
			break;
		}
		case GizmoMode::Rotate:
		{
			RotateGizmoVisual visual{};
			if (!RotateGizmoTool::TryBuildVisual(refViewportContext, basis, visual))
			{
				return;
			}
			RotateGizmoTool::Draw(refUi, refViewportContext, visual, hoveredHandle, activeDragSession);
			break;
		}
		default:
			break;
		}
	}

	bool EditorGizmoService::TryBuildGizmoBasis(
		const SceneService& refSceneService,
		const AssetDatabaseService& refAssetDatabaseService,
		const SelectionService& refSelectionService,
		const EditorGizmoState& refGizmoState,
		GizmoBasis& outBasis)
	{
		outBasis = {};
		const EditorSelection& refSelection = refSelectionService.GetSelection();
		if (refSelection.eKind != EditorSelectionKind::Entity || refSelection.uId == 0)
		{
			return false;
		}

		const AshEngine::Entity entity = refSceneService.FindEntity(refSelection.uId);
		if (!entity.is_valid())
		{
			return false;
		}

		outBasis.uEntityId = refSelection.uId;
		const glm::mat4 matWorldTransform = refSceneService.GetActiveScene().get_entity_world_transform(refSelection.uId);
		outBasis.vecOrigin = glm::vec3(matWorldTransform[3]);
		const std::vector<SceneEntityId> vecSelectedEntityIds =
			BuildSelectedTopLevelEntityIds(refSceneService, refSelectionService);
		if (vecSelectedEntityIds.size() > 1u)
		{
			glm::vec3 vecOriginSum{ 0.0f };
			uint32_t uValidOriginCount = 0;
			for (const SceneEntityId uEntityId : vecSelectedEntityIds)
			{
				const AshEngine::Entity entitySelected = refSceneService.FindEntity(uEntityId);
				if (!entitySelected.is_valid())
				{
					continue;
				}

				const glm::mat4 matSelectedWorldTransform =
					refSceneService.GetActiveScene().get_entity_world_transform(uEntityId);
				vecOriginSum += glm::vec3(matSelectedWorldTransform[3]);
				++uValidOriginCount;
			}
			if (uValidOriginCount > 0)
			{
				outBasis.vecOrigin = vecOriginSum / static_cast<float>(uValidOriginCount);
			}
		}
		if (refGizmoState.ePivot == GizmoPivotMode::Center)
		{
			AshEngine::SceneWorldBounds bounds{};
			if (EditorSceneBoundsUtils::TryBuildMergedSubtreeWorldBounds(
				refSceneService,
				refAssetDatabaseService,
				vecSelectedEntityIds,
				bounds))
			{
				outBasis.vecOrigin = bounds.center;
			}
		}
		if (refGizmoState.eSpace == GizmoCoordinateSpace::Local || refGizmoState.eMode == GizmoMode::Scale)
		{
			outBasis.vecAxes[0] = NormalizeOrFallback(glm::vec3(matWorldTransform[0]), glm::vec3(1.0f, 0.0f, 0.0f));
			outBasis.vecAxes[1] = NormalizeOrFallback(glm::vec3(matWorldTransform[1]), glm::vec3(0.0f, 1.0f, 0.0f));
			outBasis.vecAxes[2] = NormalizeOrFallback(glm::vec3(matWorldTransform[2]), glm::vec3(0.0f, 0.0f, 1.0f));
		}
		outBasis.bValid = true;
		return true;
	}

	void EditorGizmoService::ResetInteraction()
	{
		_upMoveScaleTool->ResetInteraction();
		_upRotateTool->ResetInteraction();
	}

	void EditorGizmoService::CancelInteraction(IEditorCommandExecutor& refCommandExecutor)
	{
		_upMoveScaleTool->CancelInteraction(refCommandExecutor);
		_upRotateTool->CancelInteraction(refCommandExecutor);
	}
}

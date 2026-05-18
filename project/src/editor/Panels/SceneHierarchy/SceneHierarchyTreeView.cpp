#include "Panels/SceneHierarchy/SceneHierarchyTreeView.h"

#include "Base/hlog.h"
#include "Core/EditorCommand.h"
#include "Core/EntityCommands.h"
#include "Core/EditorIds.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"
#include "Services/DragDropTransferService.h"
#include "Services/IEditorIconService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
		struct SceneHierarchyDropRequest
		{
			SceneEntityId uSceneEntityId = 0;
			SceneEntityId uNewParentId = 0;
			uint32_t uSiblingIndex = kSceneAppendSiblingIndex;
			EditorTreeDropVisual eVisual = EditorTreeDropVisual::None;
			bool bChangesScene = false;
		};

		struct SceneEntityDropValidationData
		{
			const SceneService* pSceneService = nullptr;
			const DragDropTransferService* pDragDropTransferService = nullptr;
			SceneEntityId uTargetSceneEntityId = 0;
			bool bRootAppendSlot = false;
		};

		EditorIconId GetEntityIconId(const AshEngine::Entity& refEntity)
		{
			if (refEntity.has_camera_component())
			{
				return EditorIconId::EntityCamera;
			}
			if (refEntity.has_light_component())
			{
				const AshEngine::LightComponent& refLightComponent = refEntity.get_light_component();
				switch (refLightComponent.type)
				{
				case AshEngine::LightType::Directional:
					return EditorIconId::EntityLightDirectional;
				case AshEngine::LightType::Point:
					return EditorIconId::EntityLightPoint;
				case AshEngine::LightType::Spot:
					return EditorIconId::EntityLightSpot;
				default:
					break;
				}
			}
			if (refEntity.has_mesh_component())
			{
				return EditorIconId::EntityMesh;
			}

			return refEntity.get_parent().is_valid() ? EditorIconId::EntityActor : EditorIconId::EntityScene;
		}

		std::vector<uint64_t> DecodeDraggedSceneEntityIds(
			const DragDropTransferService* pService,
			const DragDropTransferId uTransferId)
		{
			if (!pService || uTransferId == 0)
			{
				return {};
			}

			const DragDropTransferData* pData = pService->Resolve(uTransferId);
			return pData ? pData->vecEntityIds : std::vector<uint64_t>{};
		}

		uint32_t CountDraggedSiblingsBeforeIndex(
			const SceneService& refSceneService,
			const std::vector<SceneEntityId>& vecDraggedEntityIds,
			const SceneEntityId uParentId,
			const uint32_t uSiblingIndex)
		{
			uint32_t uCount = 0;
			for (const SceneEntityId uDraggedEntityId : vecDraggedEntityIds)
			{
				const AshEngine::Entity entityDragged = refSceneService.FindEntity(uDraggedEntityId);
				if (entityDragged.is_valid() &&
					GetEntityParentId(entityDragged) == uParentId &&
					refSceneService.GetEntitySiblingIndex(uDraggedEntityId) < uSiblingIndex)
				{
					++uCount;
				}
			}
			return uCount;
		}

		void SortDraggedEntityIdsForStableSiblingInsert(
			const SceneService& refSceneService,
			const SceneEntityId uTargetParentId,
			std::vector<SceneEntityId>& vecDraggedEntityIds)
		{
			const bool bHasDraggedEntityFromTargetParent =
				std::any_of(
					vecDraggedEntityIds.begin(),
					vecDraggedEntityIds.end(),
					[&refSceneService, uTargetParentId](const SceneEntityId uDraggedEntityId)
					{
						const AshEngine::Entity entityDragged = refSceneService.FindEntity(uDraggedEntityId);
						return entityDragged.is_valid() && GetEntityParentId(entityDragged) == uTargetParentId;
					});
			if (!bHasDraggedEntityFromTargetParent)
			{
				return;
			}

			std::stable_sort(
				vecDraggedEntityIds.begin(),
				vecDraggedEntityIds.end(),
				[&refSceneService](const SceneEntityId uLeftEntityId, const SceneEntityId uRightEntityId)
				{
					const AshEngine::Entity entityLeft = refSceneService.FindEntity(uLeftEntityId);
					const AshEngine::Entity entityRight = refSceneService.FindEntity(uRightEntityId);
					const SceneEntityId uLeftParentId = GetEntityParentId(entityLeft);
					const SceneEntityId uRightParentId = GetEntityParentId(entityRight);
					if (uLeftParentId != uRightParentId)
					{
						return uLeftParentId < uRightParentId;
					}
					return
						refSceneService.GetEntitySiblingIndex(uLeftEntityId) <
						refSceneService.GetEntitySiblingIndex(uRightEntityId);
				});
		}

		std::vector<SceneHierarchyDropRequest> BuildStableSiblingDropRequests(
			const SceneService& refSceneService,
			std::vector<SceneEntityId> vecTopLevelDraggedEntityIds,
			const SceneEntityId uNewParentId,
			const uint32_t uBaseSiblingIndex,
			const EditorTreeDropVisual eVisual)
		{
			if (vecTopLevelDraggedEntityIds.empty())
			{
				return {};
			}

			SortDraggedEntityIdsForStableSiblingInsert(refSceneService, uNewParentId, vecTopLevelDraggedEntityIds);
			std::vector<SceneHierarchyDropRequest> vecRequests{};
			vecRequests.reserve(vecTopLevelDraggedEntityIds.size());
			uint32_t uInsertOffset = 0;
			bool bHasSceneChange = false;
			for (const SceneEntityId uDraggedEntityId : vecTopLevelDraggedEntityIds)
			{
				const AshEngine::Entity entityDragged = refSceneService.FindEntity(uDraggedEntityId);
				if (!entityDragged.is_valid() || !refSceneService.CanReparentEntity(uDraggedEntityId, uNewParentId))
				{
					return {};
				}

				const SceneEntityId uCurrentParentId = GetEntityParentId(entityDragged);
				const uint32_t uCurrentSiblingIndex = refSceneService.GetEntitySiblingIndex(uDraggedEntityId);
				SceneHierarchyDropRequest request{};
				request.uSceneEntityId = uDraggedEntityId;
				request.uNewParentId = uNewParentId;
				request.uSiblingIndex = uBaseSiblingIndex + uInsertOffset;
				request.eVisual = eVisual;
				request.bChangesScene =
					uCurrentParentId != request.uNewParentId ||
					uCurrentSiblingIndex != request.uSiblingIndex;
				bHasSceneChange = bHasSceneChange || request.bChangesScene;
				vecRequests.push_back(request);
				++uInsertOffset;
			}

			return bHasSceneChange ? vecRequests : std::vector<SceneHierarchyDropRequest>{};
		}

		std::vector<SceneHierarchyDropRequest> BuildDropRequestsForTarget(
			const SceneService& refSceneService,
			const std::vector<uint64_t>& vecDraggedEntityIds,
			const AshEngine::Entity& refTargetEntity,
			const EditorTreeDropVisual eVisual)
		{
			if (!refTargetEntity.is_valid())
			{
				return {};
			}

			std::vector<SceneEntityId> vecTopLevelDraggedEntityIds =
				refSceneService.BuildTopLevelEntityIds(vecDraggedEntityIds);
			if (vecTopLevelDraggedEntityIds.empty() ||
				std::find(vecTopLevelDraggedEntityIds.begin(), vecTopLevelDraggedEntityIds.end(), refTargetEntity.get_id()) !=
					vecTopLevelDraggedEntityIds.end())
			{
				return {};
			}

			SceneEntityId uNewParentId = 0;
			uint32_t uBaseSiblingIndex = 0;
			if (eVisual == EditorTreeDropVisual::Before || eVisual == EditorTreeDropVisual::After)
			{
				uNewParentId = GetEntityParentId(refTargetEntity);
				const uint32_t uTargetSiblingIndex = refSceneService.GetEntitySiblingIndex(refTargetEntity.get_id());
				const uint32_t uRawInsertIndex = uTargetSiblingIndex + (eVisual == EditorTreeDropVisual::After ? 1u : 0u);
				const uint32_t uRemovedBeforeInsert = CountDraggedSiblingsBeforeIndex(
					refSceneService,
					vecTopLevelDraggedEntityIds,
					uNewParentId,
					uRawInsertIndex);
				uBaseSiblingIndex =
					uRawInsertIndex >= uRemovedBeforeInsert
					? uRawInsertIndex - uRemovedBeforeInsert
					: 0u;
			}
			else if (eVisual == EditorTreeDropVisual::Into)
			{
				uNewParentId = refTargetEntity.get_id();
				const uint32_t uChildCount = GetParentChildCount(refSceneService, uNewParentId);
				const uint32_t uRemovedBeforeAppend = CountDraggedSiblingsBeforeIndex(
					refSceneService,
					vecTopLevelDraggedEntityIds,
					uNewParentId,
					uChildCount);
				uBaseSiblingIndex =
					uChildCount >= uRemovedBeforeAppend
					? uChildCount - uRemovedBeforeAppend
					: 0u;
			}
			else
			{
				return {};
			}

			return BuildStableSiblingDropRequests(
				refSceneService,
				std::move(vecTopLevelDraggedEntityIds),
				uNewParentId,
				uBaseSiblingIndex,
				eVisual);
		}

		std::vector<SceneHierarchyDropRequest> BuildRootAppendDropRequests(
			const SceneService& refSceneService,
			const std::vector<uint64_t>& vecDraggedEntityIds)
		{
			std::vector<SceneEntityId> vecTopLevelDraggedEntityIds =
				refSceneService.BuildTopLevelEntityIds(vecDraggedEntityIds);
			if (vecTopLevelDraggedEntityIds.empty())
			{
				return {};
			}

			const SceneEntityId uNewParentId = 0;
			const uint32_t uRootCount = GetParentChildCount(refSceneService, uNewParentId);
			const uint32_t uRemovedBeforeAppend = CountDraggedSiblingsBeforeIndex(
				refSceneService,
				vecTopLevelDraggedEntityIds,
				uNewParentId,
				uRootCount);
			const uint32_t uBaseSiblingIndex =
				uRootCount >= uRemovedBeforeAppend
				? uRootCount - uRemovedBeforeAppend
				: 0u;
			return BuildStableSiblingDropRequests(
				refSceneService,
				std::move(vecTopLevelDraggedEntityIds),
				uNewParentId,
				uBaseSiblingIndex,
				EditorTreeDropVisual::Before);
		}

		bool ExecuteSceneHierarchyDropRequests(
			IEditorCommandExecutor& refCommandExecutor,
			const std::vector<SceneHierarchyDropRequest>& vecRequests)
		{
			if (vecRequests.empty())
			{
				return false;
			}

			std::vector<SceneHierarchyDropRequest> vecChangingRequests{};
			vecChangingRequests.reserve(vecRequests.size());
			for (const SceneHierarchyDropRequest& refRequest : vecRequests)
			{
				if (refRequest.bChangesScene)
				{
					vecChangingRequests.push_back(refRequest);
				}
			}
			if (vecChangingRequests.empty())
			{
				return false;
			}

			if (vecChangingRequests.size() == 1u)
			{
				const SceneHierarchyDropRequest& refRequest = vecChangingRequests.front();
				return refCommandExecutor.ExecuteCommand(
					std::make_unique<ReparentEntityCommand>(
						refRequest.uSceneEntityId,
						refRequest.uNewParentId,
						refRequest.uSiblingIndex));
			}

			std::unique_ptr<CompositeCommand> upCommand = std::make_unique<CompositeCommand>("Reparent Entities");
			for (const SceneHierarchyDropRequest& refRequest : vecChangingRequests)
			{
				upCommand->Append(
					std::make_unique<ReparentEntityCommand>(
						refRequest.uSceneEntityId,
						refRequest.uNewParentId,
						refRequest.uSiblingIndex));
			}
			return refCommandExecutor.ExecuteCommand(std::move(upCommand));
		}

		void RestoreSelectionForDropRequests(
			SelectionService* pSelectionService,
			const SceneService& refSceneService,
			const std::vector<SceneHierarchyDropRequest>& vecRequests)
		{
			if (!pSelectionService || vecRequests.size() <= 1u)
			{
				return;
			}

			std::vector<EditorSelection> vecSelections{};
			vecSelections.reserve(vecRequests.size());
			for (const SceneHierarchyDropRequest& refRequest : vecRequests)
			{
				const AshEngine::Entity entity = refSceneService.FindEntity(refRequest.uSceneEntityId);
				if (entity.is_valid())
				{
					vecSelections.push_back({
						EditorSelectionKind::Entity,
						entity.get_id(),
						entity.get_name(),
						{}
					});
				}
			}
			pSelectionService->SelectRange(vecSelections);
		}

		bool ValidateSceneDropTarget(
			const DragDropTransferId uTransferId,
			const EditorTreeDropVisual eVisual,
			void* pUserData)
		{
			const SceneEntityDropValidationData* pValidationData =
				static_cast<const SceneEntityDropValidationData*>(pUserData);
			if (!pValidationData || !pValidationData->pSceneService)
			{
				return false;
			}

			const std::vector<uint64_t> vecDraggedSceneEntityIds =
				DecodeDraggedSceneEntityIds(pValidationData->pDragDropTransferService, uTransferId);
			if (pValidationData->bRootAppendSlot)
			{
				return !BuildRootAppendDropRequests(*pValidationData->pSceneService, vecDraggedSceneEntityIds).empty();
			}

			const AshEngine::Entity entityTarget =
				pValidationData->pSceneService->FindEntity(pValidationData->uTargetSceneEntityId);
			return !BuildDropRequestsForTarget(
				*pValidationData->pSceneService,
				vecDraggedSceneEntityIds,
				entityTarget,
				eVisual).empty();
		}

		void HandleRootAppendDropTarget(
			EditorTreeWidget& refTreeWidget,
			const SceneHierarchyPanelDeps& refDeps,
			const bool bDraggingSceneEntity)
		{
			if (!refDeps.pSceneService)
			{
				return;
			}

			SceneEntityDropValidationData validationData{};
			validationData.pSceneService = refDeps.pSceneService;
			validationData.pDragDropTransferService = refDeps.pDragDropTransferService;
			validationData.bRootAppendSlot = true;

			EditorTreeDropTargetDesc dropTarget{};
			dropTarget.pPayloadType = EditorDragPayloadTypes::SceneEntity;
			dropTarget.pfnValidateDrop = ValidateSceneDropTarget;
			dropTarget.pValidationUserData = &validationData;

			EditorTreeDropSlotDesc slotDesc{};
			slotDesc.svUniqueId = "__scene_hierarchy_root_append__";
			slotDesc.fHeight = 18.0f;
			slotDesc.bExpandToAvailableHeightWhileDragging = bDraggingSceneEntity;
			slotDesc.ePreviewVisual = EditorTreeDropVisual::Before;
			slotDesc.pDropTarget = &dropTarget;

			const EditorTreeDropSlotResult slotResult = refTreeWidget.DrawDropSlot(slotDesc, bDraggingSceneEntity);
			if (slotResult.bDropDelivered &&
				slotResult.uDropTransferId != 0 &&
				refDeps.pCommandExecutor &&
				refDeps.pDragDropTransferService)
			{
				const std::vector<uint64_t> vecDraggedEntityIds = DecodeDraggedSceneEntityIds(
					refDeps.pDragDropTransferService,
					slotResult.uDropTransferId);
				const std::vector<SceneHierarchyDropRequest> vecRequests =
					BuildRootAppendDropRequests(*refDeps.pSceneService, vecDraggedEntityIds);
				if (!vecRequests.empty())
				{
					const bool bExecuted = ExecuteSceneHierarchyDropRequests(*refDeps.pCommandExecutor, vecRequests);
					if (!bExecuted)
					{
						HLogWarning(
							"SceneHierarchy failed to apply root drop reparent for {} entities.",
							static_cast<unsigned long long>(vecRequests.size()));
					}
					else
					{
						RestoreSelectionForDropRequests(refDeps.pSelectionService, *refDeps.pSceneService, vecRequests);
					}
				}
			}
		}

		void DrawEntityTreeRecursive(
			EditorTreeWidget& refTreeWidget,
			const EditorFrameContext& refFrameContext,
			const SceneHierarchyPanelDeps& refDeps,
			SceneHierarchyPanelState& refState,
			const AshEngine::Entity& refEntity,
			const std::vector<SceneEntityId>& vecVisibleEntityIds,
			const bool bIsLastSibling)
		{
			if (!refDeps.pSceneService)
			{
				return;
			}

			const std::vector<AshEngine::Entity> vecChildren = refEntity.get_children();
			const bool bHasChildren = !vecChildren.empty();
			const std::string strEntityName = refEntity.get_name();
			const bool bSelected =
				refDeps.pSelectionService &&
				refDeps.pSelectionService->IsSelected(EditorSelectionKind::Entity, refEntity.get_id());

			AshEngine::UITextureHandle pIconClosed = nullptr;
			AshEngine::UITextureHandle pIconOpen = nullptr;
			if (refDeps.pIconService && refFrameContext.pUiContext)
			{
				pIconClosed = refDeps.pIconService->GetIcon(GetEntityIconId(refEntity), *refFrameContext.pUiContext);
				pIconOpen = pIconClosed;
			}

			const SceneEntityId uSceneEntityId = refEntity.get_id();
			SceneEntityDropValidationData validationData{};
			validationData.pSceneService = refDeps.pSceneService;
			validationData.pDragDropTransferService = refDeps.pDragDropTransferService;
			validationData.uTargetSceneEntityId = uSceneEntityId;

			std::vector<uint64_t> vecDraggedEntityIds{};
			if (refDeps.pSelectionService && bSelected && refDeps.pSelectionService->HasMultipleSelections())
			{
				vecDraggedEntityIds = refDeps.pSelectionService->GetSelectedIds(EditorSelectionKind::Entity);
			}
			if (vecDraggedEntityIds.empty())
			{
				vecDraggedEntityIds.push_back(uSceneEntityId);
			}

			std::string strDragPreviewText = strEntityName;
			if (vecDraggedEntityIds.size() > 1u)
			{
				strDragPreviewText = std::to_string(vecDraggedEntityIds.size()) + " Entities";
			}

			const DragDropTransferId uDragTransferId = refDeps.pDragDropTransferService
				? refDeps.pDragDropTransferService->Register(DragDropTransferData{ "SceneEntity", vecDraggedEntityIds, {} })
				: 0;
			const EditorTreeDragSourceDesc dragSource{
				EditorDragPayloadTypes::SceneEntity,
				uDragTransferId,
				strDragPreviewText.c_str()
			};

			EditorTreeDropTargetDesc dropTarget{};
			dropTarget.pPayloadType = EditorDragPayloadTypes::SceneEntity;
			dropTarget.bAllowBefore = true;
			dropTarget.bAllowAfter = true;
			dropTarget.bAllowInto = true;
			dropTarget.bAutoExpandOnIntoHover = true;
			dropTarget.pfnValidateDrop = ValidateSceneDropTarget;
			dropTarget.pValidationUserData = &validationData;

			const std::string strUniqueId = std::to_string(uSceneEntityId);
			EditorTreeItemDesc itemDesc{};
			itemDesc.svUniqueId = strUniqueId;
			itemDesc.svLabel = strEntityName;
			itemDesc.pIcon = pIconClosed;
			itemDesc.pIconWhenOpen = pIconOpen;
			itemDesc.bIsSelected = bSelected;
			itemDesc.bHasChildren = bHasChildren;
			itemDesc.bIsLastSibling = bIsLastSibling;
			itemDesc.pDragSource = &dragSource;
			itemDesc.pDropTarget = &dropTarget;

			const EditorTreeItemResult itemResult = refTreeWidget.DrawItem(itemDesc);
			if (itemResult.bClicked && refDeps.pSelectionService)
			{
				SelectEntityFromHierarchy(refFrameContext, refDeps, refState, refEntity, vecVisibleEntityIds);
			}
			if (refFrameContext.pUiContext &&
				refFrameContext.pUiContext->is_item_clicked(AshEngine::UIMouseButton::Right) &&
				refDeps.pSelectionService &&
				!refDeps.pSelectionService->IsSelected(EditorSelectionKind::Entity, uSceneEntityId))
			{
				refDeps.pSelectionService->SelectSingle({
					EditorSelectionKind::Entity,
					uSceneEntityId,
					strEntityName,
					{}
				});
				refState.uRangeSelectionAnchorEntityId = uSceneEntityId;
			}
			DrawEntityContextMenu(refFrameContext, refDeps, refState, refEntity);

			if (itemResult.bDropDelivered &&
				itemResult.uDropTransferId != 0 &&
				refDeps.pCommandExecutor &&
				refDeps.pDragDropTransferService)
			{
				const std::vector<uint64_t> vecDroppedEntityIds = DecodeDraggedSceneEntityIds(
					refDeps.pDragDropTransferService,
					itemResult.uDropTransferId);
				const std::vector<SceneHierarchyDropRequest> vecRequests = BuildDropRequestsForTarget(
					*refDeps.pSceneService,
					vecDroppedEntityIds,
					refEntity,
					itemResult.eDropVisual);
				if (!vecRequests.empty())
				{
					const bool bExecuted = ExecuteSceneHierarchyDropRequests(*refDeps.pCommandExecutor, vecRequests);
					if (!bExecuted)
					{
						HLogWarning(
							"SceneHierarchy failed to apply drop reparent for {} entities.",
							static_cast<unsigned long long>(vecRequests.size()));
					}
					else
					{
						RestoreSelectionForDropRequests(refDeps.pSelectionService, *refDeps.pSceneService, vecRequests);
					}
				}
			}

			if (!itemResult.bOpened)
			{
				return;
			}

			if (bHasChildren)
			{
				refTreeWidget.PushLevel(!bIsLastSibling);
				for (size_t uChildIndex = 0; uChildIndex < vecChildren.size(); ++uChildIndex)
				{
					DrawEntityTreeRecursive(
						refTreeWidget,
						refFrameContext,
						refDeps,
						refState,
						vecChildren[uChildIndex],
						vecVisibleEntityIds,
						uChildIndex + 1 == vecChildren.size());
				}
				refTreeWidget.PopLevel();
			}
			refTreeWidget.TreePop();
		}
	}

	void SceneHierarchyTreeView::Draw(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState,
		AshEngine::Scene& refScene) const
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		const std::vector<SceneEntityId> vecVisibleEntityIds = BuildVisibleEntityIds(refScene);
		const bool bDraggingSceneEntity = IsSceneEntityDragActive(refFrameContext.pUiContext);

		EditorTreeWidget treeWidget(refUi, refState.treeWidgetStateEntities, MakeSceneTreeStyle());
		treeWidget.ResetDragStateIfInactive();

		const std::vector<AshEngine::Entity> vecRoots = refScene.get_root_entities();
		for (size_t uRootIndex = 0; uRootIndex < vecRoots.size(); ++uRootIndex)
		{
			DrawEntityTreeRecursive(
				treeWidget,
				refFrameContext,
				refDeps,
				refState,
				vecRoots[uRootIndex],
				vecVisibleEntityIds,
				uRootIndex + 1 == vecRoots.size());
		}

		HandleRootAppendDropTarget(treeWidget, refDeps, bDraggingSceneEntity);
	}
}

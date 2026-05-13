#include "Panels/SceneHierarchyPanel.h"

#include "Base/hlog.h"
#include "Core/EditorCommand.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Core/EditorStringUtils.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Services/CommandService.h"
#include "Services/DragDropTransferService.h"
#include "Services/IEditorIconService.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorActionWidgets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UIColor kSceneHierarchyAccentColor{ 0.67f, 0.78f, 0.92f, 1.0f };
		constexpr AshEngine::UIColor kSceneHierarchyMutedColor{ 0.67f, 0.70f, 0.76f, 1.0f };
		constexpr const char* kSceneEntityContextPopupId = "SceneHierarchyEntityContextMenu";
		constexpr const char* kSceneContentContextPopupId = "SceneHierarchyContentContextMenu";
		constexpr float kSceneHierarchySearchWidth = 220.0f;

		struct SceneHierarchyEntityFilterOption
		{
			const char* pLabel = "";
			bool (*pfnMatches)(const AshEngine::Entity& refEntity) = nullptr;
		};

		SceneEntityId GetSelectedSceneEntityId(const SceneHierarchyPanelDeps& refDeps)
		{
			if (!refDeps.pSelectionService)
			{
				return 0;
			}

			const EditorSelection& refSelection = refDeps.pSelectionService->GetSelection();
			return
				refSelection.eKind == EditorSelectionKind::Entity
				? refSelection.uId
				: 0;
		}

		SceneEntityId GetEntityParentId(const AshEngine::Entity& refEntity)
		{
			const AshEngine::Entity entityParent = refEntity.get_parent();
			return entityParent.is_valid() ? entityParent.get_id() : 0;
		}

		uint32_t GetEntitySiblingIndex(const AshEngine::Entity& refEntity, const SceneService& refSceneService)
		{
			return refEntity.is_valid() ? refSceneService.GetEntitySiblingIndex(refEntity.get_id()) : 0u;
		}

		uint32_t GetParentChildCount(const SceneService& refSceneService, SceneEntityId uParentId)
		{
			if (uParentId == 0)
			{
				return static_cast<uint32_t>(refSceneService.GetActiveScene().get_root_entities().size());
			}

			const AshEngine::Entity entityParent = refSceneService.FindEntity(uParentId);
			return entityParent.is_valid() ? static_cast<uint32_t>(entityParent.get_children().size()) : 0u;
		}

		uint32_t GetReparentInsertIndexMax(
			const SceneService& refSceneService,
			SceneEntityId uSceneEntityId,
			SceneEntityId uCurrentParentId,
			SceneEntityId uTargetParentId)
		{
			uint32_t uChildCount = GetParentChildCount(refSceneService, uTargetParentId);
			if (uSceneEntityId != 0 && uCurrentParentId == uTargetParentId && uChildCount > 0)
			{
				--uChildCount;
			}
			return uChildCount;
		}

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

		std::string BuildEntityPathLabel(const AshEngine::Entity& refEntity)
		{
			if (!refEntity.is_valid())
			{
				return {};
			}

			std::vector<std::string> vecParts{};
			AshEngine::Entity current = refEntity;
			for (uint32_t uGuard = 0; uGuard < 64 && current.is_valid(); ++uGuard)
			{
				vecParts.push_back(current.get_name());
				current = current.get_parent();
			}

			std::string strPath{};
			for (std::vector<std::string>::reverse_iterator it = vecParts.rbegin(); it != vecParts.rend(); ++it)
			{
				if (!strPath.empty())
				{
					strPath += "/";
				}
				strPath += *it;
			}
			return strPath;
		}

		bool MatchesAllEntities(const AshEngine::Entity&)
		{
			return true;
		}

		bool MatchesCameraEntities(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_camera_component();
		}

		bool MatchesLightEntities(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_light_component();
		}

		bool MatchesMeshEntities(const AshEngine::Entity& refEntity)
		{
			return refEntity.has_mesh_component();
		}

		constexpr std::array<SceneHierarchyEntityFilterOption, 4> kSceneHierarchyEntityFilters{ {
			{ "All", &MatchesAllEntities },
			{ "Camera", &MatchesCameraEntities },
			{ "Light", &MatchesLightEntities },
			{ "Mesh", &MatchesMeshEntities },
		} };

		constexpr const char* kSceneHierarchyDragPayloadType = "ASH_EDITOR_SCENE_ENTITY";

		struct SceneHierarchyDropRequest
		{
			SceneEntityId uSceneEntityId = 0;
			SceneEntityId uNewParentId = 0;
			uint32_t uSiblingIndex = kSceneAppendSiblingIndex;
			EditorTreeDropVisual eVisual = EditorTreeDropVisual::None;
			bool bValid = false;
		};

		struct SceneEntityDropValidationData
		{
			const SceneService* pSceneService = nullptr;
			const DragDropTransferService* pDragDropTransferService = nullptr;
			SceneEntityId uTargetSceneEntityId = 0;
			bool bRootAppendSlot = false;
		};

		SceneEntityId DecodeDraggedSceneEntityId(const DragDropTransferService* pService, DragDropTransferId uTransferId)
		{
			if (!pService || uTransferId == 0)
			{
				return 0;
			}
			const DragDropTransferData* pData = pService->Resolve(uTransferId);
			return (pData && !pData->vecEntityIds.empty())
				? static_cast<SceneEntityId>(pData->vecEntityIds[0])
				: 0;
		}

		uint32_t AdjustInsertIndexForMove(
			SceneEntityId uCurrentParentId,
			uint32_t uCurrentSiblingIndex,
			SceneEntityId uTargetParentId,
			uint32_t uInsertIndex)
		{
			if (uCurrentParentId == uTargetParentId && uCurrentSiblingIndex < uInsertIndex)
			{
				return uInsertIndex - 1u;
			}
			return uInsertIndex;
		}

		SceneHierarchyDropRequest BuildDropRequestForTarget(
			const SceneService& refSceneService,
			SceneEntityId uDraggedSceneEntityId,
			const AshEngine::Entity& refTargetEntity,
			EditorTreeDropVisual eVisual)
		{
			if (uDraggedSceneEntityId == 0 || !refTargetEntity.is_valid() || uDraggedSceneEntityId == refTargetEntity.get_id())
			{
				return {};
			}

			const AshEngine::Entity entityDragged = refSceneService.FindEntity(uDraggedSceneEntityId);
			if (!entityDragged.is_valid())
			{
				return {};
			}

			const SceneEntityId uCurrentParentId = GetEntityParentId(entityDragged);
			const uint32_t uCurrentSiblingIndex = refSceneService.GetEntitySiblingIndex(uDraggedSceneEntityId);
			const SceneEntityId uTargetParentId = GetEntityParentId(refTargetEntity);
			const uint32_t uTargetSiblingIndex = refSceneService.GetEntitySiblingIndex(refTargetEntity.get_id());

			SceneHierarchyDropRequest request{};
			request.uSceneEntityId = uDraggedSceneEntityId;

			if (eVisual == EditorTreeDropVisual::Before || eVisual == EditorTreeDropVisual::After)
			{
				if (!refSceneService.CanReparentEntity(uDraggedSceneEntityId, uTargetParentId))
				{
					return {};
				}

				const uint32_t uRawInsertIndex = uTargetSiblingIndex + (eVisual == EditorTreeDropVisual::After ? 1u : 0u);
				request.uNewParentId = uTargetParentId;
				request.uSiblingIndex = AdjustInsertIndexForMove(
					uCurrentParentId,
					uCurrentSiblingIndex,
					uTargetParentId,
					uRawInsertIndex);
				request.eVisual = eVisual;
			}
			else if (eVisual == EditorTreeDropVisual::Into)
			{
				if (!refSceneService.CanReparentEntity(uDraggedSceneEntityId, refTargetEntity.get_id()))
				{
					return {};
				}

				request.uNewParentId = refTargetEntity.get_id();
				request.uSiblingIndex = GetReparentInsertIndexMax(
					refSceneService,
					uDraggedSceneEntityId,
					uCurrentParentId,
					refTargetEntity.get_id());
				request.eVisual = EditorTreeDropVisual::Into;
			}
			else
			{
				return {};
			}

			request.bValid =
				uCurrentParentId != request.uNewParentId ||
				uCurrentSiblingIndex != request.uSiblingIndex;
			return request;
		}

		SceneHierarchyDropRequest BuildRootAppendDropRequest(
			const SceneService& refSceneService,
			SceneEntityId uDraggedSceneEntityId)
		{
			if (uDraggedSceneEntityId == 0)
			{
				return {};
			}

			const AshEngine::Entity entityDragged = refSceneService.FindEntity(uDraggedSceneEntityId);
			if (!entityDragged.is_valid())
			{
				return {};
			}

			const SceneEntityId uCurrentParentId = GetEntityParentId(entityDragged);
			const uint32_t uCurrentSiblingIndex = refSceneService.GetEntitySiblingIndex(uDraggedSceneEntityId);
			SceneHierarchyDropRequest request{};
			request.uSceneEntityId = uDraggedSceneEntityId;
			request.uNewParentId = 0;
			request.uSiblingIndex = GetReparentInsertIndexMax(refSceneService, uDraggedSceneEntityId, uCurrentParentId, 0);
			request.eVisual = EditorTreeDropVisual::Before;
			request.bValid = uCurrentParentId != 0 || uCurrentSiblingIndex != request.uSiblingIndex;
			return request;
		}

		bool ValidateSceneDropTarget(
			DragDropTransferId uTransferId,
			EditorTreeDropVisual eVisual,
			void* pUserData)
		{
			const SceneEntityDropValidationData* pValidationData = static_cast<const SceneEntityDropValidationData*>(pUserData);
			if (!pValidationData || !pValidationData->pSceneService)
			{
				return false;
			}

			const SceneEntityId uDraggedSceneEntityId = DecodeDraggedSceneEntityId(
				pValidationData->pDragDropTransferService, uTransferId);
			if (pValidationData->bRootAppendSlot)
			{
				return BuildRootAppendDropRequest(*pValidationData->pSceneService, uDraggedSceneEntityId).bValid;
			}

			const AshEngine::Entity entityTarget = pValidationData->pSceneService->FindEntity(pValidationData->uTargetSceneEntityId);
			return BuildDropRequestForTarget(*pValidationData->pSceneService, uDraggedSceneEntityId, entityTarget, eVisual).bValid;
		}

		EditorTreeWidgetStyle MakeSceneTreeStyle()
		{
			EditorTreeWidgetStyle style{};
			style.fRowHeight = 24.0f;
			style.fIndentSpacing = 12.0f;
			style.fIconSize = 16.0f;
			style.fIconTextSpacing = 4.0f;
			style.fRowPaddingY = 5.0f;
			style.fRowSpacingY = 3.0f;
			style.fConnectorHorizontalPadding = 3.0f;
			style.fDropZoneRatio = 0.35f;
			style.fGuideLinePaddingY = 0.0f;
			style.fDropZoneRatio = 0.34f;
			style.colorGuideLine = { 0.46f, 0.49f, 0.54f, 0.55f };
			style.fAutoExpandHoverDelaySeconds = 0.45f;
			style.colorRowHoverFill = { 0.28f, 0.39f, 0.49f, 0.16f };
			style.colorRowHoverOutline = { 0.38f, 0.56f, 0.74f, 0.38f };
			style.colorRowSelectedFill = { 0.32f, 0.47f, 0.60f, 0.28f };
			style.colorRowSelectedOutline = { 0.43f, 0.64f, 0.85f, 0.84f };
			return style;
		}

		void DrawSceneSummary(AshEngine::UIContext& refUi, const AshEngine::Scene& refScene, SceneEntityId uSelectedSceneEntityId)
		{
			(void)uSelectedSceneEntityId;
			refUi.push_font(AshEngine::UIFontRole::Strong);
			refUi.text_colored_scaled(1.05f, kSceneHierarchyAccentColor, "%s", refScene.get_name().c_str());
			refUi.pop_font();
			refUi.text_colored_scaled(
				0.82f,
				kSceneHierarchyMutedColor,
				"%u entities | %u roots",
				refScene.get_entity_count(),
				static_cast<unsigned int>(refScene.get_root_entities().size()));
		}

		void DrawEmptySceneState(AshEngine::UIContext& refUi)
		{
			refUi.text_colored_scaled(0.92f, kSceneHierarchyMutedColor, "Scene is empty.");
			refUi.text_wrapped_scaled(0.82f, "Create a root entity to start building the scene.");
		}

		void AppendReparentCandidates(
			const SceneService& refSceneService,
			SceneEntityId uSceneEntityId,
			const AshEngine::Entity& refEntity,
			std::vector<SceneEntityId>& vecOutIds,
			std::vector<std::string>& vecOutLabels,
			uint32_t uDepth)
		{
			if (refEntity.get_id() != uSceneEntityId && refSceneService.CanReparentEntity(uSceneEntityId, refEntity.get_id()))
			{
				vecOutIds.push_back(refEntity.get_id());
				vecOutLabels.push_back(std::string(uDepth * 2, ' ') + refEntity.get_name());
			}

			for (const AshEngine::Entity& refChild : refEntity.get_children())
			{
				AppendReparentCandidates(refSceneService, uSceneEntityId, refChild, vecOutIds, vecOutLabels, uDepth + 1);
			}
		}

	}

	SceneHierarchyPanel::SceneHierarchyPanel(SceneHierarchyPanelDeps deps)
		: EditorPanel(EditorPanelIds::SceneHierarchy, EditorWindowTitles::SceneHierarchy)
		, _deps(deps)
	{
	}

	void SceneHierarchyPanel::BindEventBus(EditorEventBus* pEventBus)
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

		_eventBindings.Subscribe<EditorSelectionChangedEvent>(
			[this](const EditorSelectionChangedEvent& refEvent)
			{
				const SceneEntityId uSelectedSceneEntityId =
					refEvent.currentSelection.eKind == EditorSelectionKind::Entity
					? refEvent.currentSelection.uId
					: 0;
				if (_bAwaitingCreateChildSelection)
				{
					_bAwaitingCreateChildSelection = false;
					if (uSelectedSceneEntityId == 0)
					{
						_uCreateChildAnchorParentId = 0;
					}
				}
				else if (_uCreateChildAnchorParentId != 0 && uSelectedSceneEntityId != _uCreateChildAnchorParentId)
				{
					_uCreateChildAnchorParentId = 0;
				}
				if (_uPendingRenameEntityId != 0 && _uPendingRenameEntityId != uSelectedSceneEntityId)
				{
					ResetPendingRenameState();
				}
				if (_uPendingReparentEntityId != 0 && _uPendingReparentEntityId != uSelectedSceneEntityId)
				{
					ResetPendingReparentState();
				}
				if (_uPendingDeleteEntityId != 0 && _uPendingDeleteEntityId != uSelectedSceneEntityId)
				{
					ResetPendingDeleteState();
				}
			});
		_eventBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[this](const EditorActiveSceneChangedEvent&)
			{
				ResetTransientState();
			});
	}

	void SceneHierarchyPanel::OnAttach()
	{
		HLogInfo("SceneHierarchyPanel attached.");
	}

	void SceneHierarchyPanel::OnDetach()
	{
		UnsubscribeEvents();
		ClearDeps();
	}

	void SceneHierarchyPanel::ClearDeps()
	{
		_deps = {};
	}

	void SceneHierarchyPanel::UnsubscribeEvents()
	{
		_eventBindings.Clear();
	}

	void SceneHierarchyPanel::ResetPendingRenameState()
	{
		_uPendingRenameEntityId = 0;
		_strPendingRenameValue.clear();
		_bOpenRenamePopup = false;
	}

	void SceneHierarchyPanel::ResetPendingReparentState()
	{
		_uPendingReparentEntityId = 0;
		_iPendingReparentIndex = 0;
		_iPendingReparentInsertIndex = 0;
		_vecPendingReparentParentEntityIds.clear();
		_vecPendingReparentParentLabels.clear();
		_bOpenReparentPopup = false;
	}

	void SceneHierarchyPanel::ResetPendingDeleteState()
	{
		_uPendingDeleteEntityId = 0;
		_strPendingDeleteEntityName.clear();
		_bOpenDeletePopup = false;
	}

	void SceneHierarchyPanel::ResetTransientState()
	{
		_uCreateChildAnchorParentId = 0;
		_bAwaitingCreateChildSelection = false;
		ResetPendingRenameState();
		ResetPendingReparentState();
		ResetPendingDeleteState();
		_treeWidgetStateEntities.ResetDragState();
	}

	void SceneHierarchyPanel::ExecuteCreateRoot()
	{
		_uCreateChildAnchorParentId = 0;
		_bAwaitingCreateChildSelection = false;
		CreateEntity(0);
	}

	void SceneHierarchyPanel::ExecuteCreateChildFromSelection()
	{
		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(_deps);
		if (uSelectedSceneEntityId == 0)
		{
			_uCreateChildAnchorParentId = 0;
			_bAwaitingCreateChildSelection = false;
			return;
		}

		if (_uCreateChildAnchorParentId == 0)
		{
			_uCreateChildAnchorParentId = uSelectedSceneEntityId;
		}

		_bAwaitingCreateChildSelection = true;
		if (!CreateEntity(_uCreateChildAnchorParentId))
		{
			_uCreateChildAnchorParentId = 0;
			_bAwaitingCreateChildSelection = false;
		}
	}

	void SceneHierarchyPanel::RequestRenameSelected(AshEngine::UIContext* pUiContext)
	{
		BeginRenameSelectedEntity(pUiContext);
	}

	void SceneHierarchyPanel::RequestReparentSelected(AshEngine::UIContext* pUiContext)
	{
		BeginReparentSelectedEntity(pUiContext);
	}

	void SceneHierarchyPanel::RequestDeleteSelected(AshEngine::UIContext* pUiContext)
	{
		BeginDeleteSelectedEntity(pUiContext);
	}

	void SceneHierarchyPanel::BeginRenameSelectedEntity(AshEngine::UIContext* pUiContext)
	{
		if (!_deps.pSceneService)
		{
			HLogWarning("SceneHierarchyPanel rename requested, but SceneService is unavailable.");
			return;
		}

		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(_deps);
		if (uSelectedSceneEntityId == 0)
		{
			HLogInfo("SceneHierarchyPanel rename requested, but no entity is selected.");
			return;
		}

		const AshEngine::Entity entitySelected = _deps.pSceneService->FindEntity(uSelectedSceneEntityId);
		if (!entitySelected.is_valid())
		{
			HLogWarning("SceneHierarchyPanel rename requested, but entity {} no longer exists.", static_cast<unsigned long long>(uSelectedSceneEntityId));
			return;
		}

		_uPendingRenameEntityId = uSelectedSceneEntityId;
		_strPendingRenameValue = entitySelected.get_name();
		_bOpenRenamePopup = true;
	}

	void SceneHierarchyPanel::BeginReparentSelectedEntity(AshEngine::UIContext* pUiContext)
	{
		if (!_deps.pSceneService)
		{
			HLogWarning("SceneHierarchyPanel reparent requested, but SceneService is unavailable.");
			return;
		}

		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(_deps);
		if (uSelectedSceneEntityId == 0)
		{
			HLogInfo("SceneHierarchyPanel reparent requested, but no entity is selected.");
			return;
		}

		const AshEngine::Entity entitySelected = _deps.pSceneService->FindEntity(uSelectedSceneEntityId);
		if (!entitySelected.is_valid())
		{
			HLogWarning("SceneHierarchyPanel reparent requested, but entity {} no longer exists.", static_cast<unsigned long long>(uSelectedSceneEntityId));
			return;
		}

		_uPendingReparentEntityId = uSelectedSceneEntityId;
		_vecPendingReparentParentEntityIds.clear();
		_vecPendingReparentParentLabels.clear();
		_vecPendingReparentParentEntityIds.push_back(0);
		_vecPendingReparentParentLabels.push_back("<Root>");

		for (const AshEngine::Entity& refRoot : _deps.pSceneService->GetActiveScene().get_root_entities())
		{
			AppendReparentCandidates(
				*_deps.pSceneService,
				uSelectedSceneEntityId,
				refRoot,
				_vecPendingReparentParentEntityIds,
				_vecPendingReparentParentLabels,
				0);
		}

		const AshEngine::Entity entityParent = entitySelected.get_parent();
		const SceneEntityId uCurrentParentId = entityParent.is_valid() ? entityParent.get_id() : 0;
		_iPendingReparentInsertIndex = static_cast<int32_t>(GetEntitySiblingIndex(entitySelected, *_deps.pSceneService));
		_iPendingReparentIndex = 0;
		for (size_t uIndex = 0; uIndex < _vecPendingReparentParentEntityIds.size(); ++uIndex)
		{
			if (_vecPendingReparentParentEntityIds[uIndex] == uCurrentParentId)
			{
				_iPendingReparentIndex = static_cast<int32_t>(uIndex);
				break;
			}
		}

		_bOpenReparentPopup = true;
	}

	void SceneHierarchyPanel::BeginDeleteSelectedEntity(AshEngine::UIContext* pUiContext)
	{
		if (!_deps.pSceneService)
		{
			HLogWarning("SceneHierarchyPanel delete requested, but SceneService is unavailable.");
			return;
		}

		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(_deps);
		if (uSelectedSceneEntityId == 0)
		{
			HLogInfo("SceneHierarchyPanel delete requested, but no entity is selected.");
			return;
		}

		const AshEngine::Entity entitySelected = _deps.pSceneService->FindEntity(uSelectedSceneEntityId);
		if (!entitySelected.is_valid())
		{
			HLogWarning("SceneHierarchyPanel delete requested, but entity {} no longer exists.", static_cast<unsigned long long>(uSelectedSceneEntityId));
			return;
		}

		_uPendingDeleteEntityId = uSelectedSceneEntityId;
		_strPendingDeleteEntityName = entitySelected.get_name();
		_bOpenDeletePopup = true;
	}

	bool SceneHierarchyPanel::CreateEntity(SceneEntityId uParentId)
	{
		if (!_deps.pSceneService || !_deps.pCommandExecutor)
		{
			HLogWarning(
				"SceneHierarchyPanel create entity skipped (scene_service={}, command_executor={}).",
				_deps.pSceneService != nullptr,
				_deps.pCommandExecutor != nullptr);
			return false;
		}

		AshEngine::Scene& refScene = _deps.pSceneService->GetActiveScene();
		const std::string strEntityName = "Entity " + std::to_string(refScene.get_entity_count() + 1);
		const bool bExecuted =
			_deps.pCommandExecutor->ExecuteCommand(std::make_unique<CreateEntityCommand>(strEntityName, uParentId));
		if (!bExecuted)
		{
			HLogWarning("SceneHierarchyPanel failed to create entity '{}'.", strEntityName);
		}
		return bExecuted;
	}

	void SceneHierarchyPanel::DestroyEntity(SceneEntityId uSceneEntityId)
	{
		if (!_deps.pSceneService || !_deps.pSelectionService || !_deps.pCommandExecutor)
		{
			HLogWarning(
				"SceneHierarchyPanel delete entity skipped (scene_service={}, selection_service={}, command_executor={}).",
				_deps.pSceneService != nullptr,
				_deps.pSelectionService != nullptr,
				_deps.pCommandExecutor != nullptr);
			return;
		}

		if (uSceneEntityId == 0)
		{
			HLogInfo("SceneHierarchyPanel delete entity skipped because entityId is 0.");
			return;
		}

		if (!_deps.pCommandExecutor->ExecuteCommand(std::make_unique<DeleteEntityCommand>(uSceneEntityId)))
		{
			HLogWarning("SceneHierarchyPanel failed to delete entity {}.", static_cast<unsigned long long>(uSceneEntityId));
		}
	}

	void SceneHierarchyPanel::DrawToolbar(const EditorFrameContext& refFrameContext)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		refUi.set_next_item_width(kSceneHierarchySearchWidth);
		refUi.input_text("##SceneHierarchySearch", _strSearchText);
		refUi.same_line();
		refUi.set_next_item_width(110.0f);
		const std::vector<const char*> vecFilterLabels{ "All", "Camera", "Light", "Mesh" };
		refUi.combo("##SceneHierarchyTypeFilter", _iEntityTypeFilterIndex, vecFilterLabels);
		if (!_strSearchText.empty() || _iEntityTypeFilterIndex != 0)
		{
			refUi.same_line();
			if (refUi.small_button("Reset"))
			{
				_strSearchText.clear();
				_iEntityTypeFilterIndex = 0;
			}
		}
	}

	bool SceneHierarchyPanel::IsSceneEntityDragActive(const AshEngine::UIContext* pUiContext) const
	{
		return pUiContext && pUiContext->is_drag_drop_payload_active(kSceneHierarchyDragPayloadType);
	}

	void SceneHierarchyPanel::HandleRootAppendDropTarget(
		EditorTreeWidget& refTreeWidget,
		bool bDraggingSceneEntity)
	{
		if (!_deps.pSceneService)
		{
			return;
		}

		SceneEntityDropValidationData validationData{};
		validationData.pSceneService = _deps.pSceneService;
		validationData.pDragDropTransferService = _deps.pDragDropTransferService;
		validationData.bRootAppendSlot = true;

		EditorTreeDropTargetDesc dropTarget{};
		dropTarget.pPayloadType = kSceneHierarchyDragPayloadType;
		dropTarget.pfnValidateDrop = ValidateSceneDropTarget;
		dropTarget.pValidationUserData = &validationData;

		EditorTreeDropSlotDesc slotDesc{};
		slotDesc.svUniqueId = "__scene_hierarchy_root_append__";
		slotDesc.fHeight = 18.0f;
		slotDesc.bExpandToAvailableHeightWhileDragging = bDraggingSceneEntity;
		slotDesc.ePreviewVisual = EditorTreeDropVisual::Before;
		slotDesc.pDropTarget = &dropTarget;

		const EditorTreeDropSlotResult slotResult = refTreeWidget.DrawDropSlot(slotDesc, bDraggingSceneEntity);
		if (slotResult.bDropDelivered && slotResult.uDropTransferId != 0 && _deps.pCommandExecutor && _deps.pDragDropTransferService)
		{
			const SceneEntityId uDraggedId = DecodeDraggedSceneEntityId(_deps.pDragDropTransferService, slotResult.uDropTransferId);
			const SceneHierarchyDropRequest request =
				BuildRootAppendDropRequest(*_deps.pSceneService, uDraggedId);
			if (request.bValid)
			{
				const bool bExecuted = _deps.pCommandExecutor->ExecuteCommand(
					std::make_unique<ReparentEntityCommand>(
						request.uSceneEntityId,
						request.uNewParentId,
						request.uSiblingIndex));
				if (!bExecuted)
				{
					HLogWarning(
						"SceneHierarchyPanel failed to apply root drop reparent for entity {}.",
						static_cast<unsigned long long>(request.uSceneEntityId));
				}
			}
		}
	}

	void SceneHierarchyPanel::DrawEntityTree(
		EditorTreeWidget& refTreeWidget,
		const EditorFrameContext& refFrameContext,
		const AshEngine::Entity& refEntity,
		bool bIsLastSibling)
	{
		if (!_deps.pSceneService)
		{
			return;
		}

		const std::vector<AshEngine::Entity> vecChildren = refEntity.get_children();
		const bool bHasChildren = !vecChildren.empty();
		const std::string strEntityName = refEntity.get_name();
		const bool bSelected =
			_deps.pSelectionService &&
			_deps.pSelectionService->GetSelection().eKind == EditorSelectionKind::Entity &&
			_deps.pSelectionService->GetSelection().uId == refEntity.get_id();

		AshEngine::UITextureHandle pIconClosed = nullptr;
		AshEngine::UITextureHandle pIconOpen = nullptr;
		if (_deps.pIconService && refFrameContext.pUiContext)
		{
			pIconClosed = _deps.pIconService->GetIcon(GetEntityIconId(refEntity), *refFrameContext.pUiContext);
			pIconOpen = pIconClosed;
		}

		const SceneEntityId uSceneEntityId = refEntity.get_id();
		SceneEntityDropValidationData validationData{};
		validationData.pSceneService = _deps.pSceneService;
		validationData.pDragDropTransferService = _deps.pDragDropTransferService;
		validationData.uTargetSceneEntityId = uSceneEntityId;
		const DragDropTransferId uDragTransferId = _deps.pDragDropTransferService
			? _deps.pDragDropTransferService->Register(DragDropTransferData{ "SceneEntity", { uSceneEntityId }, {} })
			: 0;
		const EditorTreeDragSourceDesc dragSource{
			kSceneHierarchyDragPayloadType,
			uDragTransferId,
			strEntityName.c_str()
		};
		EditorTreeDropTargetDesc dropTarget{};
		dropTarget.pPayloadType = kSceneHierarchyDragPayloadType;
		dropTarget.bAllowBefore = true;
		dropTarget.bAllowAfter = true;
		dropTarget.bAllowInto = true;
		dropTarget.bAutoExpandOnIntoHover = true;
		dropTarget.pfnValidateDrop = ValidateSceneDropTarget;
		dropTarget.pValidationUserData = const_cast<SceneEntityDropValidationData*>(&validationData);

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
		if (itemResult.bClicked && _deps.pSelectionService)
		{
			_deps.pSelectionService->Select({ EditorSelectionKind::Entity, uSceneEntityId, strEntityName, {} });
		}
		if (refFrameContext.pUiContext && refFrameContext.pUiContext->is_item_clicked(AshEngine::UIMouseButton::Right))
		{
			if (_deps.pSelectionService)
			{
				_deps.pSelectionService->Select({ EditorSelectionKind::Entity, uSceneEntityId, strEntityName, {} });
			}
		}
		DrawEntityContextMenu(refFrameContext, refEntity);

		if (itemResult.bDropDelivered && itemResult.uDropTransferId != 0 && _deps.pCommandExecutor && _deps.pDragDropTransferService)
		{
			const SceneEntityId uDraggedId = DecodeDraggedSceneEntityId(_deps.pDragDropTransferService, itemResult.uDropTransferId);
			const SceneHierarchyDropRequest request = BuildDropRequestForTarget(
				*_deps.pSceneService,
				uDraggedId,
				refEntity,
				itemResult.eDropVisual);
			if (request.bValid)
			{
				const bool bExecuted = _deps.pCommandExecutor->ExecuteCommand(
					std::make_unique<ReparentEntityCommand>(
						request.uSceneEntityId,
						request.uNewParentId,
						request.uSiblingIndex));
				if (!bExecuted)
				{
					HLogWarning(
						"SceneHierarchyPanel failed to apply drop reparent for entity {}.",
						static_cast<unsigned long long>(request.uSceneEntityId));
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
				DrawEntityTree(refTreeWidget, refFrameContext, vecChildren[uChildIndex], uChildIndex + 1 == vecChildren.size());
			}
			refTreeWidget.PopLevel();
		}
		refTreeWidget.TreePop();
	}

	void SceneHierarchyPanel::DrawEntityContextMenu(const EditorFrameContext& refFrameContext, const AshEngine::Entity& refEntity)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!refUi.begin_popup_context_item(kSceneEntityContextPopupId))
		{
			return;
		}

		if (_deps.pSelectionService)
		{
			_deps.pSelectionService->Select({ EditorSelectionKind::Entity, refEntity.get_id(), refEntity.get_name(), {} });
		}

		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::SceneCreateChild,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::SelectionRename,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::SelectionReparent,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}
		refUi.separator();
		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::SelectionDelete,
			"scene_hierarchy.entity_context"))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}

	void SceneHierarchyPanel::DrawContentContextMenu(const EditorFrameContext& refFrameContext, SceneEntityId uSelectedSceneEntityId)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!refUi.begin_popup(kSceneContentContextPopupId))
		{
			return;
		}

		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::SceneCreateRoot,
			"scene_hierarchy.content_context"))
		{
			refUi.close_current_popup();
		}

		refUi.separator();
		const bool bCanCreateChild = uSelectedSceneEntityId != 0;
		if (_deps.pCommandService && DrawEditorActionMenuItem(
			refUi,
			*_deps.pCommandService,
			EditorActionIds::SceneCreateChild,
			"scene_hierarchy.content_context",
			bCanCreateChild))
		{
			refUi.close_current_popup();
		}

		refUi.end_popup();
	}

	void SceneHierarchyPanel::DrawRenameModal(const EditorFrameContext& refFrameContext)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (_bOpenRenamePopup)
		{
			refUi.open_popup("Rename Entity");
			_bOpenRenamePopup = false;
		}
		if (!refUi.begin_popup_modal("Rename Entity"))
		{
			return;
		}

		refUi.text_unformatted("Update the selected entity name.");
		if (_uPendingRenameEntityId == 0)
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}
		refUi.input_text("Name", _strPendingRenameValue);
		refUi.separator();

		bool bCanApply = _uPendingRenameEntityId != 0 && !_strPendingRenameValue.empty();
		if (bCanApply && _deps.pSceneService)
		{
			const AshEngine::Entity entity = _deps.pSceneService->FindEntity(_uPendingRenameEntityId);
			bCanApply = entity.is_valid() && entity.get_name() != _strPendingRenameValue;
		}
		refUi.begin_disabled(!bCanApply);
		if (refUi.button("Apply"))
		{
			if (_deps.pCommandExecutor)
			{
				const bool bExecuted = _deps.pCommandExecutor->ExecuteCommand(
					std::make_unique<RenameEntityCommand>(_uPendingRenameEntityId, _strPendingRenameValue));
				if (!bExecuted)
				{
					HLogWarning(
						"SceneHierarchyPanel failed to rename entity {}.",
						static_cast<unsigned long long>(_uPendingRenameEntityId));
				}
			}
			else
			{
				HLogWarning("SceneHierarchyPanel rename apply clicked, but command executor is unavailable.");
			}
			ResetPendingRenameState();
			refUi.close_current_popup();
		}
		refUi.end_disabled();
		refUi.same_line();
		if (refUi.button("Cancel"))
		{
			ResetPendingRenameState();
			refUi.close_current_popup();
		}
		refUi.end_popup();
	}

	void SceneHierarchyPanel::DrawReparentModal(const EditorFrameContext& refFrameContext)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (_bOpenReparentPopup)
		{
			refUi.open_popup("Reparent Entity");
			_bOpenReparentPopup = false;
		}
		if (!refUi.begin_popup_modal("Reparent Entity"))
		{
			return;
		}

		refUi.text_unformatted("Move the selected entity under another parent.");
		if (_uPendingReparentEntityId == 0)
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}
		refUi.combo("Parent", _iPendingReparentIndex, _vecPendingReparentParentLabels);
		int32_t iInsertIndexMax = 0;
		SceneEntityId uTargetParentId = 0;
		SceneEntityId uCurrentParentId = 0;
		refUi.separator();

		bool bHasValidTarget =
			_uPendingReparentEntityId != 0 &&
			_iPendingReparentIndex >= 0 &&
			_iPendingReparentIndex < static_cast<int32_t>(_vecPendingReparentParentEntityIds.size());
		if (bHasValidTarget && _deps.pSceneService)
		{
			const AshEngine::Entity entity = _deps.pSceneService->FindEntity(_uPendingReparentEntityId);
			bHasValidTarget = entity.is_valid();
			if (bHasValidTarget)
			{
				uTargetParentId = _vecPendingReparentParentEntityIds[static_cast<size_t>(_iPendingReparentIndex)];
				uCurrentParentId = GetEntityParentId(entity);
				iInsertIndexMax = static_cast<int32_t>(GetReparentInsertIndexMax(
					*_deps.pSceneService,
					_uPendingReparentEntityId,
					uCurrentParentId,
					uTargetParentId));
				_iPendingReparentInsertIndex = std::clamp(_iPendingReparentInsertIndex, 0, iInsertIndexMax);
				const int32_t iCurrentSiblingIndex = static_cast<int32_t>(GetEntitySiblingIndex(entity, *_deps.pSceneService));
				bHasValidTarget =
					uCurrentParentId != uTargetParentId ||
					iCurrentSiblingIndex != _iPendingReparentInsertIndex;
			}
		}

		refUi.input_int("Insert At", _iPendingReparentInsertIndex);
		_iPendingReparentInsertIndex = std::clamp(_iPendingReparentInsertIndex, 0, iInsertIndexMax);
		refUi.text("Valid Range: 0 - %d", iInsertIndexMax);
		refUi.text_unformatted("Insert At is the 0-based sibling slot under the target parent.");
		refUi.begin_disabled(!bHasValidTarget);
		if (refUi.button("Apply"))
		{
			if (_deps.pCommandExecutor && bHasValidTarget)
			{
				const bool bExecuted = _deps.pCommandExecutor->ExecuteCommand(
					std::make_unique<ReparentEntityCommand>(
						_uPendingReparentEntityId,
						uTargetParentId,
						static_cast<uint32_t>(_iPendingReparentInsertIndex)));
				if (!bExecuted)
				{
					HLogWarning(
						"SceneHierarchyPanel failed to reparent entity {}.",
						static_cast<unsigned long long>(_uPendingReparentEntityId));
				}
			}
			else if (!_deps.pCommandExecutor && bHasValidTarget)
			{
				HLogWarning("SceneHierarchyPanel reparent apply clicked, but command executor is unavailable.");
			}
			ResetPendingReparentState();
			refUi.close_current_popup();
		}
		refUi.end_disabled();
		refUi.same_line();
		if (refUi.button("Cancel"))
		{
			ResetPendingReparentState();
			refUi.close_current_popup();
		}
		refUi.end_popup();
	}

	void SceneHierarchyPanel::DrawDeleteModal(const EditorFrameContext& refFrameContext)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (_bOpenDeletePopup)
		{
			refUi.open_popup("Delete Entity");
			_bOpenDeletePopup = false;
		}
		if (!refUi.begin_popup_modal("Delete Entity"))
		{
			return;
		}

		refUi.text_unformatted("Delete the selected entity?");
		if (_uPendingDeleteEntityId == 0)
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}
		refUi.text("Target: %s", _strPendingDeleteEntityName.empty() ? "<Unknown>" : _strPendingDeleteEntityName.c_str());
		refUi.text_unformatted("This action can be undone.");
		refUi.separator();

		const bool bCanDelete = _uPendingDeleteEntityId != 0;
		refUi.begin_disabled(!bCanDelete);
		if (refUi.button("Delete"))
		{
			DestroyEntity(_uPendingDeleteEntityId);
			ResetPendingDeleteState();
			refUi.close_current_popup();
		}
		refUi.end_disabled();
		refUi.same_line();
		if (refUi.button("Cancel"))
		{
			ResetPendingDeleteState();
			refUi.close_current_popup();
		}
		refUi.end_popup();
	}

	void SceneHierarchyPanel::OnGui(const EditorFrameContext& frameContext)
	{
		if (!BeginPanelWindow(frameContext))
		{
			EndPanelWindow(frameContext);
			return;
		}
		if (!frameContext.pUiContext)
		{
			EndPanelWindow(frameContext);
			return;
		}
		AshEngine::UIContext& refUi = *frameContext.pUiContext;
		if (!_deps.pSceneService)
		{
			refUi.text_unformatted("Scene service unavailable.");
			EndPanelWindow(frameContext);
			return;
		}

		AshEngine::Scene& refScene = _deps.pSceneService->GetActiveScene();
		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(_deps);
		const bool bDraggingSceneEntity = IsSceneEntityDragActive(frameContext.pUiContext);

		const AshEngine::UIVec2 vecHeaderCursorPos = refUi.get_cursor_pos();
		refUi.begin_group();
		DrawSceneSummary(refUi, refScene, uSelectedSceneEntityId);
		refUi.end_group();
		const AshEngine::UIRect rectSummary = refUi.get_item_rect();

		const float fDividerPadding = 14.0f;
		const float fToolbarStartX = vecHeaderCursorPos.x + rectSummary.width + fDividerPadding * 2.0f;
		refUi.same_line(fToolbarStartX, 0.0f);
		refUi.begin_group();
		DrawToolbar(frameContext);
		refUi.end_group();
		const AshEngine::UIRect rectToolbar = refUi.get_item_rect();

		const float fDividerX = rectSummary.x + rectSummary.width + fDividerPadding;
		const float fDividerTop = std::min(rectSummary.y, rectToolbar.y) + 2.0f;
		const float fDividerBottom =
			std::max(rectSummary.y + rectSummary.height, rectToolbar.y + rectToolbar.height) - 2.0f;
		refUi.draw_window_line(
			{ fDividerX, fDividerTop },
			{ fDividerX, fDividerBottom },
			{ 0.42f, 0.46f, 0.52f, 0.75f },
			1.0f);

		refUi.separator();

		const std::vector<AshEngine::Entity> vecRoots = refScene.get_root_entities();
		if (vecRoots.empty())
		{
			DrawEmptySceneState(refUi);
		}

		// When search or type filtering is active, present a flat match list to avoid fighting the tree open-state.
		if (!_strSearchText.empty() || _iEntityTypeFilterIndex != 0)
		{
			DrawSearchResults(frameContext, refScene, uSelectedSceneEntityId);
		}
		else
		{
			EditorTreeWidget treeWidget(refUi, _treeWidgetStateEntities, MakeSceneTreeStyle());
			treeWidget.ResetDragStateIfInactive();
			for (size_t uRootIndex = 0; uRootIndex < vecRoots.size(); ++uRootIndex)
			{
				DrawEntityTree(treeWidget, frameContext, vecRoots[uRootIndex], uRootIndex + 1 == vecRoots.size());
			}

			HandleRootAppendDropTarget(treeWidget, bDraggingSceneEntity);
		}

		const bool bOpenContentMenu =
			refUi.is_window_hovered_with_children() &&
			!refUi.is_any_item_hovered() &&
			!refUi.is_any_item_active() &&
			refUi.is_mouse_released(AshEngine::UIMouseButton::Right);
		const bool bClearSelection =
			refUi.is_window_hovered_with_children() &&
			!refUi.is_any_item_hovered() &&
			!refUi.is_any_item_active() &&
			refUi.is_mouse_released(AshEngine::UIMouseButton::Left);
		if (bOpenContentMenu)
		{
			refUi.open_popup(kSceneContentContextPopupId);
		}
		if (bClearSelection && _deps.pSelectionService)
		{
			_deps.pSelectionService->Clear();
		}

		DrawContentContextMenu(frameContext, uSelectedSceneEntityId);

		DrawRenameModal(frameContext);
		DrawReparentModal(frameContext);
		DrawDeleteModal(frameContext);
		EndPanelWindow(frameContext);
	}

	void SceneHierarchyPanel::DrawSearchResults(
		const EditorFrameContext& refFrameContext,
		AshEngine::Scene& refScene,
		SceneEntityId uSelectedEntityId)
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;

		const std::string strLoweredSearch = ToLowerCopy(_strSearchText);
		_iEntityTypeFilterIndex = std::clamp(
			_iEntityTypeFilterIndex,
			0,
			static_cast<int32_t>(kSceneHierarchyEntityFilters.size() - 1));
		const SceneHierarchyEntityFilterOption& refFilter = kSceneHierarchyEntityFilters[_iEntityTypeFilterIndex];
		std::vector<AshEngine::Entity> vecMatches{};
		for (const AshEngine::Entity& refEntity : refScene.get_entities())
		{
			const std::string strName = refEntity.get_name();
			if (refFilter.pfnMatches && !refFilter.pfnMatches(refEntity))
			{
				continue;
			}
			if (!strLoweredSearch.empty() && ToLowerCopy(strName).find(strLoweredSearch) == std::string::npos)
			{
				continue;
			}
			vecMatches.push_back(refEntity);
		}

		refUi.text_colored(
			kSceneHierarchyMutedColor,
			"Matches: %u | Filter: %s",
			static_cast<uint32_t>(vecMatches.size()),
			refFilter.pLabel);
		refUi.separator();

		if (vecMatches.empty())
		{
			refUi.text_unformatted("No entities match the current search/filter.");
			return;
		}

		if (!refUi.begin_table(
			"SceneHierarchySearchResults",
			2,
			AshEngine::UITableFlagBits::RowBg |
				AshEngine::UITableFlagBits::BordersInner |
				AshEngine::UITableFlagBits::SizingStretchProp |
				AshEngine::UITableFlagBits::ScrollY))
		{
			return;
		}

		refUi.table_setup_column("Name", AshEngine::UITableColumnFlagBits::WidthStretch);
		refUi.table_setup_column("Path", AshEngine::UITableColumnFlagBits::WidthStretch);
		refUi.table_headers_row();
		for (const AshEngine::Entity& refEntity : vecMatches)
		{
			if (!refEntity.is_valid())
			{
				continue;
			}

			const SceneEntityId uEntityId = refEntity.get_id();
			const bool bSelected = uEntityId != 0 && uEntityId == uSelectedEntityId;
			const std::string strName = refEntity.get_name();
			const std::string strPath = BuildEntityPathLabel(refEntity);

			refUi.table_next_row();
			refUi.table_next_column();
			const std::string strId = std::to_string(uEntityId);
			refUi.push_id(strId.c_str());
			if (refUi.selectable(strName.c_str(), bSelected, AshEngine::UISelectableFlagBits::SpanAllColumns))
			{
				if (_deps.pSelectionService)
				{
					_deps.pSelectionService->Select({ EditorSelectionKind::Entity, uEntityId, strName, {} });
				}
			}
			refUi.table_next_column();
			refUi.text_unformatted(strPath.c_str());
			refUi.pop_id();
		}

		refUi.end_table();
	}
}

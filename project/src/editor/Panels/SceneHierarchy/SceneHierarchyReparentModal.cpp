#include "Panels/SceneHierarchy/SceneHierarchyReparentModal.h"

#include "Base/hlog.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"
#include "Services/SceneService.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace AshEditor
{
	namespace
	{
		uint32_t GetReparentInsertIndexMax(
			const SceneService& refSceneService,
			const SceneEntityId uSceneEntityId,
			const SceneEntityId uCurrentParentId,
			const SceneEntityId uTargetParentId)
		{
			uint32_t uChildCount = GetParentChildCount(refSceneService, uTargetParentId);
			if (uSceneEntityId != 0 && uCurrentParentId == uTargetParentId && uChildCount > 0)
			{
				--uChildCount;
			}
			return uChildCount;
		}
	}

	void SceneHierarchyReparentModal::BeginFromSelection(
		SceneHierarchyPanelState& refState,
		const SceneHierarchyPanelDeps& refDeps) const
	{
		if (!refDeps.pSceneService)
		{
			HLogWarning("SceneHierarchy reparent requested, but SceneService is unavailable.");
			return;
		}

		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(refDeps);
		if (uSelectedSceneEntityId == 0)
		{
			HLogInfo("SceneHierarchy reparent requested, but no entity is selected.");
			return;
		}

		const AshEngine::Entity entitySelected = refDeps.pSceneService->FindEntity(uSelectedSceneEntityId);
		if (!entitySelected.is_valid())
		{
			HLogWarning(
				"SceneHierarchy reparent requested, but entity {} no longer exists.",
				static_cast<unsigned long long>(uSelectedSceneEntityId));
			return;
		}

		refState.reparentModal.uEntityId = uSelectedSceneEntityId;
		BuildReparentCandidateLists(
			*refDeps.pSceneService,
			uSelectedSceneEntityId,
			refState.reparentModal.vecParentEntityIds,
			refState.reparentModal.vecParentLabels);

		const AshEngine::Entity entityParent = entitySelected.get_parent();
		const SceneEntityId uCurrentParentId = entityParent.is_valid() ? entityParent.get_id() : 0;
		refState.reparentModal.iInsertIndex =
			static_cast<int32_t>(GetEntitySiblingIndex(entitySelected, *refDeps.pSceneService));
		refState.reparentModal.iParentIndex = 0;
		for (size_t uIndex = 0; uIndex < refState.reparentModal.vecParentEntityIds.size(); ++uIndex)
		{
			if (refState.reparentModal.vecParentEntityIds[uIndex] == uCurrentParentId)
			{
				refState.reparentModal.iParentIndex = static_cast<int32_t>(uIndex);
				break;
			}
		}

		refState.reparentModal.bOpenPopup = true;
	}

	void SceneHierarchyReparentModal::Draw(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState) const
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (refState.reparentModal.bOpenPopup)
		{
			refUi.open_popup("Reparent Entity");
			refState.reparentModal.bOpenPopup = false;
		}

		if (!refUi.begin_popup_modal("Reparent Entity"))
		{
			return;
		}

		refUi.text_unformatted("Move the selected entity under another parent.");
		if (refState.reparentModal.uEntityId == 0)
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}

		refUi.combo(
			"Parent",
			refState.reparentModal.iParentIndex,
			refState.reparentModal.vecParentLabels);

		int32_t iInsertIndexMax = 0;
		SceneEntityId uTargetParentId = 0;
		SceneEntityId uCurrentParentId = 0;
		refUi.separator();

		bool bHasValidTarget =
			refState.reparentModal.uEntityId != 0 &&
			refState.reparentModal.iParentIndex >= 0 &&
			refState.reparentModal.iParentIndex <
				static_cast<int32_t>(refState.reparentModal.vecParentEntityIds.size());
		if (bHasValidTarget && refDeps.pSceneService)
		{
			const AshEngine::Entity entity = refDeps.pSceneService->FindEntity(refState.reparentModal.uEntityId);
			bHasValidTarget = entity.is_valid();
			if (bHasValidTarget)
			{
				uTargetParentId = refState.reparentModal.vecParentEntityIds[
					static_cast<size_t>(refState.reparentModal.iParentIndex)];
				uCurrentParentId = GetEntityParentId(entity);
				iInsertIndexMax = static_cast<int32_t>(GetReparentInsertIndexMax(
					*refDeps.pSceneService,
					refState.reparentModal.uEntityId,
					uCurrentParentId,
					uTargetParentId));
				refState.reparentModal.iInsertIndex =
					std::clamp(refState.reparentModal.iInsertIndex, 0, iInsertIndexMax);
				const int32_t iCurrentSiblingIndex =
					static_cast<int32_t>(GetEntitySiblingIndex(entity, *refDeps.pSceneService));
				bHasValidTarget =
					uCurrentParentId != uTargetParentId ||
					iCurrentSiblingIndex != refState.reparentModal.iInsertIndex;
			}
		}

		refUi.input_int("Insert At", refState.reparentModal.iInsertIndex);
		refState.reparentModal.iInsertIndex = std::clamp(refState.reparentModal.iInsertIndex, 0, iInsertIndexMax);
		refUi.text("Valid Range: 0 - %d", iInsertIndexMax);
		refUi.text_unformatted("Insert At is the 0-based sibling slot under the target parent.");
		refUi.begin_disabled(!bHasValidTarget);
		if (refUi.button("Apply"))
		{
			if (refDeps.pCommandExecutor && bHasValidTarget)
			{
				const bool bExecuted = refDeps.pCommandExecutor->ExecuteCommand(
					std::make_unique<ReparentEntityCommand>(
						refState.reparentModal.uEntityId,
						uTargetParentId,
						static_cast<uint32_t>(refState.reparentModal.iInsertIndex)));
				if (!bExecuted)
				{
					HLogWarning(
						"SceneHierarchy failed to reparent entity {}.",
						static_cast<unsigned long long>(refState.reparentModal.uEntityId));
				}
			}
			else if (!refDeps.pCommandExecutor && bHasValidTarget)
			{
				HLogWarning("SceneHierarchy reparent apply clicked, but command executor is unavailable.");
			}
			refState.reparentModal.Reset();
			refUi.close_current_popup();
		}
		refUi.end_disabled();
		refUi.same_line();
		if (refUi.button("Cancel"))
		{
			refState.reparentModal.Reset();
			refUi.close_current_popup();
		}
		refUi.end_popup();
	}
}

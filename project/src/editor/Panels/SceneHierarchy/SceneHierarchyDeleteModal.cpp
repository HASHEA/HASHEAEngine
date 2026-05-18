#include "Panels/SceneHierarchy/SceneHierarchyDeleteModal.h"

#include "Base/hlog.h"
#include "Function/Gui/UIContext.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <cfloat>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace AshEditor
{
	void SceneHierarchyDeleteModal::BeginFromSelection(
		SceneHierarchyPanelState& refState,
		const SceneHierarchyPanelDeps& refDeps) const
	{
		if (!refDeps.pSceneService || !refDeps.pSelectionService)
		{
			HLogWarning(
				"SceneHierarchy delete requested, but required service is unavailable (scene_service={}, selection_service={}).",
				refDeps.pSceneService != nullptr,
				refDeps.pSelectionService != nullptr);
			return;
		}

		const std::vector<uint64_t> vecSelectedIds = refDeps.pSelectionService->GetSelectedIds(EditorSelectionKind::Entity);
		std::vector<SceneEntityId> vecDeleteEntityIds = refDeps.pSceneService->BuildTopLevelEntityIds(vecSelectedIds);
		if (vecDeleteEntityIds.empty())
		{
			HLogInfo("SceneHierarchy delete requested, but no valid entity is selected.");
			return;
		}

		const SceneEntityId uPrimaryDeleteEntityId = vecDeleteEntityIds.front();
		const AshEngine::Entity entitySelected = refDeps.pSceneService->FindEntity(uPrimaryDeleteEntityId);
		if (!entitySelected.is_valid())
		{
			HLogWarning(
				"SceneHierarchy delete requested, but entity {} no longer exists.",
				static_cast<unsigned long long>(uPrimaryDeleteEntityId));
			return;
		}

		refState.deleteModal.uEntityId = uPrimaryDeleteEntityId;
		refState.deleteModal.vecEntityIds = std::move(vecDeleteEntityIds);
		refState.deleteModal.strDisplayName = refState.deleteModal.vecEntityIds.size() == 1u
			? entitySelected.get_name()
			: std::to_string(refState.deleteModal.vecEntityIds.size()) + " entities";
		refState.deleteModal.bOpenPopup = true;
	}

	void SceneHierarchyDeleteModal::Draw(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState) const
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (refState.deleteModal.bOpenPopup)
		{
			refUi.open_popup("Delete Entity");
			refState.deleteModal.bOpenPopup = false;
		}

		refUi.set_next_window_size({ 500.0f, 0.0f }, AshEngine::UIConditionFlagBits::Always);
		refUi.set_next_window_size_constraints({ 500.0f, 0.0f }, { 760.0f, FLT_MAX });
		if (!refUi.begin_popup_modal("Delete Entity"))
		{
			return;
		}

		refUi.text_unformatted(
			refState.deleteModal.vecEntityIds.size() > 1u
			? "Delete the selected entities?"
			: "Delete the selected entity?");
		if (refState.deleteModal.vecEntityIds.empty())
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}

		refUi.text_wrapped(
			"Target: %s",
			refState.deleteModal.strDisplayName.empty() ? "<Unknown>" : refState.deleteModal.strDisplayName.c_str());
		refUi.text_wrapped("This action can be undone.");
		refUi.separator();

		const bool bCanDelete = !refState.deleteModal.vecEntityIds.empty();
		refUi.begin_disabled(!bCanDelete);
		if (refUi.button("Delete"))
		{
			DestroySelectedEntities(refDeps, refState.deleteModal.vecEntityIds);
			refState.deleteModal.Reset();
			refUi.close_current_popup();
		}
		refUi.end_disabled();
		refUi.same_line();
		if (refUi.button("Cancel"))
		{
			refState.deleteModal.Reset();
			refUi.close_current_popup();
		}
		refUi.end_popup();
	}
}

#include "Panels/SceneHierarchy/SceneHierarchyRenameModal.h"

#include "Base/hlog.h"
#include "Core/EntityCommands.h"
#include "Core/IEditorCommandExecutor.h"
#include "Function/Gui/UIContext.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"
#include "Services/SceneService.h"

#include <cfloat>
#include <memory>

namespace AshEditor
{
	void SceneHierarchyRenameModal::BeginFromSelection(
		SceneHierarchyPanelState& refState,
		const SceneHierarchyPanelDeps& refDeps) const
	{
		if (!refDeps.pSceneService)
		{
			HLogWarning("SceneHierarchy rename requested, but SceneService is unavailable.");
			return;
		}

		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(refDeps);
		if (uSelectedSceneEntityId == 0)
		{
			HLogInfo("SceneHierarchy rename requested, but no entity is selected.");
			return;
		}

		const AshEngine::Entity entitySelected = refDeps.pSceneService->FindEntity(uSelectedSceneEntityId);
		if (!entitySelected.is_valid())
		{
			HLogWarning(
				"SceneHierarchy rename requested, but entity {} no longer exists.",
				static_cast<unsigned long long>(uSelectedSceneEntityId));
			return;
		}

		refState.renameModal.uEntityId = uSelectedSceneEntityId;
		refState.renameModal.strValue = entitySelected.get_name();
		refState.renameModal.bOpenPopup = true;
	}

	void SceneHierarchyRenameModal::Draw(
		const EditorFrameContext& refFrameContext,
		const SceneHierarchyPanelDeps& refDeps,
		SceneHierarchyPanelState& refState) const
	{
		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (refState.renameModal.bOpenPopup)
		{
			refUi.open_popup("Rename Entity");
			refState.renameModal.bOpenPopup = false;
		}

		refUi.set_next_window_size({ 460.0f, 0.0f }, AshEngine::UIConditionFlagBits::Always);
		refUi.set_next_window_size_constraints({ 460.0f, 0.0f }, { 720.0f, FLT_MAX });
		if (!refUi.begin_popup_modal("Rename Entity"))
		{
			return;
		}

		refUi.text_unformatted("Rename the selected entity.");
		if (refState.renameModal.uEntityId == 0)
		{
			refUi.close_current_popup();
			refUi.end_popup();
			return;
		}

		refUi.set_next_item_width(-1.0f);
		refUi.input_text("Name", refState.renameModal.strValue);
		refUi.separator();

		bool bCanApply = refState.renameModal.uEntityId != 0 && !refState.renameModal.strValue.empty();
		if (bCanApply && refDeps.pSceneService)
		{
			const AshEngine::Entity entity = refDeps.pSceneService->FindEntity(refState.renameModal.uEntityId);
			bCanApply = entity.is_valid() && entity.get_name() != refState.renameModal.strValue;
		}

		refUi.begin_disabled(!bCanApply);
		if (refUi.button("Apply"))
		{
			if (refDeps.pCommandExecutor)
			{
				const bool bExecuted = refDeps.pCommandExecutor->ExecuteCommand(
					std::make_unique<RenameEntityCommand>(
						refState.renameModal.uEntityId,
						refState.renameModal.strValue));
				if (!bExecuted)
				{
					HLogWarning(
						"SceneHierarchy failed to rename entity {}.",
						static_cast<unsigned long long>(refState.renameModal.uEntityId));
				}
			}
			else
			{
				HLogWarning("SceneHierarchy rename apply clicked, but command executor is unavailable.");
			}
			refState.renameModal.Reset();
			refUi.close_current_popup();
		}
		refUi.end_disabled();
		refUi.same_line();
		if (refUi.button("Cancel"))
		{
			refState.renameModal.Reset();
			refUi.close_current_popup();
		}
		refUi.end_popup();
	}
}

#include "Panels/SceneHierarchyPanel.h"

#include "Base/hlog.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelSupport.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Widgets/EditorThemeColors.h"

#include <algorithm>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr const char* kSceneContentContextPopupId = "SceneHierarchyContentContextMenu";

		void DrawSceneSummary(AshEngine::UIContext& refUi, const AshEngine::Scene& refScene)
		{
			refUi.push_font(AshEngine::UIFontRole::Strong);
			refUi.text_colored_scaled(1.05f, GetEditorHeadingTextColor(refUi), "%s", refScene.get_name().c_str());
			refUi.pop_font();
			refUi.text_colored_scaled(
				0.82f,
				GetEditorMutedTextColor(refUi),
				"%u entities | %u roots",
				refScene.get_entity_count(),
				static_cast<unsigned int>(refScene.get_root_entities().size()));
		}

		void DrawEmptySceneState(AshEngine::UIContext& refUi)
		{
			refUi.text_colored_scaled(0.92f, GetEditorMutedTextColor(refUi), "Scene is empty.");
			refUi.text_wrapped_scaled(0.82f, "Create a root entity to start building the scene.");
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
				if (_state.bAwaitingCreateChildSelection)
				{
					_state.bAwaitingCreateChildSelection = false;
					if (uSelectedSceneEntityId == 0)
					{
						_state.uCreateChildAnchorParentId = 0;
					}
				}
				else if (_state.uCreateChildAnchorParentId != 0 &&
					uSelectedSceneEntityId != _state.uCreateChildAnchorParentId)
				{
					_state.uCreateChildAnchorParentId = 0;
				}

				if (_state.renameModal.uEntityId != 0 && _state.renameModal.uEntityId != uSelectedSceneEntityId)
				{
					_state.renameModal.Reset();
				}
				if (_state.reparentModal.uEntityId != 0 && _state.reparentModal.uEntityId != uSelectedSceneEntityId)
				{
					_state.reparentModal.Reset();
				}
				if (_state.deleteModal.uEntityId != 0 && _state.deleteModal.uEntityId != uSelectedSceneEntityId)
				{
					_state.deleteModal.Reset();
				}
			});
		_eventBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[this](const EditorActiveSceneChangedEvent&)
			{
				_state.ResetTransientState();
			});
		_eventBindings.Subscribe<EditorSceneChangedEvent>(
			[this](const EditorSceneChangedEvent& refEvent)
			{
				if (refEvent.eKind == AshEngine::SceneChangeKind::SceneReloaded ||
					refEvent.eKind == AshEngine::SceneChangeKind::SceneReplaced)
				{
					_state.ResetTransientState();
					return;
				}

				if (refEvent.eKind == AshEngine::SceneChangeKind::HierarchyChanged)
				{
					_state.treeWidgetStateEntities.ResetDragState();
					return;
				}

				if (refEvent.eKind != AshEngine::SceneChangeKind::EntityRemoved)
				{
					return;
				}

				const SceneEntityId uRemovedEntityId = static_cast<SceneEntityId>(refEvent.uEntityId);
				if (_state.uCreateChildAnchorParentId == uRemovedEntityId)
				{
					_state.uCreateChildAnchorParentId = 0;
					_state.bAwaitingCreateChildSelection = false;
				}
				if (_state.uRangeSelectionAnchorEntityId == uRemovedEntityId)
				{
					_state.uRangeSelectionAnchorEntityId = 0;
				}
				if (_state.renameModal.uEntityId == uRemovedEntityId)
				{
					_state.renameModal.Reset();
				}
				if (_state.reparentModal.uEntityId == uRemovedEntityId)
				{
					_state.reparentModal.Reset();
				}
				if (_state.deleteModal.uEntityId == uRemovedEntityId)
				{
					_state.deleteModal.Reset();
				}
				_state.treeWidgetStateEntities.ResetDragState();
			});
	}

	void SceneHierarchyPanel::OnAttach()
	{
		HLogInfo("SceneHierarchyPanel attached.");
	}

	void SceneHierarchyPanel::OnDetach()
	{
		UnsubscribeEvents();
		_state.ResetTransientState();
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

	void SceneHierarchyPanel::ExecuteCreateRoot()
	{
		AshEditor::ExecuteCreateRoot(_state, _deps);
	}

	void SceneHierarchyPanel::ExecuteCreateChildFromSelection()
	{
		AshEditor::ExecuteCreateChildFromSelection(_state, _deps);
	}

	void SceneHierarchyPanel::ExecuteCopySelection()
	{
		AshEditor::ExecuteCopySelection(_state, _deps);
	}

	void SceneHierarchyPanel::ExecutePasteSelection()
	{
		AshEditor::ExecutePasteSelection(_state, _deps);
	}

	void SceneHierarchyPanel::ExecuteDuplicateSelection()
	{
		AshEditor::ExecuteDuplicateSelection(_deps);
	}

	bool SceneHierarchyPanel::CanPasteSelection() const
	{
		return AshEditor::CanPasteSelection(_state);
	}

	void SceneHierarchyPanel::RequestRenameSelected(AshEngine::UIContext* pUiContext)
	{
		(void)pUiContext;
		_renameModal.BeginFromSelection(_state, _deps);
	}

	void SceneHierarchyPanel::RequestReparentSelected(AshEngine::UIContext* pUiContext)
	{
		(void)pUiContext;
		_reparentModal.BeginFromSelection(_state, _deps);
	}

	void SceneHierarchyPanel::RequestDeleteSelected(AshEngine::UIContext* pUiContext)
	{
		(void)pUiContext;
		_deleteModal.BeginFromSelection(_state, _deps);
	}

	void SceneHierarchyPanel::OnGui(const EditorFrameContext& refFrameContext)
	{
		if (!BeginPanelWindow(refFrameContext))
		{
			EndPanelWindow(refFrameContext);
			return;
		}
		if (!refFrameContext.pUiContext)
		{
			EndPanelWindow(refFrameContext);
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		if (!_deps.pSceneService)
		{
			refUi.text_unformatted("Scene service unavailable.");
			EndPanelWindow(refFrameContext);
			return;
		}

		AshEngine::Scene& refScene = _deps.pSceneService->GetActiveScene();
		const SceneEntityId uSelectedSceneEntityId = GetSelectedSceneEntityId(_deps);

		const AshEngine::UIVec2 vecHeaderCursorPos = refUi.get_cursor_pos();
		const AshEngine::UIVec2 vecHeaderAvailableSize = refUi.get_content_region_avail();
		refUi.begin_group();
		DrawSceneSummary(refUi, refScene);
		refUi.end_group();
		const AshEngine::UIRect rectSummary = refUi.get_item_rect();

		const float fDividerPadding = 14.0f;
		constexpr float kSceneHierarchyToolbarEstimatedWidth = 390.0f;
		const bool bDrawToolbarInline =
			vecHeaderAvailableSize.x >= rectSummary.width + fDividerPadding * 2.0f + kSceneHierarchyToolbarEstimatedWidth;
		if (bDrawToolbarInline)
		{
			const float fToolbarStartX = vecHeaderCursorPos.x + rectSummary.width + fDividerPadding * 2.0f;
			refUi.same_line(fToolbarStartX, 0.0f);
		}
		else
		{
			refUi.spacing();
		}
		refUi.begin_group();
		_toolbarView.Draw(refFrameContext, _deps, _state);
		refUi.end_group();
		const AshEngine::UIRect rectToolbar = refUi.get_item_rect();

		if (bDrawToolbarInline)
		{
			const float fDividerX = rectSummary.x + rectSummary.width + fDividerPadding;
			const float fDividerTop = std::min(rectSummary.y, rectToolbar.y) + 2.0f;
			const float fDividerBottom =
				std::max(rectSummary.y + rectSummary.height, rectToolbar.y + rectToolbar.height) - 2.0f;
			refUi.draw_window_line(
				{ fDividerX, fDividerTop },
				{ fDividerX, fDividerBottom },
				GetEditorGuideLineColor(refUi),
				1.0f);
		}

		refUi.separator();

		const std::vector<AshEngine::Entity> vecRoots = refScene.get_root_entities();
		if (vecRoots.empty())
		{
			DrawEmptySceneState(refUi);
		}

		// Search and type filters intentionally switch to a flat result view to avoid fighting the tree open-state.
		if (!_state.strSearchText.empty() || _state.iEntityTypeFilterIndex != 0)
		{
			_searchResultsView.Draw(refFrameContext, _deps, _state, refScene);
		}
		else
		{
			_treeView.Draw(refFrameContext, _deps, _state, refScene);
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

		DrawContentContextMenu(refFrameContext, _deps, uSelectedSceneEntityId);
		_renameModal.Draw(refFrameContext, _deps, _state);
		_reparentModal.Draw(refFrameContext, _deps, _state);
		_deleteModal.Draw(refFrameContext, _deps, _state);
		EndPanelWindow(refFrameContext);
	}
}

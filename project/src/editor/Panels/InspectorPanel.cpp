#include "Panels/InspectorPanel.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/InspectorComponentEditorRegistry.h"
#include "Panels/Inspector/InspectorPanelState.h"
#include "Panels/Inspector/InspectorPanelSupport.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <memory>
#include <vector>

namespace AshEditor
{
	InspectorPanel::InspectorPanel(InspectorPanelDeps deps)
		: EditorPanel(EditorPanelIds::Inspector, EditorWindowTitles::Inspector)
		, _deps(deps)
		, _upState(std::make_unique<InspectorPanelState>())
		, _upComponentEditorRegistry(std::make_unique<InspectorComponentEditorRegistry>())
	{
		RegisterDefaultInspectorComponentEditors(*_upComponentEditorRegistry);
	}

	InspectorPanel::~InspectorPanel() = default;

	void InspectorPanel::BindEventBus(EditorEventBus* pEventBus)
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
				const bool bCurrentSingleEntity =
					refEvent.vecCurrentSelections.size() == 1 &&
					refEvent.currentSelection.eKind == EditorSelectionKind::Entity;
				const bool bPreviousSingleEntity =
					refEvent.vecPreviousSelections.size() == 1 &&
					refEvent.previousSelection.eKind == EditorSelectionKind::Entity;
				if (!bCurrentSingleEntity ||
					!bPreviousSingleEntity ||
					refEvent.currentSelection.uId != refEvent.previousSelection.uId)
				{
					ResetEntityDrafts();
				}
			});
		_eventBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[this](const EditorActiveSceneChangedEvent&)
			{
				ResetEntityDrafts();
				_bRefreshEntityDraftsOnNextGui = false;
			});
		_eventBindings.Subscribe<EditorSceneChangedEvent>(
			[this](const EditorSceneChangedEvent& refEvent)
			{
				RequestEntityDraftRefreshFromSceneChange(refEvent);
			});
	}

	void InspectorPanel::ClearDeps()
	{
		_deps = {};
	}

	void InspectorPanel::UnsubscribeEvents()
	{
		_eventBindings.Clear();
	}

	void InspectorPanel::RequestEntityDraftRefreshFromSceneChange(const EditorSceneChangedEvent& refEvent)
	{
		if (refEvent.eKind == AshEngine::SceneChangeKind::SceneReloaded ||
			refEvent.eKind == AshEngine::SceneChangeKind::SceneReplaced)
		{
			_bRefreshEntityDraftsOnNextGui = true;
			return;
		}

		if (!_deps.pSelectionService)
		{
			return;
		}

		const EditorSelection& refSelection = _deps.pSelectionService->GetSelection();
		if (refSelection.eKind != EditorSelectionKind::Entity ||
			refSelection.uId == 0 ||
			refSelection.uId != refEvent.uEntityId)
		{
			return;
		}

		switch (refEvent.eKind)
		{
		case AshEngine::SceneChangeKind::EntityRemoved:
		case AshEngine::SceneChangeKind::HierarchyChanged:
		case AshEngine::SceneChangeKind::ComponentChanged:
			_bRefreshEntityDraftsOnNextGui = true;
			break;
		default:
			break;
		}
	}

	InspectorPanelState& InspectorPanel::AccessInspectorState()
	{
		return GetState();
	}

	const InspectorPanelDeps& InspectorPanel::AccessInspectorDeps() const
	{
		return _deps;
	}

	void InspectorPanel::OnAttach()
	{
		HLogInfo("InspectorPanel attached.");
	}

	void InspectorPanel::OnDetach()
	{
		UnsubscribeEvents();
		ClearDeps();
	}

	void InspectorPanel::OnGui(const EditorFrameContext& frameContext)
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
		if (!_deps.pSelectionService || !_deps.pSelectionService->HasSelection())
		{
			ResetEntityDrafts();
			_bRefreshEntityDraftsOnNextGui = false;
			DrawInspectorEmptyState(refUi);
			EndPanelWindow(frameContext);
			return;
		}

		const std::vector<EditorSelection>& vecSelections = _deps.pSelectionService->GetSelections();
		const EditorSelection& refSelection = _deps.pSelectionService->GetSelection();
		if (vecSelections.size() > 1)
		{
			ResetEntityDrafts();
			_bRefreshEntityDraftsOnNextGui = false;
			DrawInspectorMultiSelectionSummary(
				refUi,
				vecSelections,
				"Multi-selection is read-only for now. Batch actions can run from hierarchy commands; component editing will need explicit mixed-value rules before it is safe.");
			EndPanelWindow(frameContext);
			return;
		}

		if (refSelection.eKind == EditorSelectionKind::Entity && _deps.pSceneService)
		{
			DrawInspectorSelectionSummary(
				refUi,
				refSelection,
				"Edit the selected entity. Property changes are applied immediately and can be undone with Ctrl+Z.");
			AshEngine::Entity entity = _deps.pSceneService->FindEntity(refSelection.uId);
			if (_bRefreshEntityDraftsOnNextGui)
			{
				ResetEntityDrafts();
				_bRefreshEntityDraftsOnNextGui = false;
			}
			if (!entity.is_valid())
			{
				ResetEntityDrafts();
			}
			DrawEntityInspector(refUi, entity);
		}
		else if (refSelection.eKind == EditorSelectionKind::Asset)
		{
			DrawInspectorSelectionSummary(
				refUi,
				refSelection,
				"Basic asset metadata is folded into this tooltip. Use Asset Browser Preview when you need a richer inspection view.");
			ResetEntityDrafts();
			_bRefreshEntityDraftsOnNextGui = false;
			DrawInspectorAssetInspector(refUi, refSelection);
		}
		else
		{
			DrawInspectorSelectionSummary(refUi, refSelection);
			ResetEntityDrafts();
			_bRefreshEntityDraftsOnNextGui = false;
			DrawInspectorPanelIntro(refUi, "Inspector", "The current selection type does not have an inspector adapter yet.");
		}

		EndPanelWindow(frameContext);
	}

	InspectorPanelState& InspectorPanel::GetState()
	{
		return *_upState;
	}

	const InspectorPanelState& InspectorPanel::GetState() const
	{
		return *_upState;
	}
}

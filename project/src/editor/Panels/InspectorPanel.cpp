#include "Panels/InspectorPanel.h"

#include "Base/hlog.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/CameraComponentEditor.h"
#include "Panels/Inspector/InspectorPanelState.h"
#include "Panels/Inspector/InspectorPanelSupport.h"
#include "Panels/Inspector/LightComponentEditor.h"
#include "Panels/Inspector/MeshComponentEditor.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <memory>

namespace AshEditor
{
	InspectorPanel::InspectorPanel(InspectorPanelDeps deps)
		: EditorPanel(EditorPanelIds::Inspector, EditorWindowTitles::Inspector)
		, _deps(deps)
		, _upState(std::make_unique<InspectorPanelState>())
	{
		_vecComponentEditors.emplace_back(std::make_unique<CameraComponentEditor>());
		_vecComponentEditors.emplace_back(std::make_unique<LightComponentEditor>());
		_vecComponentEditors.emplace_back(std::make_unique<MeshComponentEditor>());
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
				if (
					refEvent.currentSelection.eKind != EditorSelectionKind::Entity ||
					refEvent.currentSelection.uId != refEvent.previousSelection.uId)
				{
					ResetEntityDrafts();
				}
			});
		_eventBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[this](const EditorActiveSceneChangedEvent&)
			{
				ResetEntityDrafts();
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
			DrawInspectorEmptyState(refUi);
			EndPanelWindow(frameContext);
			return;
		}

		const EditorSelection& refSelection = _deps.pSelectionService->GetSelection();

		if (refSelection.eKind == EditorSelectionKind::Entity && _deps.pSceneService)
		{
			DrawInspectorSelectionSummary(
				refUi,
				refSelection,
				"Edit the selected entity. Property changes are applied immediately and can be undone with Ctrl+Z.");
			AshEngine::Entity entity = _deps.pSceneService->FindEntity(refSelection.uId);
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
			DrawInspectorAssetInspector(refUi, refSelection);
		}
		else
		{
			DrawInspectorSelectionSummary(refUi, refSelection);
			ResetEntityDrafts();
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

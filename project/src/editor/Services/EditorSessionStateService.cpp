#include "Services/EditorSessionStateService.h"

#include "Core/EditorEventBus.h"
#include "Shell/PanelManager.h"
#include "Services/SceneService.h"
#include "Services/UndoRedoService.h"

namespace AshEditor
{
	void EditorSessionStateService::BindEventBus(EditorEventBus* pEventBus)
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

		_eventBindings.Subscribe<EditorActiveSceneChangedEvent>(
			[this](const EditorActiveSceneChangedEvent& refEvent)
			{
				_eventActiveScene = refEvent;
			});
		_eventBindings.Subscribe<EditorSceneChangedEvent>(
			[this](const EditorSceneChangedEvent& refEvent)
			{
				_eventActiveDocumentDirtyState.bDirty = refEvent.bDirty;
			});
		_eventBindings.Subscribe<EditorSelectionChangedEvent>(
			[this](const EditorSelectionChangedEvent& refEvent)
			{
				_selection = refEvent.currentSelection;
				_vecSelections = refEvent.vecCurrentSelections;
			});
		_eventBindings.Subscribe<EditorUndoHistoryChangedEvent>(
			[this](const EditorUndoHistoryChangedEvent& refEvent)
			{
				_eventUndoHistory = refEvent;
			});
		_eventBindings.Subscribe<EditorTransactionStateChangedEvent>(
			[this](const EditorTransactionStateChangedEvent& refEvent)
			{
				_eventTransactionState = refEvent;
			});
		_eventBindings.Subscribe<EditorActiveDocumentDirtyStateChangedEvent>(
			[this](const EditorActiveDocumentDirtyStateChangedEvent& refEvent)
			{
				_eventActiveDocumentDirtyState = refEvent;
			});
		_eventBindings.Subscribe<EditorDocumentOperationEvent>(
			[this](const EditorDocumentOperationEvent& refEvent)
			{
				_eventLastDocumentOperation = refEvent;
			});
		_eventBindings.Subscribe<EditorActionInvokedEvent>(
			[this](const EditorActionInvokedEvent& refEvent)
			{
				_eventLastActionInvocation = refEvent;
			});
		_eventBindings.Subscribe<EditorShortcutScopeChangedEvent>(
			[this](const EditorShortcutScopeChangedEvent& refEvent)
			{
				_eventShortcutScope = refEvent;
			});
		_eventBindings.Subscribe<EditorPanelOpenStateChangedEvent>(
			[this](const EditorPanelOpenStateChangedEvent& refEvent)
			{
				_mapPanelOpenStates[refEvent.strPanelId] = refEvent.bOpen;
			});
		_eventBindings.Subscribe<EditorViewportLayoutResetEvent>(
			[this](const EditorViewportLayoutResetEvent&)
			{
				++_uViewportLayoutResetRevision;
			});
	}

	void EditorSessionStateService::SyncFromServices(
		const SceneService& refSceneService,
		const UndoRedoService& refUndoRedoService)
	{
		_eventActiveScene.strSceneName = refSceneService.GetActiveScene().get_name();
		_eventActiveScene.strScenePath = refSceneService.GetActiveScenePath().generic_string();
		_eventShortcutScope = EditorShortcutScopeChangedEvent{
			EditorShortcutScope::Global,
			{}
		};
		_eventUndoHistory = EditorUndoHistoryChangedEvent{
			refUndoRedoService.CanUndo(),
			refUndoRedoService.CanRedo(),
			refUndoRedoService.HasOpenTransaction(),
			refUndoRedoService.GetOpenTransactionLabel()
		};
		_eventTransactionState = EditorTransactionStateChangedEvent{
			refUndoRedoService.HasOpenTransaction(),
			refUndoRedoService.GetOpenTransactionLabel(),
			0u
		};
		_eventActiveDocumentDirtyState = EditorActiveDocumentDirtyStateChangedEvent{
			refUndoRedoService.IsDirty()
		};
	}

	void EditorSessionStateService::SyncFromPanelManager(const PanelManager& refPanelManager)
	{
		_mapPanelOpenStates.clear();
		for (const std::unique_ptr<EditorPanel>& refPanelOwner : refPanelManager.GetPanels())
		{
			const EditorPanel* pPanel = refPanelOwner.get();
			if (!pPanel)
			{
				continue;
			}

			_mapPanelOpenStates[pPanel->GetId()] = pPanel->IsOpen();
		}
	}

	void EditorSessionStateService::Clear()
	{
		UnsubscribeEvents();
		_eventActiveScene = {};
		_selection = {};
		_vecSelections.clear();
		_eventUndoHistory = {};
		_eventTransactionState = {};
		_eventActiveDocumentDirtyState = {};
		_eventLastDocumentOperation = {};
		_eventLastActionInvocation = {};
		_eventShortcutScope = {};
		_mapPanelOpenStates.clear();
		_uViewportLayoutResetRevision = 0;
	}

	const EditorActiveSceneChangedEvent& EditorSessionStateService::GetActiveScene() const
	{
		return _eventActiveScene;
	}

	const EditorSelection& EditorSessionStateService::GetSelection() const
	{
		return _selection;
	}

	const std::vector<EditorSelection>& EditorSessionStateService::GetSelections() const
	{
		return _vecSelections;
	}

	const EditorUndoHistoryChangedEvent& EditorSessionStateService::GetUndoHistory() const
	{
		return _eventUndoHistory;
	}

	const EditorTransactionStateChangedEvent& EditorSessionStateService::GetTransactionState() const
	{
		return _eventTransactionState;
	}

	const EditorActiveDocumentDirtyStateChangedEvent& EditorSessionStateService::GetActiveDocumentDirtyState() const
	{
		return _eventActiveDocumentDirtyState;
	}

	const EditorDocumentOperationEvent& EditorSessionStateService::GetLastDocumentOperation() const
	{
		return _eventLastDocumentOperation;
	}

	const EditorActionInvokedEvent& EditorSessionStateService::GetLastActionInvocation() const
	{
		return _eventLastActionInvocation;
	}

	const EditorShortcutScopeChangedEvent& EditorSessionStateService::GetShortcutScope() const
	{
		return _eventShortcutScope;
	}

	bool EditorSessionStateService::IsPanelOpen(std::string_view svPanelId, bool bDefaultValue) const
	{
		const std::unordered_map<std::string, bool>::const_iterator itOpenState =
			_mapPanelOpenStates.find(std::string(svPanelId));
		return itOpenState != _mapPanelOpenStates.end() ? itOpenState->second : bDefaultValue;
	}

	uint64_t EditorSessionStateService::GetViewportLayoutResetRevision() const
	{
		return _uViewportLayoutResetRevision;
	}

	void EditorSessionStateService::UnsubscribeEvents()
	{
		_eventBindings.Clear();
	}
}

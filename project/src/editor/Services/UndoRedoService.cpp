#include "Services/UndoRedoService.h"

#include "Base/hlog.h"
#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <utility>
#include <vector>

namespace AshEditor
{
	UndoRedoService::~UndoRedoService() = default;

	void UndoRedoService::SetEventBus(EditorEventBus* pEventBus)
	{
		_pEventBus = pEventBus;
	}

	bool UndoRedoService::Execute(std::unique_ptr<EditorCommand> upCommand, EditorContext& refContext)
	{
		if (!upCommand)
		{
			return false;
		}

		// During an open transaction, commands are still executed immediately, but history is deferred until commit/cancel.
		return _upPendingTransaction
			? ExecuteTransactional(std::move(upCommand), refContext)
			: ExecuteStandalone(std::move(upCommand), refContext);
	}

	bool UndoRedoService::RecordExecuted(
		std::unique_ptr<EditorCommand> upCommand,
		EditorContext& refContext)
	{
		if (!upCommand)
		{
			return false;
		}

		const bool bTransactional = _upPendingTransaction != nullptr;
		try
		{
			const EditorCommandSelection selection = upCommand->GetSelectionAfterExecute();
			if (bTransactional)
			{
				_upPendingTransaction->vecCommands.reserve(
					_upPendingTransaction->vecCommands.size() + 1u);
				const bool bFirstTransactionCommand = _upPendingTransaction->vecCommands.empty();
				_upPendingTransaction->vecCommands.push_back(std::move(upCommand));
				try
				{
					ApplySelection(refContext, selection);
				}
				catch (...)
				{
					upCommand = std::move(_upPendingTransaction->vecCommands.back());
					_upPendingTransaction->vecCommands.pop_back();
					throw;
				}
				if (bFirstTransactionCommand)
				{
					_vecRedoStack.clear();
				}
			}
			else
			{
				_vecUndoStack.reserve(_vecUndoStack.size() + 1u);
				const uint64_t uNewHistoryStateId = AllocateHistoryStateId();
				_vecUndoStack.push_back(HistoryEntry{ std::move(upCommand), uNewHistoryStateId });
				try
				{
					ApplySelection(refContext, selection);
				}
				catch (...)
				{
					upCommand = std::move(_vecUndoStack.back().upCommand);
					_vecUndoStack.pop_back();
					throw;
				}
				_vecRedoStack.clear();
				_uCurrentHistoryStateId = uNewHistoryStateId;
			}
		}
		catch (...)
		{
			try
			{
				if (upCommand && !upCommand->Undo(refContext))
				{
					HLogError("Failed to roll back an already-executed command after history recording failed.");
				}
			}
			catch (...)
			{
				HLogError("Already-executed command rollback raised an exception after history recording failed.");
			}
			return false;
		}

		try
		{
			NotifyHistoryChanged();
			if (bTransactional)
			{
				NotifyTransactionStateChanged();
			}
			NotifyDocumentDirtyStateChanged();
		}
		catch (...)
		{
			HLogWarning("Already-executed command was recorded, but a history notification failed.");
		}
		return true;
	}

	bool UndoRedoService::Undo(EditorContext& refContext)
	{
		if (_upPendingTransaction || _vecUndoStack.empty())
		{
			return false;
		}

		// Keep history stable on failure: if undo() fails, the command is pushed back onto the undo stack.
		HistoryEntry entry = std::move(_vecUndoStack.back());
		_vecUndoStack.pop_back();
		if (!entry.upCommand || !entry.upCommand->Undo(refContext))
		{
			HLogWarning(
				"Undo failed for command '{}'. History preserved. undoStack={}, redoStack={}.",
				entry.upCommand ? entry.upCommand->GetLabel() : "<null>",
				_vecUndoStack.size(),
				_vecRedoStack.size());
			_vecUndoStack.push_back(std::move(entry));
			return false;
		}

		ApplySelection(refContext, entry.upCommand->GetSelectionAfterUndo());
		_vecRedoStack.push_back(std::move(entry));
		_uCurrentHistoryStateId = _vecUndoStack.empty() ? 0 : _vecUndoStack.back().uStateId;
		NotifyHistoryChanged();
		NotifyDocumentDirtyStateChanged();
		return true;
	}

	bool UndoRedoService::Redo(EditorContext& refContext)
	{
		if (_upPendingTransaction || _vecRedoStack.empty())
		{
			return false;
		}

		// Keep redo stable on failure: if execute() fails, the command is pushed back onto the redo stack.
		HistoryEntry entry = std::move(_vecRedoStack.back());
		_vecRedoStack.pop_back();
		if (!entry.upCommand || !entry.upCommand->Execute(refContext))
		{
			HLogWarning(
				"Redo failed for command '{}'. History preserved. undoStack={}, redoStack={}.",
				entry.upCommand ? entry.upCommand->GetLabel() : "<null>",
				_vecUndoStack.size(),
				_vecRedoStack.size());
			_vecRedoStack.push_back(std::move(entry));
			return false;
		}

		ApplySelection(refContext, entry.upCommand->GetSelectionAfterExecute());
		_uCurrentHistoryStateId = entry.uStateId;
		_vecUndoStack.push_back(std::move(entry));
		NotifyHistoryChanged();
		NotifyDocumentDirtyStateChanged();
		return true;
	}

	bool UndoRedoService::BeginTransaction(std::string strLabel)
	{
		if (_upPendingTransaction)
		{
			return false;
		}

		_upPendingTransaction = std::make_unique<PendingTransaction>();
		_upPendingTransaction->strLabel = std::move(strLabel);
		NotifyHistoryChanged();
		NotifyTransactionStateChanged();
		return true;
	}

	bool UndoRedoService::CommitTransaction()
	{
		if (!_upPendingTransaction)
		{
			return false;
		}

		if (_upPendingTransaction->vecCommands.empty())
		{
			_upPendingTransaction.reset();
			NotifyHistoryChanged();
			NotifyTransactionStateChanged();
			return true;
		}

		const uint64_t uNewHistoryStateId = AllocateHistoryStateId();
		if (_upPendingTransaction->vecCommands.size() == 1)
		{
			PushUndoCommand(std::move(_upPendingTransaction->vecCommands.front()), uNewHistoryStateId);
			_uCurrentHistoryStateId = uNewHistoryStateId;
			_upPendingTransaction.reset();
			NotifyHistoryChanged();
			NotifyTransactionStateChanged();
			NotifyDocumentDirtyStateChanged();
			return true;
		}

		std::unique_ptr<CompositeCommand> upComposite = std::make_unique<CompositeCommand>(_upPendingTransaction->strLabel);
		for (std::unique_ptr<EditorCommand>& upCommand : _upPendingTransaction->vecCommands)
		{
			upComposite->Append(std::move(upCommand));
		}
		PushUndoCommand(std::move(upComposite), uNewHistoryStateId);
		_uCurrentHistoryStateId = uNewHistoryStateId;
		_upPendingTransaction.reset();
		NotifyHistoryChanged();
		NotifyTransactionStateChanged();
		NotifyDocumentDirtyStateChanged();
		return true;
	}

	void UndoRedoService::CancelTransaction(EditorContext& refContext)
	{
		if (!_upPendingTransaction)
		{
			return;
		}

		for (
			std::vector<std::unique_ptr<EditorCommand>>::reverse_iterator itCommand = _upPendingTransaction->vecCommands.rbegin();
			itCommand != _upPendingTransaction->vecCommands.rend();
			++itCommand)
		{
			if (*itCommand && (*itCommand)->Undo(refContext))
			{
				ApplySelection(refContext, (*itCommand)->GetSelectionAfterUndo());
			}
			else if (*itCommand)
			{
				HLogWarning("CancelTransaction failed to undo command '{}'.", (*itCommand)->GetLabel());
			}
		}
		_upPendingTransaction.reset();
		NotifyHistoryChanged();
		NotifyTransactionStateChanged();
		NotifyDocumentDirtyStateChanged();
	}

	void UndoRedoService::Clear()
	{
		_upPendingTransaction.reset();
		_vecUndoStack.clear();
		_vecRedoStack.clear();
		_uCurrentHistoryStateId = 0;
		_uSavedHistoryStateId = 0;
		_uNextHistoryStateId = 1;
		NotifyHistoryChanged();
		NotifyTransactionStateChanged();
		NotifyDocumentDirtyStateChanged();
	}

	void UndoRedoService::MarkSaved()
	{
		_uSavedHistoryStateId = _uCurrentHistoryStateId;
		NotifyDocumentDirtyStateChanged();
	}

	bool UndoRedoService::CanUndo() const
	{
		return !_upPendingTransaction && !_vecUndoStack.empty();
	}

	bool UndoRedoService::CanRedo() const
	{
		return !_upPendingTransaction && !_vecRedoStack.empty();
	}

	bool UndoRedoService::HasOpenTransaction() const
	{
		return _upPendingTransaction != nullptr;
	}

	const std::string& UndoRedoService::GetOpenTransactionLabel() const
	{
		static const std::string k_empty_label{};
		return _upPendingTransaction ? _upPendingTransaction->strLabel : k_empty_label;
	}

	bool UndoRedoService::IsDirty() const
	{
		return
			(_upPendingTransaction && !_upPendingTransaction->vecCommands.empty()) ||
			_uCurrentHistoryStateId != _uSavedHistoryStateId;
	}

	bool UndoRedoService::ExecuteStandalone(std::unique_ptr<EditorCommand> upCommand, EditorContext& refContext)
	{
		if (!upCommand || !upCommand->Execute(refContext))
		{
			return false;
		}

		_vecRedoStack.clear();
		ApplySelection(refContext, upCommand->GetSelectionAfterExecute());
		const uint64_t uNewHistoryStateId = AllocateHistoryStateId();
		PushUndoCommand(std::move(upCommand), uNewHistoryStateId);
		_uCurrentHistoryStateId = uNewHistoryStateId;
		NotifyHistoryChanged();
		NotifyDocumentDirtyStateChanged();
		return true;
	}

	bool UndoRedoService::ExecuteTransactional(std::unique_ptr<EditorCommand> upCommand, EditorContext& refContext)
	{
		if (!_upPendingTransaction || !upCommand || !upCommand->Execute(refContext))
		{
			return false;
		}

		if (_upPendingTransaction->vecCommands.empty())
		{
			_vecRedoStack.clear();
		}

		ApplySelection(refContext, upCommand->GetSelectionAfterExecute());
		if (!_upPendingTransaction->vecCommands.empty() &&
			_upPendingTransaction->vecCommands.back() &&
			_upPendingTransaction->vecCommands.back()->TryMerge(*upCommand))
		{
			NotifyHistoryChanged();
			NotifyTransactionStateChanged();
			NotifyDocumentDirtyStateChanged();
			return true;
		}

		_upPendingTransaction->vecCommands.push_back(std::move(upCommand));
		NotifyHistoryChanged();
		NotifyTransactionStateChanged();
		NotifyDocumentDirtyStateChanged();
		return true;
	}

	void UndoRedoService::PushUndoCommand(std::unique_ptr<EditorCommand> upCommand, uint64_t uStateId)
	{
		if (!upCommand)
		{
			return;
		}

		if (_uCurrentHistoryStateId != _uSavedHistoryStateId &&
			!_vecUndoStack.empty() && _vecUndoStack.back().upCommand &&
			_vecUndoStack.back().upCommand->TryMerge(*upCommand))
		{
			_vecUndoStack.back().uStateId = uStateId;
			return;
		}

		_vecUndoStack.push_back(HistoryEntry{
			std::move(upCommand),
			uStateId
		});
	}

	void UndoRedoService::ApplySelection(EditorContext& refContext, const EditorCommandSelection& refSelection) const
	{
		if (!refContext.pSelectionService)
		{
			return;
		}

		switch (refSelection.eMode)
		{
		case EditorCommandSelectionMode::Keep:
			return;
		case EditorCommandSelectionMode::Clear:
			refContext.pSelectionService->Clear();
			return;
		case EditorCommandSelectionMode::Entity:
		{
			if (!refContext.pSceneService || refSelection.uEntityId == 0)
			{
				refContext.pSelectionService->Clear();
				return;
			}

			const AshEngine::Entity entitySelected = refContext.pSceneService->FindEntity(refSelection.uEntityId);
			if (entitySelected.is_valid())
			{
				refContext.pSelectionService->Select({
					EditorSelectionKind::Entity,
					entitySelected.get_id(),
					entitySelected.get_name(),
					{}
				});
			}
			else
			{
				refContext.pSelectionService->Clear();
			}
			return;
		}
		case EditorCommandSelectionMode::Entities:
		{
			if (!refContext.pSceneService || refSelection.vecEntityIds.empty())
			{
				refContext.pSelectionService->Clear();
				return;
			}

			std::vector<EditorSelection> vecSelections{};
			vecSelections.reserve(refSelection.vecEntityIds.size());
			for (const SceneEntityId uEntityId : refSelection.vecEntityIds)
			{
				const AshEngine::Entity entitySelected = refContext.pSceneService->FindEntity(uEntityId);
				if (entitySelected.is_valid())
				{
					vecSelections.push_back({
						EditorSelectionKind::Entity,
						entitySelected.get_id(),
						entitySelected.get_name(),
						{}
					});
				}
			}

			if (vecSelections.empty())
			{
				refContext.pSelectionService->Clear();
			}
			else
			{
				refContext.pSelectionService->SelectRange(vecSelections);
			}
			return;
		}
		default:
			return;
		}
	}

	void UndoRedoService::NotifyHistoryChanged() const
	{
		if (!_pEventBus)
		{
			return;
		}

		EditorUndoHistoryChangedEvent event{};
		event.bCanUndo = CanUndo();
		event.bCanRedo = CanRedo();
		event.bHasOpenTransaction = HasOpenTransaction();
		event.strOpenTransactionLabel = GetOpenTransactionLabel();
		_pEventBus->Publish(event);
	}

	void UndoRedoService::NotifyTransactionStateChanged() const
	{
		if (!_pEventBus)
		{
			return;
		}

		EditorTransactionStateChangedEvent event{};
		event.bHasOpenTransaction = HasOpenTransaction();
		event.strLabel = GetOpenTransactionLabel();
		event.uPendingCommandCount = _upPendingTransaction ? _upPendingTransaction->vecCommands.size() : 0u;
		_pEventBus->Publish(event);
	}

	void UndoRedoService::NotifyDocumentDirtyStateChanged() const
	{
		if (!_pEventBus)
		{
			return;
		}

		EditorActiveDocumentDirtyStateChangedEvent event{};
		event.bDirty = IsDirty();
		_pEventBus->Publish(event);
	}

	uint64_t UndoRedoService::AllocateHistoryStateId()
	{
		return _uNextHistoryStateId++;
	}
}

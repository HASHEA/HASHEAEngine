#include "Services/UndoRedoService.h"

#include "Base/hlog.h"
#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"

#include <algorithm>
#include <limits>
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

	EditorCommandRecordResult UndoRedoService::RecordExecutedCommand(
		std::unique_ptr<EditorCommand> upCommand,
		EditorContext& refContext)
	{
		if (!upCommand)
		{
			return EditorCommandRecordResult::RollbackFailed;
		}

		auto rollback = [&refContext, this](std::unique_ptr<EditorCommand>& refCommand)
		{
			try
			{
				if (refCommand && refCommand->Undo(refContext))
				{
					ApplySelection(refContext, refCommand->GetSelectionAfterUndo());
					return EditorCommandRecordResult::RolledBack;
				}
			}
			catch (...)
			{
			}
			return EditorCommandRecordResult::RollbackFailed;
		};

		if (_upPendingTransaction)
		{
			return rollback(upCommand);
		}

		EditorCommandSelection selection{};
		try
		{
			_vecUndoStack.reserve(_vecUndoStack.size() + 1u);
			selection = upCommand->GetSelectionAfterExecute();
		}
		catch (...)
		{
			return rollback(upCommand);
		}

		_vecRedoStack.clear();
		const uint64_t uNewHistoryStateId = AllocateHistoryStateId();
		// The mutation already happened outside this service. Keep this commit path
		// non-virtual after reserve so a user-defined TryMerge cannot strand an
		// applied mutation without a history entry.
		_vecUndoStack.push_back(HistoryEntry{
			std::move(upCommand),
			uNewHistoryStateId
		});
		_uCurrentHistoryStateId = uNewHistoryStateId;

		auto observe_committed_state = [](auto&& fnObserve)
		{
			try
			{
				fnObserve();
			}
			catch (...)
			{
				// Selection and synchronous UI observers run after the history entry
				// is durable. Their failures must not escape the tri-state API or undo
				// an already committed domain mutation.
			}
		};
		observe_committed_state([&]() { ApplySelection(refContext, selection); });
		observe_committed_state([&]() { NotifyHistoryChanged(); });
		observe_committed_state([&]() { NotifyDocumentDirtyStateChanged(); });
		return EditorCommandRecordResult::Recorded;
	}

	std::size_t UndoRedoService::RemoveCommandsForDocument(
		const EditorCommandDocumentKey& refKey)
	{
		if (_upPendingTransaction)
		{
			return 0u;
		}

		std::vector<bool> vecRemoveUndo(_vecUndoStack.size(), false);
		std::vector<bool> vecRemoveRedo(_vecRedoStack.size(), false);
		std::size_t uRemovedUndoCount = 0;
		std::size_t uRemovedRedoCount = 0;

		for (std::size_t uIndex = 0; uIndex < _vecUndoStack.size(); ++uIndex)
		{
			const HistoryEntry& refEntry = _vecUndoStack[uIndex];
			const std::optional<EditorCommandDocumentKey> key =
				refEntry.upCommand ? refEntry.upCommand->GetDocumentKey() : std::nullopt;
			if (key.has_value() && *key == refKey)
			{
				vecRemoveUndo[uIndex] = true;
				++uRemovedUndoCount;
			}
		}

		for (std::size_t uIndex = 0; uIndex < _vecRedoStack.size(); ++uIndex)
		{
			const HistoryEntry& refEntry = _vecRedoStack[uIndex];
			const std::optional<EditorCommandDocumentKey> key =
				refEntry.upCommand ? refEntry.upCommand->GetDocumentKey() : std::nullopt;
			if (key.has_value() && *key == refKey)
			{
				vecRemoveRedo[uIndex] = true;
				++uRemovedRedoCount;
			}
		}

		const std::size_t uRemovedCount = uRemovedUndoCount + uRemovedRedoCount;
		if (uRemovedCount == 0u)
		{
			return 0u;
		}

		if (_vecUndoStack.size() >
			std::numeric_limits<std::size_t>::max() - _vecRedoStack.size())
		{
			return 0u;
		}
		const std::size_t uTimelineEntryCount =
			_vecUndoStack.size() + _vecRedoStack.size();
		if (uRemovedCount > uTimelineEntryCount)
		{
			return 0u;
		}
		const std::size_t uSurvivorCount = uTimelineEntryCount - uRemovedCount;
		// Reserve IDs for every survivor, an optional unreachable-saved sentinel,
		// the next state ID, and the increment performed by its first allocation.
		if (static_cast<uintmax_t>(uSurvivorCount) >
			std::numeric_limits<uint64_t>::max() - uint64_t{ 3 })
		{
			return 0u;
		}

		std::vector<uint64_t> vecUndoStateIds(_vecUndoStack.size(), 0u);
		std::vector<uint64_t> vecRedoStateIds(_vecRedoStack.size(), 0u);
		uint64_t uNextRemappedStateId = 1u;
		bool bSavedStateFound = _uSavedHistoryStateId == 0u;
		uint64_t uRemappedSavedStateId = 0u;

		auto map_entry = [this, &bSavedStateFound, &uRemappedSavedStateId, &uNextRemappedStateId](
			const HistoryEntry& refEntry,
			const bool bRemove,
			uint64_t& refOutStateId)
		{
			if (!bRemove)
			{
				refOutStateId = uNextRemappedStateId++;
			}
			if (refEntry.uStateId == _uSavedHistoryStateId)
			{
				bSavedStateFound = true;
				uRemappedSavedStateId = uNextRemappedStateId - 1u;
			}
		};

		for (std::size_t uIndex = 0; uIndex < _vecUndoStack.size(); ++uIndex)
		{
			map_entry(
				_vecUndoStack[uIndex],
				vecRemoveUndo[uIndex],
				vecUndoStateIds[uIndex]);
		}
		for (std::size_t uOffset = _vecRedoStack.size(); uOffset > 0u; --uOffset)
		{
			const std::size_t uIndex = uOffset - 1u;
			map_entry(
				_vecRedoStack[uIndex],
				vecRemoveRedo[uIndex],
				vecRedoStateIds[uIndex]);
		}

		if (!bSavedStateFound)
		{
			uRemappedSavedStateId = uNextRemappedStateId++;
		}

		std::vector<HistoryEntry> vecNewUndoStack{};
		std::vector<HistoryEntry> vecNewRedoStack{};
		vecNewUndoStack.reserve(_vecUndoStack.size() - uRemovedUndoCount);
		vecNewRedoStack.reserve(_vecRedoStack.size() - uRemovedRedoCount);

		for (std::size_t uIndex = 0; uIndex < _vecUndoStack.size(); ++uIndex)
		{
			if (!vecRemoveUndo[uIndex])
			{
				_vecUndoStack[uIndex].uStateId = vecUndoStateIds[uIndex];
				vecNewUndoStack.push_back(std::move(_vecUndoStack[uIndex]));
			}
		}
		for (std::size_t uIndex = 0; uIndex < _vecRedoStack.size(); ++uIndex)
		{
			if (!vecRemoveRedo[uIndex])
			{
				_vecRedoStack[uIndex].uStateId = vecRedoStateIds[uIndex];
				vecNewRedoStack.push_back(std::move(_vecRedoStack[uIndex]));
			}
		}

		_vecUndoStack.swap(vecNewUndoStack);
		_vecRedoStack.swap(vecNewRedoStack);
		_uCurrentHistoryStateId =
			_vecUndoStack.empty() ? 0u : _vecUndoStack.back().uStateId;
		_uSavedHistoryStateId = uRemappedSavedStateId;
		_uNextHistoryStateId = uNextRemappedStateId;
		NotifyHistoryChanged();
		NotifyDocumentDirtyStateChanged();
		return uRemovedCount;
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

	bool UndoRedoService::RemoveCommandsForTerrainAsset(
		const AshEngine::TerrainAssetId assetId) noexcept
	{
		if (assetId == 0u)
		{
			return true;
		}

		const uint64_t uPreviousSavedStateId = _uSavedHistoryStateId;
		uint64_t uMappedSavedStateId = uPreviousSavedStateId;
		uint64_t uLastRetainedStateId = 0u;
		bool bSavedStateReachable = uPreviousSavedStateId == 0u;
		bool bHistoryChanged = false;
		bool bRemovedCommand = false;

		const auto visitEntry = [&](HistoryEntry& refEntry)
		{
			const uint64_t uPreviousEntryStateId = refEntry.uStateId;
			const bool bRemove = refEntry.upCommand &&
				refEntry.upCommand->GetAffectedTerrainAssetId() == assetId;
			if (bRemove)
			{
				bRemovedCommand = true;
				bHistoryChanged = true;
				if (uPreviousEntryStateId == uPreviousSavedStateId)
				{
					uMappedSavedStateId = uLastRetainedStateId;
					bSavedStateReachable = true;
				}
				return;
			}

			if (bHistoryChanged)
			{
				refEntry.uStateId = AllocateHistoryStateId();
			}
			uLastRetainedStateId = refEntry.uStateId;
			if (uPreviousEntryStateId == uPreviousSavedStateId)
			{
				uMappedSavedStateId = refEntry.uStateId;
				bSavedStateReachable = true;
			}
		};

		for (HistoryEntry& refEntry : _vecUndoStack)
		{
			visitEntry(refEntry);
		}
		for (auto itEntry = _vecRedoStack.rbegin(); itEntry != _vecRedoStack.rend(); ++itEntry)
		{
			visitEntry(*itEntry);
		}

		if (!bRemovedCommand)
		{
			return true;
		}

		const auto removeAffectedAsset = [assetId](const HistoryEntry& refEntry)
		{
			return refEntry.upCommand &&
				refEntry.upCommand->GetAffectedTerrainAssetId() == assetId;
		};
		_vecUndoStack.erase(
			std::remove_if(_vecUndoStack.begin(), _vecUndoStack.end(), removeAffectedAsset),
			_vecUndoStack.end());
		_vecRedoStack.erase(
			std::remove_if(_vecRedoStack.begin(), _vecRedoStack.end(), removeAffectedAsset),
			_vecRedoStack.end());

		_uCurrentHistoryStateId = _vecUndoStack.empty() ? 0u : _vecUndoStack.back().uStateId;
		if (bSavedStateReachable)
		{
			_uSavedHistoryStateId = uMappedSavedStateId;
		}
		// History removal is the commit. Observers are best-effort and must not
		// turn an already-committed removal into an exception visible to reload.
		try
		{
			NotifyHistoryChanged();
		}
		catch (...)
		{
		}
		try
		{
			NotifyDocumentDirtyStateChanged();
		}
		catch (...)
		{
		}
		return true;
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

	uint64_t UndoRedoService::GetCurrentHistoryStateId() const
	{
		return _uCurrentHistoryStateId;
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

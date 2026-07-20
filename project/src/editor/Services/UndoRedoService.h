#pragma once

#include "Core/IEditorCommandExecutor.h"
#include "Function/Asset/TerrainData.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace AshEditor
{
	class EditorCommand;
	struct EditorCommandDocumentKey;
	struct EditorCommandSelection;
	struct EditorContext;
	class EditorEventBus;
	enum class EditorCommandRecordResult : uint8_t;

	class UndoRedoService
	{
	public:
		~UndoRedoService();

		// Optional event bus used to broadcast history / transaction / dirty state changes.
		void SetEventBus(EditorEventBus* pEventBus);

		// Executes a command and records it in undo history (or appends it to the active transaction).
		// - command: ownership is transferred; nullptr is treated as no-op failure.
		// - context: provides access to services needed by the command and selection updates.
		// Returns true only if the command execute path succeeded and state was recorded.
		bool Execute(std::unique_ptr<EditorCommand> upCommand, EditorContext& refContext);

		// Records a command whose state mutation has already succeeded without executing it again.
		// If recording is unavailable, the command is synchronously undone and the result reports
		// whether that rollback succeeded. Already-executed commands are always recorded as
		// standalone entries: this path never invokes user-defined merge logic after the external
		// mutation has committed.
		EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<EditorCommand> upCommand,
			EditorContext& refContext);

		bool RemoveCommandsForTerrainAsset(
			AshEngine::TerrainAssetId assetId) noexcept;

		// Removes complete history entries that belong exclusively to one document. Mixed
		// composites and unscoped commands are retained. Returns zero while a transaction is open.
		std::size_t RemoveCommandsForDocument(const EditorCommandDocumentKey& refKey);

		// Undoes the last committed command.
		// Returns false if there is an open transaction, history is empty, or undo failed (history is preserved).
		bool Undo(EditorContext& refContext);

		// Redoes the last undone command.
		// Returns false if there is an open transaction, redo stack is empty, or re-execute failed (history is preserved).
		bool Redo(EditorContext& refContext);

		// Starts a transaction to group multiple commands into one undo entry.
		// - label: UI-facing label for the transaction (used by history UI).
		// Returns false if a transaction is already open.
		bool BeginTransaction(std::string strLabel);

		// Commits the open transaction.
		// - If no commands were added, commit is a no-op success and closes the transaction.
		// - If multiple commands exist, they are wrapped into a CompositeCommand with the transaction label.
		// Returns false if there is no open transaction.
		bool CommitTransaction();

		// Cancels the open transaction by undoing any commands executed during the transaction in reverse order.
		// If a command undo fails, the transaction cancel continues best-effort (selection is updated only for successful undos).
		void CancelTransaction(EditorContext& refContext);

		// Clears undo/redo history and closes any open transaction.
		void Clear();

		// Marks the current history state as "saved" (used to compute dirty state).
		void MarkSaved();

		bool CanUndo() const;
		bool CanRedo() const;
		bool HasOpenTransaction() const;
		const std::string& GetOpenTransactionLabel() const;
		uint64_t GetCurrentHistoryStateId() const;
		bool IsDirty() const;

	private:
		struct HistoryEntry
		{
			std::unique_ptr<EditorCommand> upCommand{};
			uint64_t uStateId = 0;
		};

		struct PendingTransaction
		{
			std::string strLabel{};
			std::vector<std::unique_ptr<EditorCommand>> vecCommands{};
		};

		bool ExecuteStandalone(std::unique_ptr<EditorCommand> upCommand, EditorContext& refContext);
		bool ExecuteTransactional(std::unique_ptr<EditorCommand> upCommand, EditorContext& refContext);
		void PushUndoCommand(std::unique_ptr<EditorCommand> upCommand, uint64_t uStateId);
		void ApplySelection(EditorContext& refContext, const EditorCommandSelection& refSelection) const;
		void NotifyHistoryChanged() const;
		void NotifyTransactionStateChanged() const;
		void NotifyDocumentDirtyStateChanged() const;
		uint64_t AllocateHistoryStateId();

	private:
		EditorEventBus* _pEventBus = nullptr;
		std::vector<HistoryEntry> _vecUndoStack{};
		std::vector<HistoryEntry> _vecRedoStack{};
		std::unique_ptr<PendingTransaction> _upPendingTransaction{};
		uint64_t _uCurrentHistoryStateId = 0;
		uint64_t _uSavedHistoryStateId = 0;
		uint64_t _uNextHistoryStateId = 1;
	};
}

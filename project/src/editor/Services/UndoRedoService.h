#pragma once
#include "Core/EditorCommand.h"
#include <memory>
#include <string>
#include <vector>

namespace AshEditor
{
	struct EditorContext;

	class UndoRedoService
	{
	public:
		bool execute(std::unique_ptr<EditorCommand> command, EditorContext& context);
		bool undo(EditorContext& context);
		bool redo(EditorContext& context);
		bool begin_transaction(std::string label);
		bool commit_transaction();
		void cancel_transaction(EditorContext& context);
		void clear();

		bool can_undo() const;
		bool can_redo() const;
		bool has_open_transaction() const;
		const std::string& get_open_transaction_label() const;

	private:
		struct PendingTransaction
		{
			std::string label{};
			std::vector<std::unique_ptr<EditorCommand>> commands{};
		};

		bool execute_standalone(std::unique_ptr<EditorCommand> command, EditorContext& context);
		bool execute_transactional(std::unique_ptr<EditorCommand> command, EditorContext& context);
		void push_undo_command(std::unique_ptr<EditorCommand> command);
		void apply_selection(EditorContext& context, const EditorCommandSelection& selection) const;

	private:
		std::vector<std::unique_ptr<EditorCommand>> m_undoStack{};
		std::vector<std::unique_ptr<EditorCommand>> m_redoStack{};
		std::unique_ptr<PendingTransaction> m_pendingTransaction{};
	};
}

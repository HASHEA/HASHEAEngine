#include "Services/UndoRedoService.h"
#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include <utility>

namespace AshEditor
{
	bool UndoRedoService::execute(std::unique_ptr<EditorCommand> command, EditorContext& context)
	{
		if (!command)
		{
			return false;
		}

		return m_pendingTransaction
			? execute_transactional(std::move(command), context)
			: execute_standalone(std::move(command), context);
	}

	bool UndoRedoService::undo(EditorContext& context)
	{
		if (m_pendingTransaction || m_undoStack.empty())
		{
			return false;
		}

		std::unique_ptr<EditorCommand> command = std::move(m_undoStack.back());
		m_undoStack.pop_back();
		if (!command->undo(context))
		{
			m_undoStack.push_back(std::move(command));
			return false;
		}

		apply_selection(context, command->get_selection_after_undo());
		m_redoStack.push_back(std::move(command));
		return true;
	}

	bool UndoRedoService::redo(EditorContext& context)
	{
		if (m_pendingTransaction || m_redoStack.empty())
		{
			return false;
		}

		std::unique_ptr<EditorCommand> command = std::move(m_redoStack.back());
		m_redoStack.pop_back();
		if (!command->execute(context))
		{
			m_redoStack.push_back(std::move(command));
			return false;
		}

		apply_selection(context, command->get_selection_after_execute());
		m_undoStack.push_back(std::move(command));
		return true;
	}

	bool UndoRedoService::begin_transaction(std::string label)
	{
		if (m_pendingTransaction)
		{
			return false;
		}

		m_pendingTransaction = std::make_unique<PendingTransaction>();
		m_pendingTransaction->label = std::move(label);
		return true;
	}

	bool UndoRedoService::commit_transaction()
	{
		if (!m_pendingTransaction)
		{
			return false;
		}

		if (m_pendingTransaction->commands.empty())
		{
			m_pendingTransaction.reset();
			return true;
		}

		if (m_pendingTransaction->commands.size() == 1)
		{
			push_undo_command(std::move(m_pendingTransaction->commands.front()));
			m_pendingTransaction.reset();
			return true;
		}

		auto composite = std::make_unique<CompositeCommand>(m_pendingTransaction->label);
		for (std::unique_ptr<EditorCommand>& command : m_pendingTransaction->commands)
		{
			composite->append(std::move(command));
		}
		push_undo_command(std::move(composite));
		m_pendingTransaction.reset();
		return true;
	}

	void UndoRedoService::cancel_transaction(EditorContext& context)
	{
		if (!m_pendingTransaction)
		{
			return;
		}

		for (auto it = m_pendingTransaction->commands.rbegin(); it != m_pendingTransaction->commands.rend(); ++it)
		{
			if (*it && (*it)->undo(context))
			{
				apply_selection(context, (*it)->get_selection_after_undo());
			}
		}
		m_pendingTransaction.reset();
	}

	void UndoRedoService::clear()
	{
		m_pendingTransaction.reset();
		m_undoStack.clear();
		m_redoStack.clear();
	}

	bool UndoRedoService::can_undo() const
	{
		return !m_pendingTransaction && !m_undoStack.empty();
	}

	bool UndoRedoService::can_redo() const
	{
		return !m_pendingTransaction && !m_redoStack.empty();
	}

	bool UndoRedoService::has_open_transaction() const
	{
		return m_pendingTransaction != nullptr;
	}

	const std::string& UndoRedoService::get_open_transaction_label() const
	{
		static const std::string k_empty_label{};
		return m_pendingTransaction ? m_pendingTransaction->label : k_empty_label;
	}

	bool UndoRedoService::execute_standalone(std::unique_ptr<EditorCommand> command, EditorContext& context)
	{
		if (!command || !command->execute(context))
		{
			return false;
		}

		m_redoStack.clear();
		apply_selection(context, command->get_selection_after_execute());
		push_undo_command(std::move(command));
		return true;
	}

	bool UndoRedoService::execute_transactional(std::unique_ptr<EditorCommand> command, EditorContext& context)
	{
		if (!m_pendingTransaction || !command || !command->execute(context))
		{
			return false;
		}

		if (m_pendingTransaction->commands.empty())
		{
			m_redoStack.clear();
		}

		apply_selection(context, command->get_selection_after_execute());
		m_pendingTransaction->commands.push_back(std::move(command));
		return true;
	}

	void UndoRedoService::push_undo_command(std::unique_ptr<EditorCommand> command)
	{
		if (!command)
		{
			return;
		}

		if (!m_undoStack.empty() && m_undoStack.back()->try_merge(*command))
		{
			return;
		}

		m_undoStack.push_back(std::move(command));
	}

	void UndoRedoService::apply_selection(EditorContext& context, const EditorCommandSelection& selection) const
	{
		if (!context.selection_service)
		{
			return;
		}

		switch (selection.mode)
		{
		case EditorCommandSelectionMode::Keep:
			return;
		case EditorCommandSelectionMode::Clear:
			context.selection_service->clear();
			return;
		case EditorCommandSelectionMode::Entity:
			if (!context.scene_service || selection.entity_id == 0)
			{
				context.selection_service->clear();
				return;
			}

			if (const AshEngine::Entity entity = context.scene_service->find_entity(selection.entity_id); entity.is_valid())
			{
				context.selection_service->select({ EditorSelectionKind::Entity, entity.get_id(), entity.get_name(), {} });
			}
			else
			{
				context.selection_service->clear();
			}
			return;
		default:
			return;
		}
	}
}

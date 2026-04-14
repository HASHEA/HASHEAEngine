#include "Services/UndoRedoService.h"
#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include <utility>

namespace AshEditor
{
	bool UndoRedoService::execute(std::unique_ptr<EditorCommand> command, EditorContext& context)
	{
		if (!command || !command->execute(context))
		{
			return false;
		}

		m_redoStack.clear();
		m_undoStack.push_back(std::move(command));
		return true;
	}

	bool UndoRedoService::undo(EditorContext& context)
	{
		if (m_undoStack.empty())
		{
			return false;
		}

		std::unique_ptr<EditorCommand> command = std::move(m_undoStack.back());
		m_undoStack.pop_back();
		command->undo(context);
		m_redoStack.push_back(std::move(command));
		return true;
	}

	bool UndoRedoService::redo(EditorContext& context)
	{
		if (m_redoStack.empty())
		{
			return false;
		}

		std::unique_ptr<EditorCommand> command = std::move(m_redoStack.back());
		m_redoStack.pop_back();
		if (!command->execute(context))
		{
			return false;
		}

		m_undoStack.push_back(std::move(command));
		return true;
	}

	bool UndoRedoService::can_undo() const
	{
		return !m_undoStack.empty();
	}

	bool UndoRedoService::can_redo() const
	{
		return !m_redoStack.empty();
	}
}

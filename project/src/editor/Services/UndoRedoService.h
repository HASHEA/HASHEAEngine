#pragma once
#include "Core/EditorCommand.h"
#include <memory>
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

		bool can_undo() const;
		bool can_redo() const;

	private:
		std::vector<std::unique_ptr<EditorCommand>> m_undoStack{};
		std::vector<std::unique_ptr<EditorCommand>> m_redoStack{};
	};
}

#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include <utility>

namespace AshEditor
{
	CompositeCommand::CompositeCommand(std::string label)
		: m_label(std::move(label))
	{
	}

	void CompositeCommand::append(std::unique_ptr<EditorCommand> command)
	{
		if (command)
		{
			m_commands.push_back(std::move(command));
		}
	}

	bool CompositeCommand::is_empty() const
	{
		return m_commands.empty();
	}

	size_t CompositeCommand::get_command_count() const
	{
		return m_commands.size();
	}

	std::unique_ptr<EditorCommand> CompositeCommand::release_single_command()
	{
		if (m_commands.size() != 1)
		{
			return nullptr;
		}

		std::unique_ptr<EditorCommand> command = std::move(m_commands.front());
		m_commands.clear();
		return command;
	}

	const char* CompositeCommand::get_label() const
	{
		return m_label.empty() ? "Composite Command" : m_label.c_str();
	}

	bool CompositeCommand::execute(EditorContext& context)
	{
		size_t executed_count = 0;
		for (std::unique_ptr<EditorCommand>& command : m_commands)
		{
			if (!command || !command->execute(context))
			{
				for (size_t rollback_index = executed_count; rollback_index > 0; --rollback_index)
				{
					m_commands[rollback_index - 1]->undo(context);
				}
				return false;
			}
			++executed_count;
		}
		return true;
	}

	bool CompositeCommand::undo(EditorContext& context)
	{
		size_t undone_count = 0;
		for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
		{
			if (!(*it) || !(*it)->undo(context))
			{
				for (size_t replay_index = 0; replay_index < undone_count; ++replay_index)
				{
					m_commands[m_commands.size() - undone_count + replay_index]->execute(context);
				}
				return false;
			}
			++undone_count;
		}
		return true;
	}

	EditorCommandSelection CompositeCommand::get_selection_after_execute() const
	{
		return m_commands.empty()
			? EditorCommandSelection::keep()
			: m_commands.back()->get_selection_after_execute();
	}

	EditorCommandSelection CompositeCommand::get_selection_after_undo() const
	{
		return m_commands.empty()
			? EditorCommandSelection::keep()
			: m_commands.front()->get_selection_after_undo();
	}
}

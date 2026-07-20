#include "Core/EditorCommand.h"

#include "Core/EditorContext.h"

#include <utility>

namespace AshEditor
{
	bool EditorCommandDocumentKey::operator==(const EditorCommandDocumentKey& refOther) const
	{
		return strDomain == refOther.strDomain && strIdentity == refOther.strIdentity;
	}

	bool EditorCommandDocumentKey::operator!=(const EditorCommandDocumentKey& refOther) const
	{
		return !(*this == refOther);
	}

	std::optional<EditorCommandDocumentKey> EditorCommand::GetDocumentKey() const
	{
		return std::nullopt;
	}

	CompositeCommand::CompositeCommand(std::string strLabel)
		: _strLabel(std::move(strLabel))
	{
	}

	void CompositeCommand::Append(std::unique_ptr<EditorCommand> upCommand)
	{
		if (upCommand)
		{
			_vecCommands.push_back(std::move(upCommand));
		}
	}

	bool CompositeCommand::IsEmpty() const
	{
		return _vecCommands.empty();
	}

	size_t CompositeCommand::GetCommandCount() const
	{
		return _vecCommands.size();
	}

	std::unique_ptr<EditorCommand> CompositeCommand::ReleaseSingleCommand()
	{
		if (_vecCommands.size() != 1)
		{
			return nullptr;
		}

		std::unique_ptr<EditorCommand> upCommand = std::move(_vecCommands.front());
		_vecCommands.clear();
		return upCommand;
	}

	const char* CompositeCommand::GetLabel() const
	{
		return _strLabel.empty() ? "Composite Command" : _strLabel.c_str();
	}

	bool CompositeCommand::Execute(EditorContext& refContext)
	{
		size_t uExecutedCount = 0;
		for (std::unique_ptr<EditorCommand>& upCommand : _vecCommands)
		{
			if (!upCommand || !upCommand->Execute(refContext))
			{
				for (size_t uRollbackIndex = uExecutedCount; uRollbackIndex > 0; --uRollbackIndex)
				{
					_vecCommands[uRollbackIndex - 1]->Undo(refContext);
				}
				return false;
			}
			++uExecutedCount;
		}
		return true;
	}

	bool CompositeCommand::Undo(EditorContext& refContext)
	{
		size_t uUndoneCount = 0;
		for (
			std::vector<std::unique_ptr<EditorCommand>>::reverse_iterator itCommand = _vecCommands.rbegin();
			itCommand != _vecCommands.rend();
			++itCommand)
		{
			if (!(*itCommand) || !(*itCommand)->Undo(refContext))
			{
				for (size_t uReplayIndex = 0; uReplayIndex < uUndoneCount; ++uReplayIndex)
				{
					_vecCommands[_vecCommands.size() - uUndoneCount + uReplayIndex]->Execute(refContext);
				}
				return false;
			}
			++uUndoneCount;
		}
		return true;
	}

	std::optional<EditorCommandDocumentKey> CompositeCommand::GetDocumentKey() const
	{
		if (_vecCommands.empty() || !_vecCommands.front())
		{
			return std::nullopt;
		}

		const std::optional<EditorCommandDocumentKey> key =
			_vecCommands.front()->GetDocumentKey();
		if (!key.has_value())
		{
			return std::nullopt;
		}

		for (size_t uIndex = 1; uIndex < _vecCommands.size(); ++uIndex)
		{
			if (!_vecCommands[uIndex] || _vecCommands[uIndex]->GetDocumentKey() != key)
			{
				return std::nullopt;
			}
		}
		return key;
	}

	EditorCommandSelection CompositeCommand::GetSelectionAfterExecute() const
	{
		return _vecCommands.empty()
			? EditorCommandSelection::Keep()
			: _vecCommands.back()->GetSelectionAfterExecute();
	}

	EditorCommandSelection CompositeCommand::GetSelectionAfterUndo() const
	{
		return _vecCommands.empty()
			? EditorCommandSelection::Keep()
			: _vecCommands.front()->GetSelectionAfterUndo();
	}
}

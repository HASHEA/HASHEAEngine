#pragma once

#include <memory>

namespace AshEditor
{
	class EditorCommand;

	class IEditorCommandExecutor
	{
	public:
		virtual ~IEditorCommandExecutor() = default;

		virtual bool ExecuteCommand(std::unique_ptr<EditorCommand> upCommand) = 0;
	};
}

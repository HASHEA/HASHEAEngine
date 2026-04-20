#pragma once

namespace AshEditor
{
	struct EditorContext;

	class EditorCommand
	{
	public:
		virtual ~EditorCommand() = default;

		virtual const char* get_label() const = 0;
		virtual bool execute(EditorContext& context) = 0;
		virtual bool undo(EditorContext& context) = 0;
	};
}

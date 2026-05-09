#pragma once

namespace AshEditor
{
	class CommandService;
	class IEditorActionHandler;

	struct EditorActionRegistrarContext
	{
		CommandService& refCommandService;
		IEditorActionHandler* pHandler = nullptr;
	};

	class EditorActionRegistrar final
	{
	public:
		static void Register(EditorActionRegistrarContext& refContext);
	};
}

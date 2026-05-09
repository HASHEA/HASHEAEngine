#pragma once

#include <cstdint>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class CommandService;
	enum class EditorActionScope : uint8_t;

	class EditorShortcutService
	{
	public:
		// Dispatches all actions registered for a given scope (via CommandService) using UIContext key-chord polling.
		// Returns true if at least one action callback executed.
		bool DispatchScope(
			const CommandService& refCommandService,
			EditorActionScope eScope,
			const AshEngine::UIContext& refUiContext) const;
	};
}

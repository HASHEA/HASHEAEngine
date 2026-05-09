#pragma once

#include <cstdint>
#include <string>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class CommandService;

	struct EditorCommandPaletteContext
	{
		AshEngine::UIContext& refUi;
		CommandService& refCommandService;
	};

	class EditorCommandPaletteController
	{
	public:
		void RequestOpen();
		void Draw(EditorCommandPaletteContext& refContext);

	private:
		bool _bOpenRequested = false;
		std::string _strFilter{};
		int32_t _iSelectedIndex = 0;
	};
}

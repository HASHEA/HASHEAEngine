#include "Window.h"
#include "WindowWin.h"
#include "Base/hmemory.h"
namespace AshEngine
{
	std::unique_ptr<Window> Window::create(const WindowData& data)
	{
#ifdef ASH_WINDOWS
		return std::make_unique<WindowWin>(data);
#endif // Ash_Windows
	}
}

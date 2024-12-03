#include "Window.h"
#include "WindowWin.h"
#include "Base/hmemory.h"
namespace AshEngine
{
	Window* Window::create(const WindowData& data)
	{
#ifdef ASH_WINDOWS
		return Ash_New<WindowWin>(nullptr,data);
#endif // Ash_Windows
	}
}

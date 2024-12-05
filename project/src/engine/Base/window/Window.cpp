#include "Window.h"
#include "WindowWin.h"
#include "Base/hmemory.h"
namespace AshEngine
{
	Window* Window::create()
	{
#ifdef ASH_WINDOWS
		return Ash_New<WindowWin>();
#endif // Ash_Windows
	}
}

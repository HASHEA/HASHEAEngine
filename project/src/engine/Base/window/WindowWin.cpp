#include "WindowWin.h"
#include "Base/hassert.h"
#include "Base/hstring.h"
namespace AshEngine
{
	WindowWin::WindowWin(const WindowData& w_data) : data(w_data)
	{
	}
	auto WindowWin::on_update() -> void
	{
	}
	auto WindowWin::get_width() -> uint32_t
	{
		return data.width;
	}
	auto WindowWin::get_height() -> uint32_t
	{
		return data.height;
	}
	auto WindowWin::set_vsync(bool vsync) -> void
	{
#ifdef ASH_VULKAN
#else
		H_ASSERTLOG(false,"Fatal : unknown API !");
#endif // ASH_VULKAN
	}
	auto WindowWin::is_vsync() -> bool
	{
		return data.vsync;
	}
	auto WindowWin::init() -> void
	{
		H_ASSERTLOG(glfwInit(),"Fatal : failed to init glfw !");
#ifdef ASH_VULKAN
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
		H_ASSERTLOG(false,"Fatal : unknown API !");
#endif // ASH_VULKAN
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		glfwWindowHint(GLFW_SAMPLES, 4);
		handle = glfwCreateWindow(data.width, data.height, data.title, nullptr, nullptr);
		if (!handle)
		{
			HLogError("Fatal : failed to create glfw window !");
			return;
		}
		set_title(data.title);
		glfwMakeContextCurrent(handle);
		register_native_event(data);
		glfwSetInputMode(handle,GLFW_STICKY_KEYS,1);
		glfwSetWindowUserPointer(handle,this);

	}
	auto WindowWin::shutdown() -> void
	{
		glfwDestroyWindow(handle);
		glfwTerminate();
	}
	auto WindowWin::set_title(const char* title) -> void
	{
		StringBuffer sb{};
		sb.init(2 * sizeof(title),nullptr); 
		sb.append(title);
#ifdef ASH_VULKAN// PENGINE_VULKAN
		sb.append("(Vulkan)");
#else
		H_ASSERTLOG(false, "Fatal : unknown API !");
#endif
		glfwSetWindowTitle(handle, sb.m_pData);
		sb.shutdown();
	}
	auto WindowWin::get_native_interface() -> void*
	{
		return handle;
	}
	auto WindowWin::register_native_event(const WindowData& data) -> void
	{
		glfwSetWindowSizeCallback(handle, [](GLFWwindow* win, int32_t w, int32_t h) {

			});
		glfwSetWindowIconifyCallback(handle, [](GLFWwindow* win, int minimized) {
			
			});

		glfwSetWindowCloseCallback(handle, [](GLFWwindow* win) {
			
			});

		glfwSetMouseButtonCallback(handle, [](GLFWwindow* window, int32_t btnId, int32_t state, int32_t mods) {
			auto w = (WindowWin*)glfwGetWindowUserPointer(window);

			double x;
			double y;
			glfwGetCursorPos(window, &x, &y);

			});

		glfwSetCursorPosCallback(handle, [](GLFWwindow* window, double x, double y) {
			
			});

		glfwSetKeyCallback(handle, [](GLFWwindow*, int32_t key, int32_t scancode, int32_t action, int32_t mods) {
			switch (action)
			{
			case GLFW_PRESS: {
				
				break;
			}
			case GLFW_RELEASE: {
				
				break;
			}
			}
			});
		glfwSetScrollCallback(handle, [](GLFWwindow* win, double xOffset, double yOffset) {
			
			});
	}
}
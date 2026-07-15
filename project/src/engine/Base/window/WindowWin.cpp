#include "WindowWin.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include "Base/hstring.h"
namespace AshEngine
{
	namespace
	{
		static auto make_window_event(WindowEventType type, uint32_t width, uint32_t height) -> WindowEvent
		{
			WindowEvent event{};
			event.type = type;
			event.width = width;
			event.height = height;
			return event;
		}
	}

	auto WindowWin::on_update() -> void
	{
		glfwPollEvents();
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
		data.vsync = vsync;
	}
	auto WindowWin::is_vsync() -> bool
	{
		return data.vsync;
	}
	auto WindowWin::init(const WindowConfig& w_data) -> void
	{
		data = w_data;
		H_ASSERTLOG(glfwInit(),"Fatal : failed to init glfw !");
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		glfwWindowHint(GLFW_SAMPLES, 4);
		glfwWindowHint(GLFW_DECORATED, data.exactClientExtent ? GLFW_FALSE : GLFW_TRUE);
		handle = glfwCreateWindow(data.width, data.height, data.title, nullptr, nullptr);
		if (!handle)
		{
			HLogError("Fatal : failed to create glfw window !");
			return;
		}
		int32_t actual_width = 0;
		int32_t actual_height = 0;
		glfwGetWindowSize(handle, &actual_width, &actual_height);
		data.width = static_cast<uint32_t>(actual_width > 0 ? actual_width : 0);
		data.height = static_cast<uint32_t>(actual_height > 0 ? actual_height : 0);
		if (data.exactClientExtent)
		{
			HLogInfo(
				"WindowWin: exact client extent requested {}x{}, actual {}x{}.",
				w_data.width,
				w_data.height,
				data.width,
				data.height);
		}
		shouldClose = false;
		set_title(data.title);
		glfwSetWindowUserPointer(handle,this);
		register_native_event(data);
		glfwSetInputMode(handle,GLFW_STICKY_KEYS,1);
		if (data.backend == RHI::Backend::Vulkan)
		{
			glfwMakeContextCurrent(handle);
			uint32_t extensionCount = 0;
			auto pEx = glfwGetRequiredInstanceExtensions(&extensionCount);
			extensions.init(nullptr, extensionCount, extensionCount);
			memory_copy(extensions.m_pData, pEx, extensionCount * sizeof(pEx[0]));
		}
	}
	auto WindowWin::shutdown() -> void
	{
		shouldClose = true;
		extensions.shutdown();
		glfwDestroyWindow(handle);
		glfwTerminate();
	}

	auto WindowWin::destroy() -> void
	{
		WindowWin* self = this;
		Ash_Delete(nullptr, self);
	}
	auto WindowWin::set_title(const char* title) -> void
	{
		StringBuffer sb{};
		sb.init(MAX_TITLE_LENGTH,nullptr);
		sb.append(title);
		sb.append("(");
		sb.append(RHI::backend_to_string(data.backend));
		sb.append(")");
		glfwSetWindowTitle(handle, sb.m_pData);
		sb.shutdown();
	}
	auto WindowWin::get_native_interface() -> void*
	{
		return handle;
	}
	auto WindowWin::register_native_event(const WindowConfig& data) -> void
	{
		glfwSetWindowSizeCallback(handle, [](GLFWwindow* win, int32_t w, int32_t h) {
			auto* window = static_cast<WindowWin*>(glfwGetWindowUserPointer(win));
			if (!window)
			{
				return;
			}

			window->data.width = static_cast<uint32_t>(w > 0 ? w : 0);
			window->data.height = static_cast<uint32_t>(h > 0 ? h : 0);
			window->push_event(make_window_event(WindowEventType::Resize, window->data.width, window->data.height));
			});
		glfwSetWindowIconifyCallback(handle, [](GLFWwindow* win, int _minimized) {
			auto* window = static_cast<WindowWin*>(glfwGetWindowUserPointer(win));
			if (!window)
			{
				return;
			}

			window->minimized = _minimized == 1;
			window->push_event(make_window_event(window->minimized ? WindowEventType::Minimized : WindowEventType::Restored, window->data.width, window->data.height));
			});

		glfwSetWindowCloseCallback(handle, [](GLFWwindow* win) {
			auto* window = static_cast<WindowWin*>(glfwGetWindowUserPointer(win));
			if (!window)
			{
				return;
			}

			window->shouldClose = true;
			window->push_event(make_window_event(WindowEventType::CloseRequested, window->data.width, window->data.height));
			});

		glfwSetMouseButtonCallback(handle, [](GLFWwindow* window, int32_t btnId, int32_t state, int32_t mods) {
			auto* w = static_cast<WindowWin*>(glfwGetWindowUserPointer(window));
			if (!w)
			{
				return;
			}

			double x;
			double y;
			glfwGetCursorPos(window, &x, &y);
			WindowEvent event = make_window_event(state == GLFW_RELEASE ? WindowEventType::MouseButtonReleased : WindowEventType::MouseButtonPressed, w->data.width, w->data.height);
			event.mouseButton = btnId;
			event.mods = mods;
			event.mouseX = x;
			event.mouseY = y;
			w->push_event(event);
			});

		glfwSetCursorPosCallback(handle, [](GLFWwindow* window, double x, double y) {
			auto* w = static_cast<WindowWin*>(glfwGetWindowUserPointer(window));
			if (!w)
			{
				return;
			}

			WindowEvent event = make_window_event(WindowEventType::MouseMoved, w->data.width, w->data.height);
			event.mouseX = x;
			event.mouseY = y;
			w->push_event(event);
			});

		glfwSetKeyCallback(handle, [](GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods) {
			auto* w = static_cast<WindowWin*>(glfwGetWindowUserPointer(window));
			if (!w)
			{
				return;
			}

			switch (action)
			{
			case GLFW_PRESS:
			case GLFW_REPEAT:
			{
				WindowEvent event = make_window_event(WindowEventType::KeyPressed, w->data.width, w->data.height);
				event.key = key;
				event.scancode = scancode;
				event.mods = mods;
				event.repeated = action == GLFW_REPEAT;
				w->push_event(event);
				break;
			}
			case GLFW_RELEASE:
			{
				WindowEvent event = make_window_event(WindowEventType::KeyReleased, w->data.width, w->data.height);
				event.key = key;
				event.scancode = scancode;
				event.mods = mods;
				w->push_event(event);
				break;
			}
			default:
				break;
			}
			});
		glfwSetCharCallback(handle, [](GLFWwindow* window, uint32_t codepoint) {
			auto* w = static_cast<WindowWin*>(glfwGetWindowUserPointer(window));
			if (!w)
			{
				return;
			}

			WindowEvent event = make_window_event(WindowEventType::TextInput, w->data.width, w->data.height);
			event.codepoint = codepoint;
			w->push_event(event);
			});
		glfwSetScrollCallback(handle, [](GLFWwindow* win, double xOffset, double yOffset) {
			auto* window = static_cast<WindowWin*>(glfwGetWindowUserPointer(win));
			if (!window)
			{
				return;
			}

			WindowEvent event = make_window_event(WindowEventType::MouseScrolled, window->data.width, window->data.height);
			event.scrollX = xOffset;
			event.scrollY = yOffset;
			window->push_event(event);
			});
	}
	auto WindowWin::is_minimized() -> bool
	{
		return minimized;
	}
	auto WindowWin::should_close() -> bool
	{
		return shouldClose;
	}
	auto WindowWin::poll_event(WindowEvent& outEvent) -> bool
	{
		return pop_event(outEvent);
	}
}

#pragma once
#include "Window.h"
#include "GLFW/glfw3.h"
namespace AshEngine
{
#define MAX_TITLE_LENGTH 128
	//useless because we use glfwwindow to do cross platform
	class ASH_API WindowWin : public Window
	{
	public:
		WindowWin() = default;
		virtual ~WindowWin() {};

	public:
		auto on_update() -> void override;

		auto get_width() -> uint32_t override;

		auto get_height() -> uint32_t override;

		auto set_vsync(bool vsync) -> void override;

		auto is_vsync() -> bool override;

		auto init(const WindowConfig& w_data) -> void override;

		auto shutdown() -> void override;

		auto set_title(const char* title) -> void override;

		auto get_native_interface() -> void* override;

		auto should_close() -> bool override;
	private:
		auto register_native_event(const WindowConfig& data) -> void;
		WindowConfig data{};
		GLFWwindow* handle = nullptr;
		static bool shouldClose;

		

	};
};
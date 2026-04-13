#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include "Base/ds/harray.hpp"
#include "Graphics/RHIBackend.h"
#include <vector>
namespace AshEngine
{
	enum class WindowEventType
	{
		None,
		Resize,
		Minimized,
		Restored,
		CloseRequested,
		KeyPressed,
		KeyReleased,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled,
	};

	struct WindowEvent
	{
		WindowEventType type = WindowEventType::None;
		uint32_t width = 0;
		uint32_t height = 0;
		int32_t key = -1;
		int32_t scancode = 0;
		int32_t mods = 0;
		int32_t mouseButton = -1;
		bool repeated = false;
		double mouseX = 0.0;
		double mouseY = 0.0;
		double scrollX = 0.0;
		double scrollY = 0.0;
	};

	struct WindowConfig
	{
		uint32_t	width;
		uint32_t	height;
		bool		vsync;
		const char*	title;
		RHI::Backend backend = RHI::Backend::Default;
	};
	class ASH_API Window
	{
	public:
		Window() = default;
		virtual ~Window() {}
		virtual auto on_update() -> void = 0;
		virtual auto get_width() -> uint32_t = 0;
		virtual auto get_height() -> uint32_t = 0;
		virtual auto set_vsync(bool vsync) -> void = 0;
		virtual auto is_vsync() -> bool = 0;
		virtual auto init(const WindowConfig& w_data) -> void = 0;
		virtual auto shutdown() -> void = 0;
		virtual auto destroy() -> void = 0;
		virtual auto set_title(const char* title) -> void = 0;
		virtual auto get_native_interface() -> void* = 0;
		virtual auto get_native_window() -> void* { return nullptr; }
		virtual auto should_close() -> bool = 0;
		virtual auto is_minimized() -> bool = 0;
		virtual auto poll_event(WindowEvent& outEvent) -> bool = 0;
		static Window* create();
		inline auto get_scale() const
		{
			return scale;
		}

		inline auto get_extensions() const& -> const Array<const char*>&
		{
			return extensions;
		}
	protected:
		inline auto push_event(const WindowEvent& event) -> void
		{
			pendingEvents.push_back(event);
		}

		inline auto pop_event(WindowEvent& outEvent) -> bool
		{
			if (pendingEvents.empty())
			{
				return false;
			}

			outEvent = pendingEvents.front();
			pendingEvents.erase(pendingEvents.begin());
			return true;
		}

		float scale = 1.f;
		Array<const char*> extensions;
		std::vector<WindowEvent> pendingEvents{};
	};
}

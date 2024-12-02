#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include "Base/ds/harray.hpp"
#include <memory>
namespace AshEngine
{
	struct WindowData
	{
		uint32_t	width;
		uint32_t	height;
		bool		vsync;
		const char*	title;
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
		virtual auto init() -> void = 0;
		virtual auto shutdown() -> void = 0;
		virtual auto set_title(const char* title) -> void = 0;
		virtual auto get_native_interface() -> void* = 0;
		virtual auto get_native_window() -> void* { return nullptr; }

		static std::unique_ptr<Window> create(const WindowData& data);
		inline auto get_scale() const
		{
			return scale;
		}

		inline auto get_extensions() const&
		{
			return extensions;
		}
	protected:
		float scale = 1.f;
		Array<const char*> extensions;
	};
}
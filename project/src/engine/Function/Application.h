#pragma once
#include "Base/hcore.h"
#include "Base/hplatform.h"
namespace RHI
{
	class GraphicsContext;
	class Swapchain;
}

namespace AshEngine
{
	class Window;
	class ASH_API Application
	{
	public:
		Application();
		~Application();
		inline static Application* get()
		{
			return app;
		}
	public:
		inline static auto& get_graphics_context()
		{
			return get()->graphicsContext;
		}
		inline static auto& get_window()
		{
			return get()->window;
		}
		inline static auto get_swapchain()
		{
			return get()->swapChain;
		}
		auto start() -> void;
	protected:
		virtual auto _on_update() -> void;
		virtual auto _on_gui() -> void;
		virtual auto _on_render_debug() -> void;
		virtual auto _on_render() -> void;
		virtual auto _present() -> void;
	public:
		static Application* app;
	protected:
		Window*					window					= nullptr;
		RHI::GraphicsContext*	graphicsContext			= nullptr;
		RHI::Swapchain*			swapChain					= nullptr;
	};
};
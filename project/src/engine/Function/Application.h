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
	class RenderDevice;
	class Renderer;
	class Window;
	struct ASH_API EngineInitConfig
	{
		uint32_t initWidth = 0;
		uint32_t initHeight = 0;
		uint32_t swapchainBufferCount = 3;
		const char* title = nullptr;
		bool bVsync = false;
	};
	class ASH_API Application
	{
	public:
		Application(const EngineInitConfig& config);
		~Application();
		inline static Application* get()
		{
			return app;
		}
	public:
		inline static auto& get_window()
		{
			return get()->window;
		}
#if defined(ASH_ENGINE)
		inline static auto& get_graphics_context()
		{
			return get()->graphicsContext;
		}
		inline static auto get_swapchain()
		{
			return get()->swapChain;
		}
		inline static auto get_render_device()
		{
			return get()->renderDevice;
		}
#endif
		inline static auto get_renderer()
		{
			return get()->renderer;
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
		RHI::Swapchain*			swapChain				= nullptr;
		RenderDevice*			renderDevice			= nullptr;
		Renderer*				renderer				= nullptr;
	};
};

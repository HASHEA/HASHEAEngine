#pragma once
#include "Base/hcore.h"
#include "Base/input/Input.h"
#include "Base/hplatform.h"
#include "Graphics/RHIBackend.h"
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
	struct WindowEvent;
	struct ASH_API EngineInitConfig
	{
		uint32_t initWidth = 0;
		uint32_t initHeight = 0;
		uint32_t swapchainBufferCount = 3;
		const char* title = nullptr;
		bool bVsync = false;
		RHI::Backend backend = RHI::Backend::Default;
		const char* backendConfigPath = "product/config/Engine.ini";
	};
	class ASH_API Application
	{
	public:
		Application(const EngineInitConfig& config);
		virtual ~Application();
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
		inline static auto& get_input()
		{
			return get()->inputState;
		}
		auto request_exit() -> void;
		auto set_max_frame_count(uint64_t inMaxFrameCount) -> void;
		auto get_frame_index() const -> uint64_t
		{
			return frameIndex;
		}
		auto start() -> void;
	protected:
		auto _pump_platform_events() -> void;
		auto _tick_frame() -> void;
		auto _render_frame() -> void;
		auto _present_frame() -> void;
		auto _should_render_frame() const -> bool;
		auto _should_exit() const -> bool;
		auto _process_window_events() -> void;
		auto _handle_window_event(const WindowEvent& event) -> void;
		virtual auto _on_startup() -> void;
		virtual auto _on_shutdown() -> void;
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
		InputState				inputState{};
		uint64_t				frameIndex				= 0;
		uint64_t				maxFrameCount			= 0;
		bool					exitRequested			= false;
		bool					started					= false;
	};
};

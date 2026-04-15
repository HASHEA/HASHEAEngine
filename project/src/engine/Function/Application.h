#pragma once
#include "Base/hcore.h"
#include "Base/input/Input.h"
#include "Base/hplatform.h"
#include "Base/hthreading.h"
#include "Graphics/RHIBackend.h"
#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
namespace RHI
{
	class GraphicsContext;
	class Swapchain;
}

namespace AshEngine
{
	class RenderDevice;
	class Renderer;
	class UIContext;
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
		EngineThreadingConfig threading{};
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
		inline static auto get_ui_context()
		{
			return get() ? get()->uiContext : nullptr;
		}
		inline static auto& get_input()
		{
			return get()->_get_thread_input_state();
		}
		inline static auto get_rhi_backend() -> RHI::Backend
		{
			return get()->activeBackend;
		}
		inline static auto get_rhi_backend_name() -> const char*
		{
			return RHI::backend_to_string(get()->activeBackend);
		}
		auto request_exit() -> void;
		auto set_max_frame_count(uint64_t inMaxFrameCount) -> void;
		auto set_max_run_seconds(double inMaxRunSeconds) -> void;
		auto get_frame_index() const -> uint64_t
		{
			return frameIndex.load(std::memory_order_acquire);
		}
		auto is_logic_thread_enabled() const -> bool
		{
			return logicThreadEnabled;
		}
		auto start() -> void;
	protected:
		auto _pump_platform_events() -> void;
		auto _tick_frame() -> void;
		auto _render_frame() -> void;
		auto _present_frame() -> void;
		auto _should_render_frame() const -> bool;
		auto _should_exit() const -> bool;
		auto _should_logic_exit() const -> bool;
		auto _process_window_events() -> void;
		auto _handle_window_event(const WindowEvent& event) -> void;
		auto _start_logic_thread_if_needed() -> void;
		auto _stop_logic_thread() -> void;
		auto _logic_thread_main() -> void;
		auto _capture_logic_thread_failure(std::exception_ptr exception) -> void;
		auto _check_logic_thread_failure() -> void;
		auto _publish_logic_input_snapshot() -> void;
		auto _consume_logic_input_snapshot() -> void;
		auto _get_thread_input_state() -> InputState&;
		virtual auto _on_startup() -> void;
		virtual auto _on_shutdown() -> void;
		virtual auto _on_update() -> void;
		virtual auto _on_logic_startup() -> void;
		virtual auto _on_logic_shutdown() -> void;
		virtual auto _on_logic_update() -> void;
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
		UIContext*				uiContext				= nullptr;
		RHI::Backend			activeBackend			= RHI::Backend::Default;
		EngineThreadingConfig	threadingConfig{};
		InputState				inputState{};
		InputState				logicInputState{};
		InputState				pendingLogicInputState{};
		std::mutex				pendingLogicInputMutex{};
		std::chrono::steady_clock::time_point runStartTime{};
		std::thread				logicThread{};
		std::mutex				logicThreadFailureMutex{};
		std::exception_ptr		logicThreadException{};
		std::string				logicThreadFailureMessage{};
		std::atomic<uint64_t>	frameIndex				{ 0 };
		uint64_t				maxFrameCount			= 0;
		double					maxRunSeconds			= 0.0;
		std::atomic<bool>		exitRequested			{ false };
		std::atomic<bool>		logicThreadStopRequested{ false };
		std::atomic<bool>		logicThreadRunning		{ false };
		std::atomic<bool>		logicThreadFailed		{ false };
		bool					threadingInitialized	= false;
		bool					logicThreadEnabled		= false;
		bool					pendingLogicInputDirty	= false;
		bool					started					= false;
	};
};

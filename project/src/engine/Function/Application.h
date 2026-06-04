#pragma once
#include "Base/hcore.h"
#include "Base/input/Input.h"
#include "Base/hplatform.h"
#include "Base/hthreading.h"
#include "Function/Gui/UICommon.h"
#include "Function/Diagnostics/PerfGate.h"
#include "Graphics/RHIBackend.h"
#include "Function/Render/DebugDrawService.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/ScenePresentationSubsystem.h"
#include "Function/Render/SceneRenderer.h"
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
		std::string uiIniPath{};
		UIThemePreset uiThemePreset = UIThemePreset::SlateStudio;
		std::string uiThemeId{};
		std::string uiThemeDefinition{};
		// editor begin 修改原因：让编辑器显式控制 ImGui 多视口能力，支持面板拖出为原生系统窗口。
		bool bUiEnableViewports = false;
		// editor end
		// editor begin 修改原因：允许编辑器从配置与资源目录向引擎透传字体、粗体和中文字形策略。
		std::string uiFontPath{};
		std::string uiFontMergePath{};
		std::string uiStrongFontPath{};
		std::string uiStrongFontMergePath{};
		float uiFontSizePixels = 17.0f;
		bool bUiUseFullChineseGlyphRange = false;
		// editor end
		EngineThreadingConfig threading{};
	};
	class ASH_API Application
	{
public:
		Application(const EngineInitConfig& config);
		virtual ~Application();
		auto initialize() -> bool;
		auto initialize(const EngineInitConfig& config) -> bool;
		inline static Application* get()
		{
			return app;
		}
	public:
		inline static auto& get_window()
		{
			return get()->window;
		}
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
		inline static auto get_renderer()
		{
			return get()->renderer;
		}
		inline static auto get_ui_context()
		{
			return get() ? get()->uiContext : nullptr;
		}
		inline static auto get_scene_presentation()
		{
			return get() ? &get()->scenePresentation : nullptr;
		}
		inline static auto get_debug_draw_service()
		{
			return get() ? &get()->debugDrawService : nullptr;
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
		auto draw_engine_overlay() -> void;
		auto request_exit() -> void;
		auto set_max_frame_count(uint64_t inMaxFrameCount) -> void;
		auto set_max_run_seconds(double inMaxRunSeconds) -> void;
		auto configure_perf_gate(const PerfGateConfig& config) -> void;
		auto get_frame_index() const -> uint64_t
		{
			return frameIndex.load(std::memory_order_acquire);
		}
		auto is_initialized() const -> bool
		{
			return initialized;
		}
		auto is_logic_thread_enabled() const -> bool
		{
			return logicThreadEnabled;
		}
		auto get_render_asset_manager() -> RenderAssetManager&
		{
			return renderAssetManager;
		}
		auto get_scene_renderer() -> SceneRenderer&
		{
			return sceneRenderer;
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
		auto _run_scene_presentation_update_phase() -> void;
		auto _run_scene_presentation_submit_phase() -> void;
		auto _shutdown_runtime() -> void;
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
		EngineInitConfig		initConfig{};
		RHI::Backend			activeBackend			= RHI::Backend::Default;
		RenderAssetManager		renderAssetManager{};
		SceneRenderer			sceneRenderer{};
		ScenePresentationSubsystem scenePresentation{};
		DebugDrawService		debugDrawService{};
		PerfGateController		perfGateController{};
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
		bool					initialized				= false;
		bool					logServiceInitialized	= false;
		bool					memoryServiceInitialized = false;
	};
};

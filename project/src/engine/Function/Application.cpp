#include "Application.h"
#include "Graphics/DynamicRHI.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/Swapchain.h"
#include "Function/Gui/UIContext.h"
#include "Function/Render/RenderDevice.h"
#include "Function/Render/Renderer.h"
#include "Base/hlog.h"
#include "Base/hmemory.h"
#include "Base/hfile.h"
#include "Base/window/Window.h"
#include "Base/hcache.h"
#include "Graphics/RHICommon.h"
namespace AshEngine
{
	namespace
	{
		static auto resolve_application_rhi_config(const EngineInitConfig& config) -> RHI::RuntimeRHIConfig
		{
			RHI::RuntimeRHIConfig runtimeConfig = RHI::load_runtime_rhi_config(config.backendConfigPath);
			if (config.backend != RHI::Backend::Default)
			{
				runtimeConfig.backend = config.backend;
			}

			runtimeConfig.backend = RHI::resolve_runtime_backend(runtimeConfig.backend);
			H_ASSERTLOG(runtimeConfig.backend != RHI::Backend::Default, "Failed to resolve an available graphics backend.");
			return runtimeConfig;
		}
	}

	Application* Application::app = nullptr;
	Application::Application(const EngineInitConfig& config)
		: threadingConfig(config.threading)
	{
		app = this;
	
		/*init at very first to ensure log*/
		LogService::instance()->init(nullptr);
		MemoryService::instance()->init(nullptr);
		register_current_thread_role(EngineThreadRole::Render);
		threadingInitialized = initialize_threading(threadingConfig);
		if (!threadingInitialized)
		{
			HLogWarning("Engine threading initialization failed. Falling back to single-threaded application execution.");
		}
		logicThreadEnabled = threadingInitialized && threadingConfig.enable_logic_thread;
		if (threadingConfig.enable_logic_thread && !logicThreadEnabled)
		{
			HLogWarning("Logic-thread mode was requested but is unavailable because engine threading initialization did not complete.");
		}

		const RHI::RuntimeRHIConfig runtimeRhiConfig = resolve_application_rhi_config(config);
		const RHI::Backend resolvedBackend = runtimeRhiConfig.backend;
		activeBackend = resolvedBackend;
		HLogInfo("Initializing engine RHI backend: {}", RHI::backend_to_string(resolvedBackend));

		/*window*/
		WindowConfig windowConfig = { config.initWidth, config.initHeight, config.bVsync, config.title, resolvedBackend };
		window = Window::create();
		window->init(windowConfig);

		/*gfx*/
		RHI::GraphicsContextInitConfig gfxConfig{};
		gfxConfig.window = window->get_native_interface();
		gfxConfig.width = window->get_width();
		gfxConfig.height = window->get_height();
		gfxConfig.backend = resolvedBackend;
		gfxConfig.vulkanValidation = runtimeRhiConfig.vulkanValidation;
		gfxConfig.dx12Validation = runtimeRhiConfig.dx12Validation;
		const auto& windowExtensions = window->get_extensions();
		gfxConfig.addtionalExtensions.init(nullptr, windowExtensions.size(), 0);
		for (uint32_t extensionIndex = 0; extensionIndex < windowExtensions.size(); ++extensionIndex)
		{
			gfxConfig.addtionalExtensions.push_back(windowExtensions[extensionIndex]);
		}
		graphicsContext = RHI::GraphicsContext::create(resolvedBackend);
		H_ASSERTLOG(graphicsContext != nullptr, "Failed to create graphics context for backend '{}'.", RHI::backend_to_string(resolvedBackend));
		if (!graphicsContext)
		{
			return;
		}
		graphicsContext->init(&gfxConfig);

		/*shader manager*/
		
		/*swapchain*/
		std::vector<RHI::AshColorSpace> colorSpace = { RHI::ASH_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		std::vector<RHI::AshFormat> format = { RHI::ASH_FORMAT_B8G8R8A8_SRGB };
		std::vector<RHI::AshPresentMode> presentMode = {RHI::ASH_PRESENT_MODE_MAILBOX_KHR };
		RHI::SwapChainInitConfig scConfig{};
		scConfig.swapchainBufferCount = config.swapchainBufferCount;
		scConfig.window = window->get_native_interface();
		scConfig.width = window->get_width();
		scConfig.height = window->get_height();
		scConfig.colorFormatCount = format.size();
		scConfig.pColorFormat = format.data();
		scConfig.colorSpaceCount = colorSpace.size();
		scConfig.pColorSpace = colorSpace.data();
		scConfig.presentModeCount = presentMode.size();
		scConfig.pPresentMode = presentMode.data();
		swapChain = RHI::Swapchain::create(resolvedBackend);
		H_ASSERTLOG(swapChain != nullptr, "Failed to create swapchain for backend '{}'.", RHI::backend_to_string(resolvedBackend));
		if (!swapChain)
		{
			return;
		}
		swapChain->init(&scConfig);
		renderDevice = new RenderDevice(graphicsContext, swapChain);
		renderer = new Renderer(renderDevice);
		uiContext = new UIContext();
		if (!uiContext->init(window, graphicsContext, renderDevice))
		{
			HLogWarning("UIContext initialization failed. Engine UI facade will remain disabled.");
			delete uiContext;
			uiContext = nullptr;
		}
	}
	Application::~Application()
	{
		_stop_logic_thread();
		if (threadingInitialized && !is_threading_shutting_down())
		{
			flush_render_commands();
		}
		if (graphicsContext)
		{
			graphicsContext->wait_idle();
		}
		shutdown_threading();
		delete renderer;
		renderer = nullptr;
		if (uiContext)
		{
			uiContext->shutdown();
			delete uiContext;
			uiContext = nullptr;
		}
		delete renderDevice;
		renderDevice = nullptr;
		if (swapChain)
		{
			swapChain->shutdown();
			swapChain->destroy();
			swapChain = nullptr;
		}
		if (graphicsContext)
		{
			graphicsContext->shutdown();
			graphicsContext->destroy();
			graphicsContext = nullptr;
		}
		if (window)
		{
			window->shutdown();
			window->destroy();
			window = nullptr;
		}
		app = nullptr;
		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
	}
	auto Application::request_exit() -> void
	{
		exitRequested.store(true, std::memory_order_release);
	}
	auto Application::set_max_frame_count(uint64_t inMaxFrameCount) -> void
	{
		maxFrameCount = inMaxFrameCount;
	}
	auto Application::set_max_run_seconds(double inMaxRunSeconds) -> void
	{
		maxRunSeconds = inMaxRunSeconds > 0.0 ? inMaxRunSeconds : 0.0;
	}
	auto Application::start() -> void
	{
		if (started)
		{
			return;
		}

		started = true;
		exitRequested.store(false, std::memory_order_release);
		logicThreadStopRequested.store(false, std::memory_order_release);
		logicThreadFailed.store(false, std::memory_order_release);
		frameIndex.store(0, std::memory_order_release);
		logicThreadException = nullptr;
		logicThreadFailureMessage.clear();
		runStartTime = std::chrono::steady_clock::now();
		_on_startup();
		_start_logic_thread_if_needed();

		while (!_should_exit())
		{
			_pump_platform_events();
			_tick_frame();
			_check_logic_thread_failure();
			pump_render_commands();

			if (_should_render_frame())
			{
				_render_frame();
				_present_frame();
			}
			else if (!has_pending_render_commands())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			pump_render_commands();

			const uint64_t currentFrameIndex = frameIndex.fetch_add(1, std::memory_order_acq_rel) + 1;
			if (maxFrameCount > 0 && currentFrameIndex >= maxFrameCount)
			{
				HLogInfo("Application smoke frame limit reached: {}", maxFrameCount);
				request_exit();
			}
			if (maxRunSeconds > 0.0)
			{
				const auto elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStartTime).count();
				if (elapsedSeconds >= maxRunSeconds)
				{
					HLogInfo("Application smoke time limit reached: {:.2f}", maxRunSeconds);
					request_exit();
				}
			}
		}

		_stop_logic_thread();
		if (threadingInitialized && !is_threading_shutting_down())
		{
			flush_render_commands();
		}
		_on_shutdown();
		started = false;
	}
	auto Application::_pump_platform_events() -> void
	{
		inputState.begin_frame();
		if (!window)
		{
			return;
		}

		window->on_update();
		_process_window_events();
		_publish_logic_input_snapshot();
		if (uiContext)
		{
			uiContext->begin_frame();
		}
	}
	auto Application::_tick_frame() -> void
	{
		if (!logicThreadEnabled)
		{
			_on_update();
		}
	}
	auto Application::_render_frame() -> void
	{
		_on_render();
	}
	auto Application::_present_frame() -> void
	{
		_present();
	}
	auto Application::_should_render_frame() const -> bool
	{
		return window != nullptr && !window->is_minimized();
	}
	auto Application::_should_exit() const -> bool
	{
		return exitRequested.load(std::memory_order_acquire) || window == nullptr || window->should_close();
	}
	auto Application::_should_logic_exit() const -> bool
	{
		return exitRequested.load(std::memory_order_acquire) || logicThreadStopRequested.load(std::memory_order_acquire);
	}
	auto Application::_on_startup() -> void
	{
	}
	auto Application::_on_shutdown() -> void
	{
	}
	auto Application::_on_update() -> void
	{
	}
	auto Application::_on_logic_startup() -> void
	{
	}
	auto Application::_on_logic_shutdown() -> void
	{
	}
	auto Application::_on_logic_update() -> void
	{
		_on_update();
	}
	auto Application::_process_window_events() -> void
	{
		if (!window)
		{
			return;
		}

		WindowEvent event{};
		while (window->poll_event(event))
		{
			_handle_window_event(event);
		}
	}
	auto Application::_handle_window_event(const WindowEvent& event) -> void
	{
		switch (event.type)
		{
		case WindowEventType::Resize:
			if (swapChain && event.width > 0 && event.height > 0)
			{
				swapChain->resize_swapchain(event.width, event.height);
			}
			break;
		case WindowEventType::Minimized:
			HLogInfo("Window minimized.");
			break;
		case WindowEventType::Restored:
			HLogInfo("Window restored.");
			break;
		case WindowEventType::CloseRequested:
			HLogInfo("Window close requested.");
			request_exit();
			break;
		case WindowEventType::KeyPressed:
			inputState.set_key_state(event.key, true, event.repeated);
			break;
		case WindowEventType::KeyReleased:
			inputState.set_key_state(event.key, false, false);
			break;
		case WindowEventType::TextInput:
			break;
		case WindowEventType::MouseButtonPressed:
			inputState.set_mouse_position(event.mouseX, event.mouseY);
			inputState.set_mouse_button_state(event.mouseButton, true);
			break;
		case WindowEventType::MouseButtonReleased:
			inputState.set_mouse_position(event.mouseX, event.mouseY);
			inputState.set_mouse_button_state(event.mouseButton, false);
			break;
		case WindowEventType::MouseMoved:
			inputState.set_mouse_position(event.mouseX, event.mouseY);
			break;
		case WindowEventType::MouseScrolled:
			inputState.add_scroll_delta(event.scrollX, event.scrollY);
			break;
		case WindowEventType::None:
		default:
			break;
		}

		if (uiContext)
		{
			uiContext->handle_window_event(event);
		}
	}
	auto Application::_start_logic_thread_if_needed() -> void
	{
		if (!logicThreadEnabled || logicThread.joinable())
		{
			return;
		}

		logicThreadStopRequested.store(false, std::memory_order_release);
		logicThread = std::thread([this]()
		{
			_logic_thread_main();
		});
	}
	auto Application::_stop_logic_thread() -> void
	{
		logicThreadStopRequested.store(true, std::memory_order_release);
		if (logicThread.joinable())
		{
			logicThread.join();
		}
		logicThreadRunning.store(false, std::memory_order_release);
	}
	auto Application::_logic_thread_main() -> void
	{
		register_current_thread_role(EngineThreadRole::Logic);
		logicThreadRunning.store(true, std::memory_order_release);
		HLogInfo("Logic thread started.");

		try
		{
			_consume_logic_input_snapshot();
			_on_logic_startup();
			while (!_should_logic_exit())
			{
				_consume_logic_input_snapshot();
				_on_logic_update();
				if (threadingConfig.logic_thread_idle_sleep_ms > 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(threadingConfig.logic_thread_idle_sleep_ms));
				}
			}
			_on_logic_shutdown();
		}
		catch (...)
		{
			_capture_logic_thread_failure(std::current_exception());
		}

		logicThreadRunning.store(false, std::memory_order_release);
		register_current_thread_role(EngineThreadRole::Unknown);
		HLogInfo("Logic thread stopped.");
	}
	auto Application::_capture_logic_thread_failure(std::exception_ptr exception) -> void
	{
		std::string failureMessage = "Unknown logic-thread failure.";
		if (exception)
		{
			try
			{
				std::rethrow_exception(exception);
			}
			catch (const std::exception& caughtException)
			{
				failureMessage = caughtException.what();
			}
			catch (...)
			{
				failureMessage = "Logic thread threw a non-standard exception.";
			}
		}

		{
			std::scoped_lock<std::mutex> lock(logicThreadFailureMutex);
			logicThreadException = exception;
			logicThreadFailureMessage = failureMessage;
		}

		logicThreadFailed.store(true, std::memory_order_release);
		exitRequested.store(true, std::memory_order_release);
		HLogError("Logic thread aborted: {}", failureMessage);
	}
	auto Application::_check_logic_thread_failure() -> void
	{
		if (logicThreadFailed.load(std::memory_order_acquire))
		{
			request_exit();
		}
	}
	auto Application::_publish_logic_input_snapshot() -> void
	{
		if (!logicThreadEnabled)
		{
			return;
		}

		std::scoped_lock<std::mutex> lock(pendingLogicInputMutex);
		pendingLogicInputState = inputState;
		pendingLogicInputDirty = true;
	}
	auto Application::_consume_logic_input_snapshot() -> void
	{
		if (!logicThreadEnabled)
		{
			return;
		}

		std::scoped_lock<std::mutex> lock(pendingLogicInputMutex);
		if (!pendingLogicInputDirty)
		{
			return;
		}

		logicInputState = pendingLogicInputState;
		pendingLogicInputDirty = false;
	}
	auto Application::_get_thread_input_state() -> InputState&
	{
		if (logicThreadEnabled && is_in_logic_thread())
		{
			return logicInputState;
		}
		return inputState;
	}
	auto Application::_on_gui() -> void
	{
	}
	auto Application::_on_render_debug() -> void
	{
	}
	auto Application::_on_render() -> void
	{
		if (renderer && renderer->begin_frame())
		{
			_on_render_debug();
			_on_gui();
			renderer->end_frame();
		}
	}
	auto Application::_present() -> void
	{
		if (renderer)
		{
			renderer->present();
		}
	}

};

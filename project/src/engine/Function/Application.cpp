#pragma once
#include "Application.h"
#include "Graphics/DynamicRHI.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/Swapchain.h"
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
	{
		app = this;
	
		/*init at very first to ensure log*/
		LogService::instance()->init(nullptr);
		MemoryService::instance()->init(nullptr);

		const RHI::RuntimeRHIConfig runtimeRhiConfig = resolve_application_rhi_config(config);
		const RHI::Backend resolvedBackend = runtimeRhiConfig.backend;
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
	}
	Application::~Application()
	{
		app = nullptr;
		if (graphicsContext)
		{
			graphicsContext->wait_idle();
		}
		delete renderer;
		renderer = nullptr;
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
		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
	}
	auto Application::request_exit() -> void
	{
		exitRequested = true;
	}
	auto Application::set_max_frame_count(uint64_t inMaxFrameCount) -> void
	{
		maxFrameCount = inMaxFrameCount;
	}
	auto Application::start() -> void
	{
		if (started)
		{
			return;
		}

		started = true;
		exitRequested = false;
		frameIndex = 0;
		_on_startup();

		while (!_should_exit())
		{
			_pump_platform_events();
			_tick_frame();

			if (_should_render_frame())
			{
				_render_frame();
				_present_frame();
			}

			++frameIndex;
			if (maxFrameCount > 0 && frameIndex >= maxFrameCount)
			{
				HLogInfo("Application smoke frame limit reached: {}", maxFrameCount);
				request_exit();
			}
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
	}
	auto Application::_tick_frame() -> void
	{
		_on_update();
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
		return exitRequested || window == nullptr || window->should_close();
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

#pragma once
#include "Application.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/Swapchain.h"
#include "Base/hlog.h"
#include "Base/hmemory.h"
#include "Base/window/Window.h"
#include "Graphics/RHICommon.h"
namespace AshEngine
{
	Application* Application::app = nullptr;
	Application::Application()
	{
		/*init at very first to ensure log*/
		LogService::instance()->init(nullptr);
		MemoryService::instance()->init(nullptr);

		/*window*/
		WindowConfig windowConfig = { 1280, 720, false, "Ash Engine" };
		window = Window::create();
		window->init(windowConfig);

		/*gfx*/
		RHI::GraphicsContextInitConfig gfxConfig{};
		gfxConfig.window = window->get_native_interface();
		gfxConfig.width = window->get_width();
		gfxConfig.height = window->get_height();
		gfxConfig.addtionalExtensions = window->get_extensions();
		graphicsContext = RHI::GraphicsContext::create();
		graphicsContext->init(&gfxConfig);

		/*swapchain*/
		std::vector<RHI::AshColorSpace> colorSpace = { RHI::ASH_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		std::vector<RHI::AshFormat> format = { RHI::ASH_FORMAT_B8G8R8A8_SRGB };
		std::vector<RHI::AshPresentMode> presentMode = {RHI::ASH_PRESENT_MODE_MAILBOX_KHR };
		RHI::SwapChainInitConfig scConfig{};
		scConfig.window = window->get_native_interface();
		scConfig.width = window->get_width();
		scConfig.height = window->get_height();
		scConfig.colorFormatCount = format.size();
		scConfig.pColorFormat = format.data();
		scConfig.colorSpaceCount = colorSpace.size();
		scConfig.pColorSpace = colorSpace.data();
		scConfig.presentModeCount = presentMode.size();
		scConfig.pPresentMode = presentMode.data();
		swapChain = RHI::Swapchain::create();
		swapChain->init(&scConfig);
	}
	Application::~Application()
	{
		swapChain->shutdown();
		Ash_Delete(nullptr, swapChain);
		graphicsContext->shutdown();
		Ash_Delete(nullptr,graphicsContext);
		window->shutdown();
		Ash_Delete(nullptr, window);
		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
	}
	auto Application::start() -> void
	{
		while (0)
		{

		}
	}
	auto Application::_on_update() -> void
	{
	}
	auto Application::_on_gui() -> void
	{
	}
	auto Application::_on_render_debug() -> void
	{
	}
};
#pragma once
#include "Application.h"
#include "Graphics/GraphicsContext.h"
#include "Base/hlog.h"
#include "Base/hmemory.h"
#include "Base/window/Window.h"
namespace AshEngine
{
	Application* Application::app = nullptr;
	Application::Application()
	{
		/*init at very first to ensure log*/
		LogService::instance()->init(nullptr);
		MemoryService::instance()->init(nullptr);

		/*window*/
		window = Window::create({ 1280, 720, false, "Ash Engine" });
		window->init();

		/*gfx*/
		RHI::GraphicsContextInitConfig gfxConfig{};
		graphicsContext = RHI::GraphicsContext::create();
		graphicsContext->init(&gfxConfig);


	}
	Application::~Application()
	{
		graphicsContext->shutdown();
		window->shutdown();
		MemoryService::instance()->shutdown();
		LogService::instance()->shutdown();
	}
	auto Application::start() -> void
	{
		/*while (1)
		{

		}*/
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
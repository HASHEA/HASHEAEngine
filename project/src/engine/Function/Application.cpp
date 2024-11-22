#pragma once
#include "Application.h"
#include "Graphics/GraphicsContext.h"
#include "Base/hlog.h"
#include "Base/hmemory.h"
namespace HASHEAENGINE
{
	Application::Application()
	{
		/*init at very first to ensure log*/
		LogService::instance()->init(nullptr);
		MemoryService::instance()->init(nullptr);

		/*window*/

		/*gfx*/
		RHI::GraphicsContextInitConfig gfxConfig{};
		graphicsContext = RHI::GraphicsContext::create();
		graphicsContext->init(&gfxConfig);


	}
	Application::~Application()
	{
	}
	auto Application::start() -> void
	{
		/*while (1)
		{

		}*/
	}
};
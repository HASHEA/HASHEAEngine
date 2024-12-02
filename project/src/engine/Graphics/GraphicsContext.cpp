#pragma once
#include "Base/hmemory.h"
#include "SwapChain.h"
#include"GraphicsContext.h"
#ifdef ASH_VULKAN
#include "Vullkan/VulkanSwapchain.h"
#include "Vullkan/VulkanContext.h"
#endif        // ASH_VULKAN
namespace RHI {
   
	std::shared_ptr<GraphicsContext> GraphicsContext::create()
	{
#ifdef ASH_VULKAN
		return Ash_New_Shared<VulkanContext>();
#else
		H_ASSERTLOG(false, "Unsupported Api!");
#endif        // ASH_VULKAN
	}

	std::shared_ptr<Swapchain> Swapchain::create(uint32_t width, uint32_t height)
	{
#ifdef ASH_VULKAN
		return Ash_New_Shared<VulkanSwapChain>(width, height);
#else
		H_ASSERTLOG(false, "Unsupported Api!");
#endif
	}
};
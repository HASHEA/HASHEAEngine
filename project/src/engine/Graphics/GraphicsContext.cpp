#pragma once
#include "Base/hmemory.h"
#include "SwapChain.h"
#include"GraphicsContext.h"
#ifdef ASH_VULKAN
#include "Vullkan/VulkanSwapchain.h"
#include "Vullkan/VulkanContext.h"
#endif        // ASH_VULKAN
namespace RHI {
   
	GraphicsContext* GraphicsContext::create()
	{
#ifdef ASH_VULKAN
		return Ash_New<VulkanContext>();
#else
		H_ASSERTLOG(false, "Unsupported Api!");
#endif        // ASH_VULKAN
	}

	Swapchain* Swapchain::create(uint32_t width, uint32_t height)
	{
#ifdef ASH_VULKAN
		return Ash_New<VulkanSwapChain>(nullptr,width, height);
#else
		H_ASSERTLOG(false, "Unsupported Api!");
#endif
	}
};
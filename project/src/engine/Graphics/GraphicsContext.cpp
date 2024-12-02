#pragma once
#include "Base/hmemory.h"

#include"GraphicsContext.h"
#ifdef ASH_VULKAN
#include "Vullkan/VulkanContext.h"
#endif        // ASH_VULKAN
namespace RHI {
   
	std::shared_ptr<GraphicsContext> GraphicsContext::create()
	{
#ifdef ASH_VULKAN
		return Ash_New_Shared<VulkanContext>();
#endif        // ASH_VULKAN
	}
};
#pragma once
#include "Base/hmemory.h"

#include"GraphicsContext.h"
#ifdef HASHEA_VULKAN
#include "Vullkan/VulkanContext.h"
#endif        // HASHEA_VULKAN
namespace RHI {
   
	std::shared_ptr<GraphicsContext> GraphicsContext::create()
	{
#ifdef HASHEA_VULKAN
		return Hashea_New_Shared<VulkanContext>();
#endif        // HASHEA_VULKAN
	}
};
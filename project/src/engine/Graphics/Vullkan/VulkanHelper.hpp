#pragma once
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "VulkanWrapper.h"
namespace RHI
{
	const char* vulkan_error_string(VkResult errorCode);
	enum DeviceExtensionAndFeaturesFlags
	{
		DynamicRendering,
		TimelineSemaphore,
		Synchronization2,
		MeshShaders,
		Multiview,
		FragmentShadingRate,
		RayTracing,
		RayQuery,
		Bindless,
	};



#define VK_CHECK_RESULT(f)                                                                                                        \
	{                                                                                                                             \
		VkResult res = (f);                                                                                                       \
		if (res != VK_SUCCESS)                                                                                                    \
		{                                                                                                                         \
			HLogError("Fatal : VK CALL ERROR : VkResult is \" {0} \" in {1} at line {2}", vulkan_error_string(res), __FILE__, __LINE__); \
			H_ASSERT(res == VK_SUCCESS);                                                                                            \
		}                                                                                                                         \
	}

	
};
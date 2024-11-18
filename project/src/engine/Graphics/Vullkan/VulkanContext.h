#pragma once
#include "VulkanWrapper.h"
#include "VulkanHelper.hpp"
#include "Base/hcore.h"
#include "Base/hstring.h"
#include "Base/hbit.hpp"
#include "Graphics/GraphicsContext.h"
#include "Base/ds/harray.hpp"
#include <vector>
#include <memory>
using namespace HASHEAENGINE;
namespace RHI
{
	constexpr uint32_t MAX_SWAPCHAIN_BUFFERS = 3;

	class VulkanCommandBuffer;
	class VulkanCommandPool;
	struct FrameData
	{
		std::shared_ptr<VulkanCommandPool> cmdPool;
		std::shared_ptr<VulkanCommandBuffer> cmdBuffer;
		VkQueryPool                     vulkanTimestampQueryPool = nullptr;
		VkQueryPool                     vulkanPipelineStatsQueryPool = nullptr;
		VkSemaphore presentSemaphore = VK_NULL_HANDLE;
	};

	struct VulkanContextInitConfig
	{
		GpuDescriptorPoolCreation descriptorPoolCreation{};
		void* window = nullptr;
		uint16_t                             width = 1;
		uint16_t                             height = 1;
		uint16_t                             num_threads = 1;
	};
	class VulkanContext
	{
	public:
		auto Init(void* config = nullptr) -> HS_Result;
		auto Shutdown() -> HS_Result;

		VulkanContext() {}
		~VulkanContext() {}
	public:
		auto GetDeviceExtensionEnabled(DeviceExtensionAndFeaturesFlags extension) -> bool
		{
			return featureSwitchFlags.GetBit(extension);
		}

		inline const auto GetVulkanDevice()
		{
			return vulkanDevice;
		}

	private:
		StringView stringBuffer{};
	//vk handles
	private:
		//manually in the calling order
		auto VulkanContext::_CreateInstance()->HS_Result;
#ifdef VULKAN_DEBUG_REPORT
		auto VulkanContext::_CreateDebugUtilMessengerExt()->HS_Result;
#endif
		auto VulkanContext::_SelectAndPreparePhysicalDevice()->HS_Result;
		auto VulkanContext::_FilterDeviceSelectableExtension()->HS_Result;
		auto VulkanContext::_QuerySurpportedProperties()->HS_Result;
		auto VulkanContext::_QuerySurpportedFeatures()->HS_Result;
		auto VulkanContext::_CreateDevice()->HS_Result;
		auto VulkanContext::_CreateVMA()->HS_Result;
		auto VulkanContext::_CreateDescriptorPool(const GpuDescriptorPoolCreation& dspci)->HS_Result;
		auto VulkanContext::_CreateFrameData(uint16_t numThread)->HS_Result;
	private:
		BitSetFixed<4> featureSwitchFlags;
		VkPhysicalDeviceFragmentShadingRatePropertiesKHR fragmentShadingRateProperties;
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;
		//Features
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR   rayTracingPipelineFeatures;
		VkPhysicalDeviceRayQueryFeaturesKHR             rayQueryFeatures;
		VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures;

	private:
		
		VkAllocationCallbacks*			vulkanAllocationCallbacks			= nullptr;
		VkInstance                      vulkanInstance						= VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT        vulkanDebugUtilMessenger			= VK_NULL_HANDLE;
		VkPhysicalDevice                vulkanPhysicalDevice				= VK_NULL_HANDLE;
		VkPhysicalDeviceProperties      vulkanPhysicalDeviceProperties;
		VkDevice                        vulkanDevice						= VK_NULL_HANDLE;
		VkQueue                         vulkanMainQueue						= VK_NULL_HANDLE;
		VkQueue                         vulkanComputeQueue					= VK_NULL_HANDLE;
		VkQueue                         vulkanTransferQueue					= VK_NULL_HANDLE;
		uint32_t						vulkanMainQueueFamily				= UINT32_MAX;
		uint32_t						vulkanComputeQueueFamily			= UINT32_MAX;
		uint32_t						vulkanTransferQueueFamily			= UINT32_MAX;
		VkDescriptorPool                vulkanDescriptorPool				= VK_NULL_HANDLE;
		VkDescriptorPool                vulkanBindlessDescriptorPool = VK_NULL_HANDLE;
		VmaAllocator                    vmaAllocator						= VK_NULL_HANDLE;
		FrameData						frameDatas[MAX_SWAPCHAIN_BUFFERS];
		Array<FrameData>				thread_frame_pools;
	private:
	
		float                           gpuTimestampFrequency = 0.f;
		size_t                          uboAlignment = 256;
		size_t                          ssboAlignemnt = 256;
		uint32_t                        subgroupSize = 32;
		uint32_t                        maxFramebufferLayers = 1;
		VkExtent2D                      minFragmentShadingRateTexelSize;

	};

};
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
using namespace AshEngine;
namespace RHI
{
	constexpr uint32_t MAX_SWAPCHAIN_BUFFERS = 3;

	class VulkanCommandBuffer;
	class VulkanCommandPool;
	struct GPUTimeQuery;
	struct GpuTimeQueryTree;
	struct GpuPipelineStatistics;
	struct GPUTimeQueriesManager;
	struct FramePool
	{
		std::shared_ptr<VulkanCommandPool>	cmdPool;
		VkQueryPool							vulkanTimestampQueryPool     = nullptr;
		VkQueryPool							vulkanPipelineStatsQueryPool = nullptr;
		GpuTimeQueryTree*					timeQueries = nullptr;
	};


	
	class VulkanContext : public GraphicsContext
	{
	public:
		auto init(void* config) -> HS_Result override;
		auto shutdown() -> HS_Result override;

		VulkanContext() { instance = this; }
		~VulkanContext() {}
	public:
		auto get_device_extension_enabled(DeviceExtensionAndFeaturesFlags extension) -> bool
		{
			return featureSwitchFlags.get_bit(extension);
		}

		inline const auto get_vulkan_device_internal()
		{
			return vulkanDevice;
		}

		inline const auto get_vulkan_instance_internal()
		{
			return vulkanInstance;
		}

		inline static const auto get_vulkan_device()
		{
			return instance->get_vulkan_device_internal();
		}

		inline static const auto get_vulkan_instance()
		{
			return instance->get_vulkan_instance_internal();
		}

		inline static const auto get()
		{
			return instance;
		}
	
	private:
		StringView stringBuffer{};
	//vk handles
	private:
		//manually in the calling order
		auto VulkanContext::_create_instance()->HS_Result;
#ifdef VULKAN_DEBUG_REPORT
		auto VulkanContext::_create_debug_util_messenger_ext()->HS_Result;
#endif
		auto VulkanContext::_select_and_prepare_physical_device()->HS_Result;
		auto VulkanContext::_filter_device_selectable_extension()->HS_Result;
		auto VulkanContext::_query_supported_props()->HS_Result;
		auto VulkanContext::_query_supported_features()->HS_Result;
		auto VulkanContext::_create_device()->HS_Result;
		auto VulkanContext::_create_vulkan_memory_allocator()->HS_Result;
		auto VulkanContext::_create_descriptor_pool(const GpuDescriptorPoolCreation& dspci)->HS_Result;
		auto VulkanContext::_create_frame_pool_and_data(uint16_t numThread, uint16_t numQueryTimes)->HS_Result;
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
		Array<FramePool>				framePools;
		std::shared_ptr<GPUTimeQueriesManager> gpuTimeQueryManager = nullptr;
	private:
	
		float                           gpuTimestampFrequency = 0.f;
		size_t                          uboAlignment = 256;
		size_t                          ssboAlignemnt = 256;
		uint32_t                        subgroupSize = 32;
		uint32_t                        maxFramebufferLayers = 1;
		VkExtent2D                      minFragmentShadingRateTexelSize;


		static VulkanContext* instance;
	};






};
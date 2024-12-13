#pragma once
#include "VulkanWrapper.h"
#include "VulkanHelper.hpp"
#include "Base/hcore.h"
#include "Base/hstring.h"
#include "Base/hbit.hpp"
#include "Graphics/GraphicsContext.h"
#include "Base/ds/harray.hpp"
#include "Base/hcommandQueue.hpp"
#include <vector>
#include <memory>
using namespace AshEngine;
namespace RHI
{
	constexpr uint32_t		  MAX_SWAPCHAIN_BUFFERS = 3;
	constexpr uint32_t        k_bindless_texture_binding = 10;
	constexpr uint32_t        k_bindless_image_binding = 11;
	constexpr uint32_t        k_max_bindless_resources = 1024;
	constexpr uint32_t        k_max_frames = 2;
	constexpr uint32_t		  k_command_buffer_queue_length = 128;

	class VulkanCommandBuffer;
	class VulkanCommandPool;
	class VulkanFence;
	class VulkanBuffer;
	class VulkanDynamicBuffer;
	struct VulkanCommandBufferManager;
	struct GPUTimeQuery;
	struct GpuTimeQueryTree;
	struct GpuPipelineStatistics;
	struct GPUTimeQueriesManager;
	
	//count = numThread * numFrame
	struct FramePool
	{
		VulkanCommandPool*					cmdPool						 = nullptr;
		VkQueryPool							vulkanTimestampQueryPool     = VK_NULL_HANDLE;
		VkQueryPool							vulkanPipelineStatsQueryPool = VK_NULL_HANDLE;
		GpuTimeQueryTree*					timeQueries = nullptr;
	};
	//attention that the count of framepool may not equal to the count of framedata because of the multi thread
	//we can record in multi thread but submit in main thread
	//frame data is the datas used for submit
	//frame pool is stuffs used for record
	//count = numFrame
	struct FrameData
	{
		VkSemaphore		vulkanRenderCompleteSemaphore		= VK_NULL_HANDLE;
		VulkanFence*	vulkanCommandBufferExecutedFence	= nullptr;
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

		inline const auto get_vulkan_physical_device_internal()
		{
			return vulkanPhysicalDevice;
		}

		inline const auto get_vulkan_instance_internal()
		{
			return vulkanInstance;
		}

		inline const auto get_vulkan_allocation_callbacks_internal()
		{
			return vulkanAllocationCallbacks;
		}

		inline auto get_frame_pool_internal(uint32_t index) -> const FramePool& const
		{
			return framePools[index];
		}

		inline auto get_vma_allocator_internal()
		{
			return vmaAllocator;
		}
		
		inline auto get_global_dynamic_buffer_internal()
		{
			return global_dynamic_buffer;
		}

		inline auto get_current_frame_deletion_queue_internal() -> DelayCommandQueue&
		{
			return delayed_deletion_queues[currentFrame];
		}

		inline auto get_global_dynamic_buffer()
		{
			return instance->get_global_dynamic_buffer_internal();
		}

		inline static auto get_current_frame_deletion_queue() -> DelayCommandQueue&
		{
			return instance->get_current_frame_deletion_queue_internal();
		}

		inline static const auto get_vulkan_device()
		{
			return instance->get_vulkan_device_internal();
		}

		inline static const auto get_vulkan_physical_device()
		{
			return instance->get_vulkan_physical_device_internal();
		}

		inline static const auto get_vulkan_instance()
		{
			return instance->get_vulkan_instance_internal();
		}

		inline static const auto get_vulkan_allocation_callbacks()
		{
			return instance->get_vulkan_allocation_callbacks_internal();
		}

		inline static const auto get()
		{
			return instance;
		}

		inline static auto get_frame_pool(uint32_t index) -> const FramePool& const
		{
			return instance->get_frame_pool_internal(index);
		}

		inline static const auto get_vma_allocator()
		{
			return instance->get_vma_allocator_internal();
		}
	
		auto set_resource_name_internal(VkObjectType type, uint64_t handle, const char* name) -> void;

		inline static auto set_resource_name(VkObjectType type, uint64_t handle, const char* name) -> void
		{
			instance->set_resource_name_internal(type,handle,name);
		}
	public:
		/********************************************************** RHI INTERFACE ******************************************************************************************************/

		auto map_buffer(const MapBufferParameters& params) -> void* override;
		auto unmap_buffer(const MapBufferParameters& params) -> void override;
		auto update_buffer_data(const MapBufferParameters& params, void* data) -> void override;
		auto create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer> override;
		auto create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture> override;
		auto create_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView> override;
		auto create_sampler(const SamplerCreation& ci) -> std::shared_ptr<Sampler> override;
		//for non immediately deletion, just call smartpoint.reset
		auto destroy_rhi_resource_Immediately(std::shared_ptr<RHIResource> resource) -> void override;
		auto wait_idle() -> void override;

		/********************************************************** RHI INTERFACE ******************************************************************************************************/

	private:
		StringView stringBuffer{};
	//vk handles
	private:
		//manually in the calling order
		//instance
		auto _create_instance(const Array<const char*>& window_extensions)->HS_Result;
		auto _shutdown_instance() -> HS_Result;
#ifdef VULKAN_DEBUG_REPORT
		//debug msg util
		auto _create_debug_util_messenger_ext()->HS_Result;
		auto _shutdown_debug_util_messenger_ext() -> HS_Result;
#endif
		//device
		auto _select_and_prepare_physical_device()->HS_Result;
		auto _filter_device_selectable_extension()->HS_Result;
		auto _query_supported_props()->HS_Result;
		auto _query_supported_features()->HS_Result;
		auto _create_device()->HS_Result;
		auto _shutdown_device() -> HS_Result;
		//vma
		auto _create_vulkan_memory_allocator()->HS_Result;
		auto _shutdown_vulkan_memory_allocator() -> HS_Result;
		//descriptor pool
		auto _create_descriptor_pool(const GpuDescriptorPoolCreation& dspci)->HS_Result;
		auto _shutdown_descriptor_pool() -> HS_Result;
		//frame data
		auto _create_frame_pool_and_data(uint16_t numThread, uint16_t numQueryTimes)->HS_Result;
		auto _shutdown_frame_pool_and_data() -> HS_Result;
	private:
		BitSetFixed<4> featureSwitchFlags{};
		VkPhysicalDeviceFragmentShadingRatePropertiesKHR fragmentShadingRateProperties{};
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
		//Features
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR   rayTracingPipelineFeatures{};
		VkPhysicalDeviceRayQueryFeaturesKHR             rayQueryFeatures{};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

	private:		
		VkAllocationCallbacks*			vulkanAllocationCallbacks			= nullptr;
		VkInstance                      vulkanInstance						= VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT        vulkanDebugUtilMessenger			= VK_NULL_HANDLE;
		VkPhysicalDevice                vulkanPhysicalDevice				= VK_NULL_HANDLE;
		VkPhysicalDeviceProperties      vulkanPhysicalDeviceProperties{};
		VkDevice                        vulkanDevice						= VK_NULL_HANDLE;
		VkQueue                         vulkanMainQueue						= VK_NULL_HANDLE;
		VkQueue                         vulkanComputeQueue					= VK_NULL_HANDLE;
		VkQueue                         vulkanTransferQueue					= VK_NULL_HANDLE;
		VkQueue							vulkanPresentQueue					= VK_NULL_HANDLE;
		uint32_t						vulkanMainQueueFamily				= UINT32_MAX;
		uint32_t						vulkanComputeQueueFamily			= UINT32_MAX;
		uint32_t						vulkanTransferQueueFamily			= UINT32_MAX;
		VkDescriptorPool                vulkanDescriptorPool				= VK_NULL_HANDLE;
		VkDescriptorPool                vulkanBindlessDescriptorPool		= VK_NULL_HANDLE;
		VmaAllocator                    vmaAllocator						= VK_NULL_HANDLE;
		Array<FramePool>				framePools{};
		Array<FrameData>				frameDatas{};
		Array<VulkanCommandBuffer>		commandBufferQueue{};
		DelayCommandQueue				delayed_deletion_queues[k_max_frames];
		VkSemaphore                     vulkanImageAcquiredSemaphore		= VK_NULL_HANDLE;
		VkSemaphore                     vulkanGraphicsSemaphore				= VK_NULL_HANDLE;
		VkSemaphore                     vulkanBindSemaphore					= VK_NULL_HANDLE;
		VkSemaphore                     vulkanComputeSemaphore				= VK_NULL_HANDLE;
		VulkanFence*                    vulkanComputeFence					= nullptr;
		VulkanFence*					vulkanImmediateFence				= nullptr;
private:
		GPUTimeQueriesManager* gpuTimeQueryManager = nullptr;
		VulkanCommandBufferManager*  commandBufferRing   = nullptr;
private:
		float									gpuTimestampFrequency = 0.f;
		size_t									uboAlignment = 256;
		size_t									ssboAlignemnt = 256;
		uint32_t								subgroupSize = 32;
		uint32_t								maxFramebufferLayers = 1;
		VkExtent2D								minFragmentShadingRateTexelSize{};
		std::shared_ptr<VulkanDynamicBuffer>	global_dynamic_buffer = nullptr;
		uint32_t								currentFrame = UINT32_MAX;
private:
		static VulkanContext* instance;

		




		

};






};
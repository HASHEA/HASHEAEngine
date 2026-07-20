#pragma once
#include "VulkanWrapper.h"
#include "VulkanHelper.hpp"
#include "Base/hcore.h"
#include "Base/hstring.h"
#include "Base/hbit.hpp"
#include "Graphics/GraphicsContext.h"
#include "Base/ds/harray.hpp"
#include "Base/ds/hhash_map.hpp"
#include "Base/hcommandQueue.hpp"
#include "Graphics/Sampler.h"
#include <vector>
#include <memory>
#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
using namespace AshEngine;
namespace RHI
{

	constexpr uint32_t        k_bindless_texture_binding = 10;
	constexpr uint32_t        k_bindless_image_binding = 11;
	constexpr uint32_t        k_max_bindless_resources = 1024;
	constexpr uint32_t        k_max_frames = 3;
	constexpr uint32_t		  k_command_buffer_queue_length = 128;

	class VulkanCommandBuffer;
	class VulkanCommandPool;
	class VulkanFence;
	class VulkanBuffer;
	class VulkanDynamicBuffer;
	class VulkanGpuTimingTelemetry;
	struct VulkanCommandBufferManager;
	struct GPUTimeQuery;
	struct GpuTimeQueryTree;
	struct GpuPipelineStatistics;
	struct GPUTimeQueriesManager;
	class VulkanStagingBufferPool;
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
	//frame data is the datas used for submit and present
	//frame pool is stuffs used for record
	//count = numFrame
	struct FrameData
	{
		VkSemaphore							vulkanRenderBeginSemaphore			= VK_NULL_HANDLE;//wait for this Semaphore to begin render. normally triggered by swapchain acquireimages
		VkSemaphore							vulkanRenderCompleteSemaphore		= VK_NULL_HANDLE;//trigger this seemaphore when complete render, normally waited by swapchain to present
		VulkanFence*						vulkanCommandBufferExecutedFence	= nullptr;
	};
	struct VmaTrackedAllocationInfo
	{
		VkObjectType objectType = VK_OBJECT_TYPE_UNKNOWN;
		uint64_t resourceHandle = 0;
		uint64_t allocationHandle = 0;
		uint64_t size = 0;
		std::string debugName{};
		std::string file{};
		std::string function{};
		uint32_t line = 0;
		std::vector<uint64_t> stackFrames{};
	};
	class VulkanContext : public GraphicsContext
	{
	public:
		auto init(void* config) -> bool override;
		auto shutdown() -> bool override;
		auto destroy() -> void override;
		auto get_render_memory_stats() const -> RenderMemoryStats override;
		auto get_gpu_timing_telemetry() -> IGpuTimingTelemetry* override;
		VulkanContext();
		~VulkanContext();
	public:
		inline const auto get_absolute_frame_count_internal() const 
		{
			return absoluteFrame;
		}
		inline static const auto get_absolute_frame_count()
		{
			return instance->get_absolute_frame_count_internal();
		}

		inline const auto get_device_memory_properties_internal() const
		{
			return vulkanPhysicalDeviceMemoryProperties;
		}

		inline static const auto get_device_memory_properties() 
		{
			return instance->get_device_memory_properties_internal();
		}

		inline const auto get_device_properties_internal() const 
		{
			return vulkanPhysicalDeviceProperties;
		}
		inline static const auto get_device_properties()
		{
			return instance->get_device_properties_internal();
		}

		auto get_device_extension_enabled(DeviceExtensionAndFeaturesFlags extension) -> bool
		{
			return featureSwitchFlags.get_bit(extension);
		}

		inline const auto get_vulkan_device_internal() const
		{
			return vulkanDevice;
		}

		inline const auto get_vulkan_physical_device_internal() const
		{
			return vulkanPhysicalDevice;
		}

		inline const auto get_vulkan_instance_internal() const
		{
			return vulkanInstance;
		}

		inline const auto get_vulkan_allocation_callbacks_internal()
		{
			return vulkanAllocationCallbacks;
		}

		inline auto get_frame_pool_internal(uint32_t index) -> const FramePool& const
		{
			H_ASSERT(framePools.size() > 0);
			return framePools[index % framePools.size()];
		}

		inline auto get_frame_pool_internal() -> const FramePool& const
		{
			H_ASSERT(framePools.size() > 0);
			return framePools[(currentFrame % k_max_frames) * num_thread];
		}

		inline auto get_frame_data_internal() -> const FrameData& const
		{
			return frameDatas[currentFrame];
		}

		inline auto get_frame_data_internal(uint32_t index) -> const FrameData& const
		{
			return frameDatas[index % k_max_frames];
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
			const uint32_t deletion_queue_index = currentFrame == UINT32_MAX ? 0u : currentFrame;
			return delayed_deletion_queues[deletion_queue_index];
		}

		inline auto get_current_frame_internal()
		{
			return currentFrame;
		}

		inline static auto get_current_frame()
		{
			return instance->get_current_frame_internal();
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

		inline static auto get_frame_pool() -> const FramePool& const
		{
			return instance->get_frame_pool_internal();
		}

		inline static auto get_frame_data() -> const FrameData& const
		{
			return instance->get_frame_data_internal();
		}

		inline static auto get_frame_data(uint32_t index) -> const FrameData& const
		{
			return instance->get_frame_data_internal(index);
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

		inline static auto get_present_queue() -> VkQueue
		{
			return instance->vulkanPresentQueue;
		}

		inline static auto get_graphics_queue() -> VkQueue
		{
			return instance->vulkanMainQueue;
		}

		inline static auto get_fragment_shading_rate_texel_size()
		{
			return instance->minFragmentShadingRateTexelSize;
		}

		inline static auto get_queue_family_index(AshQueueType::Enum queueType) -> uint32_t
		{
			uint32_t index = UINT32_MAX;
			switch (queueType)
			{
			case RHI::AshQueueType::Graphics:
				index = instance->vulkanMainQueueFamily;
				break;
			case RHI::AshQueueType::Compute:
				index = instance->vulkanComputeQueueFamily;
				break;
			case RHI::AshQueueType::CopyTransfer:
				index = instance->vulkanTransferQueueFamily;
				break;
			case RHI::AshQueueType::Ignored:
				index = VK_QUEUE_FAMILY_IGNORED;
				break;
			default:
				break;
			}
			return index;
		}
		auto create_sampler(const AshSamplerState& ss) -> std::shared_ptr<Sampler>;

		inline static auto get_pipeline_cache() -> VkPipelineCache
		{
			return instance->vulkanPipelineCache;
		}
		auto vma_create_buffer(VkDeviceSize uBufferSize, VkBufferUsageFlags eBufferUsage, VmaMemoryUsage eMemUsage, VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation, void** ppData = nullptr, const char* debugName = nullptr, const char* file = nullptr, uint32_t line = 0, const char* function = nullptr) -> bool;
		auto vma_destroy_buffer(VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation, const char* file = nullptr, uint32_t line = 0, const char* function = nullptr) -> bool;
		auto vma_destroy_buffer_v(VkBuffer pVkBuffer, VmaAllocation pVMAllocation, const char* file = nullptr, uint32_t line = 0, const char* function = nullptr) -> bool;
		auto vma_create_image(const VkImageCreateInfo& sImgCreateInfo, VmaMemoryUsage eMemUsage, VkImage& pVkImage, VmaAllocation& pVMAllocation, const char* debugName = nullptr, const char* file = nullptr, uint32_t line = 0, const char* function = nullptr) -> bool;
		auto vma_destroy_image(VkImage pVkImage, VmaAllocation pVMAllocation, const char* file = nullptr, uint32_t line = 0, const char* function = nullptr) -> bool;
		auto vma_map_memory(VmaAllocation pVMAllocation, void** ppData) const -> bool;
		auto vma_unmap_memory(VmaAllocation pVMAllocation) const -> bool;
		auto vma_flush_allocation(VmaAllocation pVMAllocation, VkDeviceSize uOffset = 0, VkDeviceSize uSize = VK_WHOLE_SIZE) const -> bool;
		inline auto get_vulkan_staging_buffer_pool() const
		{
			return vulkanStagingBufferPool;
		}
		auto queue_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, void* data) -> bool;
		auto queue_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool;
		
public:
		/********************************************************** RHI INTERFACE ******************************************************************************************************/
		auto create_buffer_view(const BufferViewCreation& ci, std::shared_ptr<Buffer> parentBuffer) -> std::shared_ptr<BufferView> override;
		auto create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer> override;
		auto create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture> override;
		auto create_texture_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView> override;
		auto create_render_pass(const RenderPassCreation& ci) -> std::shared_ptr<RenderPass> override;
		auto create_framebuffer(const FramebufferCreation& ci) -> std::shared_ptr<Framebuffer> override;
		auto create_graphics_render_program(const GraphicProgramCreateDesc& desc) -> std::unique_ptr<IGraphicsRenderProgram> override;
		auto create_compute_render_program(const ComputeProgramCreateDesc& desc) -> std::unique_ptr<IComputeRenderProgram> override;
		auto create_shader(const ShaderCreation& ci) -> std::shared_ptr<Shader> override;
		auto create_sampler(const SamplerCreation& ci) -> std::shared_ptr<Sampler> override;
		auto get_sampler(const AshSamplerState& ss) -> std::shared_ptr<Sampler> override;
		auto wait_idle() -> void override;
		auto wait_for_frame_completion(uint64_t timeout_nanoseconds) -> bool override;
		auto begin_frame() -> void override;
		auto end_frame(bool has_acquired_swapchain_image = true) -> void override;
		auto get_command_buffer(uint32_t threadIndx) -> CommandBuffer* override;
		auto get_secondary_command_buffer(uint32_t threadIndx) -> CommandBuffer* override;
		auto submit(const SubmitInfo& info) -> void override;
		auto submit_immediately(const SubmitInfo& info) -> void override;
		/********************************************************** RHI INTERFACE ******************************************************************************************************/

	private:
		StringView stringBuffer{};
		VulkanValidationConfig validationConfig{};
		bool debugUtilsEnabled = false;
	//vk handles
	private:
		//manually in the calling order
		//instance
		auto _create_instance(const Array<const char*>& window_extensions)->bool;
		auto _shutdown_instance() -> bool;
#ifdef VULKAN_DEBUG_REPORT
		//debug msg util
		auto _create_debug_util_messenger_ext()->bool;
		auto _shutdown_debug_util_messenger_ext() -> bool;
#endif
		//device
		auto _select_and_prepare_physical_device()->bool;
		auto _filter_device_selectable_extension()->bool;
		auto _query_device_memory_props() -> bool;
		auto _query_supported_props()->bool;
		auto _query_supported_features()->bool;
		auto _create_device()->bool;
		auto _shutdown_device() -> bool;
		//vma
		auto _create_vulkan_memory_allocator()->bool;
		auto _shutdown_vulkan_memory_allocator() -> bool;
		//descriptor pool
		auto _create_descriptor_pool(const GpuDescriptorPoolCreation& dspci)->bool;
		auto _shutdown_descriptor_pool() -> bool;
		//frame data
		auto _create_frame_pool_and_data(uint16_t numThread, uint16_t numQueryTimes)->bool;
		auto _shutdown_frame_pool_and_data() -> bool;
		//pipeline / or other cache
		auto _load_cache() -> bool;
		auto _unload_cache() -> bool;
		//staging buffer poo

		auto _create_staging_buffer_pool() -> bool;
		auto _shutdown_staging_buffer_pool() -> bool;
		auto _enqueue_pending_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool;
		auto _enqueue_pending_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool;
		auto _ensure_upload_command_buffer_recording() -> bool;
		auto _record_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool;
		auto _record_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool;
		auto _flush_pending_buffer_uploads() -> bool;
		auto _flush_pending_texture_uploads() -> bool;
		auto _finalize_upload_command_buffer() -> bool;
		auto _track_vma_allocation(VmaAllocation allocation, VkObjectType objectType, uint64_t resourceHandle, uint64_t size, const char* debugName, const char* file, uint32_t line, const char* function) -> void;
		auto _untrack_vma_allocation(VmaAllocation allocation, VkObjectType objectType, uint64_t resourceHandle, const char* file, uint32_t line, const char* function) -> void;
		auto _dump_vma_leaks() const -> void;
		auto _capture_vma_allocation_stack(VmaTrackedAllocationInfo& info) const -> void;
		auto _shutdown_shader_pool() -> bool;
private:
		struct PendingBufferUpload
		{
			std::shared_ptr<Buffer> buffer = nullptr;
			uint32_t offset = 0;
			std::vector<uint8_t> data{};
		};

		struct PendingTextureUpload
		{
			std::shared_ptr<Texture> texture = nullptr;
			std::vector<uint8_t> data{};
		};

		BitSetFixed<16> featureSwitchFlags{};
		VkPhysicalDeviceFragmentShadingRatePropertiesKHR fragmentShadingRateProperties{};
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
		//Features
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR   rayTracingPipelineFeatures{};
		VkPhysicalDeviceRayQueryFeaturesKHR             rayQueryFeatures{};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

	private:		
		VkAllocationCallbacks*									vulkanAllocationCallbacks				= nullptr;
		VkInstance												vulkanInstance							= VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT								vulkanDebugUtilMessenger				= VK_NULL_HANDLE;
		VkPhysicalDevice										vulkanPhysicalDevice					= VK_NULL_HANDLE;
		VkPhysicalDeviceProperties								vulkanPhysicalDeviceProperties{};
		VkPhysicalDeviceMemoryProperties						vulkanPhysicalDeviceMemoryProperties{};
		VkDevice												vulkanDevice							= VK_NULL_HANDLE;
		VkQueue													vulkanMainQueue							= VK_NULL_HANDLE;
		VkQueue													vulkanComputeQueue						= VK_NULL_HANDLE;
		VkQueue													vulkanTransferQueue						= VK_NULL_HANDLE;
		VkQueue													vulkanPresentQueue						= VK_NULL_HANDLE;
		uint32_t												vulkanMainQueueFamily					= UINT32_MAX;
		uint32_t												vulkanComputeQueueFamily				= UINT32_MAX;
		uint32_t												vulkanTransferQueueFamily				= UINT32_MAX;
		VkDescriptorPool										vulkanDescriptorPool					= VK_NULL_HANDLE;
		VkDescriptorPool										vulkanBindlessDescriptorPool			= VK_NULL_HANDLE;
		VmaAllocator											vmaAllocator							= VK_NULL_HANDLE;
		Array<FramePool>										framePools{};
		Array<FrameData>										frameDatas{};
		Array<VulkanCommandBuffer*>								commandBufferQueue{};
		DelayCommandQueue										delayed_deletion_queues[k_max_frames];
		VkSemaphore												vulkanGraphicsSemaphore					= VK_NULL_HANDLE;
		VkSemaphore												vulkanBindSemaphore						= VK_NULL_HANDLE;
		VkSemaphore												vulkanComputeSemaphore					= VK_NULL_HANDLE;
		VkSemaphore												vulkanPresentCompleteSemaphore			= VK_NULL_HANDLE;
		VulkanFence*											vulkanComputeFence						= nullptr;
		VulkanFence*											vulkanImmediateFence					= nullptr; //stuck here and wait
		std::array<std::shared_ptr<Sampler>, static_cast<size_t>(ASH_SAMPLER_STATE_MAX_ENUM)> samplerCache{};
		std::unordered_map<uint64_t, std::weak_ptr<Sampler>>	samplerDedupPool{};
		mutable std::mutex										samplerDedupMutex{};
		VkPipelineCache											vulkanPipelineCache						= VK_NULL_HANDLE;
		VulkanStagingBufferPool*								vulkanStagingBufferPool					 = nullptr;
		std::unordered_map<uint64_t, std::shared_ptr<Shader>>	vulkanShaderPool;
		std::vector<PendingBufferUpload>						pendingBufferUploads{};
		std::vector<PendingTextureUpload>						pendingTextureUploads{};
		mutable std::mutex										pendingUploadMutex{};
private:
		GPUTimeQueriesManager* gpuTimeQueryManager = nullptr;
		VulkanCommandBufferManager*  commandBufferRing   = nullptr;
		VulkanCommandBuffer*		 currentUploadCommandBuffer = nullptr;
		bool						 uploadCommandsPending = false;
		bool						 uploadCommandQueued = false;
		bool						 frameActive = false;
private:
		float									gpuTimestampFrequency = 0.f;
		size_t									uboAlignment = 256;
		size_t									ssboAlignemnt = 256;
		uint32_t								subgroupSize = 32;
		uint32_t								maxFramebufferLayers = 1;
		VkExtent2D								minFragmentShadingRateTexelSize{};
		std::shared_ptr<VulkanDynamicBuffer>	global_dynamic_buffer = nullptr;
		std::unique_ptr<VulkanGpuTimingTelemetry>	gpuTimingTelemetry{};
		uint32_t								currentFrame = UINT32_MAX;
		uint32_t								previousFrame = UINT32_MAX;
		uint64_t								absoluteFrame = UINT64_MAX;
		uint16_t								num_thread = 0;
		uint32_t								local_gpu_memory_gb = 0;
		mutable std::mutex						vmaTrackedAllocationsMutex{};
		std::unordered_map<uint64_t, VmaTrackedAllocationInfo> vmaTrackedAllocations{};
		mutable RenderMemoryStats				renderMemoryStats{};
private:
		static VulkanContext* instance;
		friend class VulkanSwapchain;




};






};

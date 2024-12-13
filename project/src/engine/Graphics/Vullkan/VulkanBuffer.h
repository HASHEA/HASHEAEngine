#pragma once
#include "VulkanWrapper.h"
#include "Graphics/Buffer.h"
namespace RHI
{
	class VulkanBuffer : public Buffer
	{

	public:
		VulkanBuffer(const BufferCreation& ci);
		virtual ~VulkanBuffer();
		static auto create(const BufferCreation& ci) -> std::shared_ptr<VulkanBuffer>;
	public:
		//interfaces from Buffer 
		auto get_size() -> uint32_t override;
		auto get_name() -> const char* override;
		auto get_global_offset() -> uint32_t override;
		auto is_ready() -> bool override;
		auto get_mapped_data() -> uint8_t* override;
		auto is_dynamic() -> bool override;
	public:
		//interfaces for vk
		inline auto get_vk_device_memory()
		{
			return vkDeviceMemory;
		}
		inline auto get_vma_allocation()
		{
			return vmaAllocation;
		}
		inline auto get_vk_buffer_handle()
		{
			return vkBuffer;
		}
	private:
		VkBuffer vkBuffer = VK_NULL_HANDLE;
		VmaAllocation vmaAllocation = nullptr;
		VkDeviceMemory vkDeviceMemory = VK_NULL_HANDLE;
		VkDeviceSize   vkDeviceSize = 0;
		uint32_t usageFlags = 0;
		AshResourceUsageType::Enum         usage = AshResourceUsageType::Immutable;
		uint32_t size = 0;
		uint32_t globalOffset = UINT32_MAX;
		bool ready = true;
		uint8_t* mappedData = nullptr;
		const char* name = nullptr;
		bool dynamic = false;
		friend class VulkanDynamicBuffer;
	};

	class VulkanDynamicBuffer final : public VulkanBuffer
	{
	public:
		VulkanDynamicBuffer(const BufferCreation& ci);
		~VulkanDynamicBuffer();

	public:
		auto dynamic_allocate(uint32_t size, size_t alignment) -> void*;
		auto dynamic_allocate_buffer(std::shared_ptr<VulkanBuffer> buffer, uint32_t size, size_t alignment) -> void*;
	private:
		uint32_t dynamic_allocated_size = 0;
		uint8_t* dynamic_mapped_memory = nullptr;
	};
}
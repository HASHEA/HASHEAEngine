#pragma once
#include "VulkanWrapper.h"
#include "Graphics/Buffer.h"
#include "VulkanResourceTracker.h"
namespace RHI
{

	
	class VulkanBufferView : public BufferView	
	{
	public:
		VulkanBufferView(const BufferViewCreation& ci, std::shared_ptr<Buffer> parent);
		~VulkanBufferView();
	public:

		// 通过 BufferView 继承
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
		auto get_parent_buffer() -> std::shared_ptr<Buffer> override;
		auto get_view_type() -> AshResourceViewType override;
		auto get_view_desc() -> const BufferViewCreation & override;
		auto get_view_format() -> AshFormat override;
	private:
		VkBufferViewCreateInfo m_createInfo{};
		VkBufferView vkBufferView = VK_NULL_HANDLE;
		std::weak_ptr<Buffer> parentBuffer;
		BufferViewCreation m_ViewCreation{};

	};
	class VulkanResourceTracker;
	class VulkanBuffer : public Buffer
	{

	public:
		VulkanBuffer() = default;
		virtual ~VulkanBuffer();
		auto create(const BufferCreation& ci) -> bool;
		auto destroy() -> void;
	public:
		//interfaces from Buffer 
		auto get_size() -> uint32_t override;
		auto get_name() -> const char* override;
		auto get_global_offset() -> uint32_t override;
		auto is_ready() -> bool override;
		auto get_mapped_data() -> uint8_t* override;
		auto is_dynamic() -> bool override;
		inline auto get_native_handle() -> void* override
		{
			return m_pVkBuffer;
		}
		// 通过 Buffer 继承
		auto get_buffer_device_address() -> uint64_t override;
		auto get_buffer_creation_info() const -> const BufferCreation & override;
		auto get_default_cbv() -> std::shared_ptr<BufferView> override;
		auto get_default_srv() -> std::shared_ptr<BufferView> override;
		auto get_default_uav() -> std::shared_ptr<BufferView> override;
		auto update(uint32_t offset, uint32_t size, void* pData) -> bool override;
	public:
		//interfaces for vk
		inline auto get_vk_device_memory_size() const
		{
			return m_vkDeviceSize;
		}
		inline auto get_vma_allocation() const
		{
			return m_pVMAAllocation;
		}
		inline auto get_vk_buffer_handle() const
		{
			return m_pVkBuffer;
		}

		inline virtual auto get_descriptor_buffer_info() const -> const VkDescriptorBufferInfo&
		{
			return m_sDescriptorBufferInfo;
		}
		inline auto get_resource_tracker() -> VulkanResourceTracker&
		{
			return m_resourceTracker;
		}
		auto flush_mapped_range() -> bool;

	private:
		VkBuffer m_pVkBuffer = VK_NULL_HANDLE;
		VmaAllocation m_pVMAAllocation = nullptr;
		VkDeviceSize   m_vkDeviceSize = 0;
		VkDescriptorBufferInfo m_sDescriptorBufferInfo{};
		BufferCreation m_sCreationInfo;
		VulkanResourceTracker m_resourceTracker{AshResourceState::Unknown};
		bool m_bReady = true;
		uint8_t* m_pMappedData = nullptr;
		bool m_bCoherent = false;

		const char* m_pName = nullptr;
		bool dynamic = false;
		friend class VulkanDynamicBuffer;

		
	};

	class VulkanDynamicBuffer final : public VulkanBuffer
	{
	public:
		VulkanDynamicBuffer() = default;
		~VulkanDynamicBuffer();

	public:
		auto dynamic_allocate(uint32_t size, size_t alignment) -> void*;
		auto dynamic_allocate_buffer(std::shared_ptr<VulkanBuffer> buffer, uint32_t size, size_t alignment) -> void*;
		auto update(uint32_t offset, uint32_t size, void* pData) -> bool override;
	private:
		uint32_t dynamic_allocated_size = 0;
		uint8_t* dynamic_mapped_memory = nullptr;
	};
}
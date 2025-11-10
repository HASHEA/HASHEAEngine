#pragma once
#include "VulkanHelper.hpp"
#include <set>
namespace RHI
{	
	class VulkanFence;
	class VulkanCommandBuffer;
	struct VK_SUBRESOURCE_DATA
	{
		void* DataPtr;
		uint32_t DataRowPitch;
		uint32_t DataSlicePitch;
	};
	
	class VulkanStagingBuffer
	{
		friend class VulkanStagingBufferPool;
	public:
		VulkanStagingBuffer(uint32_t uByteWidth, const VK_SUBRESOURCE_DATA* pData, bool bReadOperation);
		~VulkanStagingBuffer();
	public:
		auto get_vkbuffer_handle() const -> VkBuffer;
		auto get_device_memory_size() const -> VkDeviceSize;
		auto map(uint32_t uOffset, uint32_t uSize) -> bool;
		auto unmap() -> bool;
		auto get_mapped_range(VkDeviceSize& uOffset, VkDeviceSize& uSize) -> bool;
		inline auto get_mapped_memory() const
		{
			return m_pMappedPtr;
		}
		inline auto is_mapped() const -> bool
		{
			return m_bMapped;
		}
	private:
		struct PACK_BUFFER_ITEM
		{
			VkBuffer vkBuffer = VK_NULL_HANDLE;
			VmaAllocation m_pVmaAllocation = nullptr;
			VkDeviceSize u64DeviceSize = 0;
			bool m_bReadOption = false;
			//use for delay release
			uint32_t m_uTimeStamp = 0;
		};
	private:
		VkBuffer m_vkBuffer = VK_NULL_HANDLE;
		VmaAllocation m_pVmaAllocation = nullptr;
		VkDeviceSize m_u64DeviceMemorySize = 0;
		void* m_pMappedPtr = nullptr;
		bool m_bMapped = false;
		bool m_bPersistentMapping = false;
		uint64_t m_u64SignalFenceCounter = UINT64_MAX;
		uint32_t m_uMappedDstOffset = 0;
		uint32_t m_uMappedDstSize = 0;
		bool m_bReadOption = false;
	};
	class VulkanStagingBufferPool
	{
		friend class VulkanStagingBuffer;
	public:
		VulkanStagingBufferPool() = default;
		~VulkanStagingBufferPool();
	public:
		auto init() -> bool;
		auto uninit() -> bool;
	public:
		auto frame_move() -> bool;
	private:
		auto _process_free_buffers() -> bool;
		auto _destroy_buffer(VkBuffer vkBuffer, VmaAllocation pVMAAllocation) -> void;
	private:	
		auto alloc_buffer(uint32_t uByteWidth,  void** ppMappedData, bool bReadOperation) -> VulkanStagingBuffer::PACK_BUFFER_ITEM;
		auto free_buffer(VulkanStagingBuffer::PACK_BUFFER_ITEM& freeItem) -> void;
		struct SizeComparator
		{
			bool operator()(VulkanStagingBuffer::PACK_BUFFER_ITEM a, VulkanStagingBuffer::PACK_BUFFER_ITEM b) const
			{
				H_ASSERTLOG(a.u64DeviceSize, "stored buffer size can not be 0 !");
				H_ASSERTLOG(b.u64DeviceSize, "stored buffer size can not be 0 !");
				return a.u64DeviceSize < b.u64DeviceSize;
			}
		};
		struct FreeBufferGroup
		{
			std::multiset<VulkanStagingBuffer::PACK_BUFFER_ITEM, SizeComparator> buffers;
		};
		FreeBufferGroup readOptionGroup{};
		FreeBufferGroup normalGroup{};
	};
};

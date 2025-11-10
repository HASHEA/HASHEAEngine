#include "VulkanStagingBuffer.h"
#include "VulkanContext.h"
#include "VulkanCommandBuffer.h"
#include <mutex>
constexpr uint32_t STAGING_BUFFER_POOL_DELAY_RELEASE_FRAME_COUNT = 16;
namespace RHI
{
	VulkanStagingBuffer::VulkanStagingBuffer(uint32_t uByteWidth, const VK_SUBRESOURCE_DATA* pData, bool bReadOperation)
	{
		auto pStagingBufferPool = VulkanContext::get()->get_vulkan_staging_buffer_pool();
		H_ASSERT(pStagingBufferPool);
		PACK_BUFFER_ITEM allocated_item = pStagingBufferPool->alloc_buffer(uByteWidth, &m_pMappedPtr, bReadOperation);
		m_vkBuffer = allocated_item.vkBuffer;
		m_pVmaAllocation = allocated_item.m_pVmaAllocation;
		m_u64DeviceMemorySize = allocated_item.u64DeviceSize;
		m_bReadOption = allocated_item.m_bReadOption;
		//fill data
		if (pData && pData->DataPtr)
		{
			bool bRetCode = this->map(0, this->get_device_memory_size());
			H_ASSERT(bRetCode);
			memory_copy(this->m_pMappedPtr, pData->DataPtr, uByteWidth);
			bRetCode = this->unmap();
			H_ASSERT(bRetCode);
		}
	}
	VulkanStagingBuffer::~VulkanStagingBuffer()
	{
		if (m_vkBuffer != VK_NULL_HANDLE)
		{
			auto handle = m_vkBuffer;
			auto alloc = m_pVmaAllocation;
			auto size = m_u64DeviceMemorySize;
			auto bReadOption = m_bReadOption;
			VulkanContext::get_current_frame_deletion_queue().emplace([handle, alloc, size, bReadOption]() {
				auto pStagingBufferPool = VulkanContext::get()->get_vulkan_staging_buffer_pool();
				if (pStagingBufferPool)
				{
					PACK_BUFFER_ITEM allocated_item{ handle ,alloc ,size ,bReadOption ,0};
					pStagingBufferPool->free_buffer(allocated_item);
				}
				VulkanContext::get()->vma_destroy_buffer_v(handle, alloc);
				});
			m_vkBuffer = VK_NULL_HANDLE;
			m_pVmaAllocation = nullptr;
			m_u64DeviceMemorySize = 0;
			m_bReadOption = false;
		}
	}
	auto VulkanStagingBuffer::get_vkbuffer_handle() const -> VkBuffer
	{
		return m_vkBuffer;
	}
	auto VulkanStagingBuffer::get_device_memory_size() const -> VkDeviceSize
	{
		return m_u64DeviceMemorySize;
	}
	auto VulkanStagingBuffer::map(uint32_t uOffset, uint32_t uSize) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(m_bMapped);
		ASH_PROCESS_SUCCESS(m_bPersistentMapping);
		bool bRetCode = VulkanContext::get()->vma_map_memory(m_pVmaAllocation, (void**)&m_pMappedPtr);
		ASH_LOG_PROCESS_ERROR(bRetCode);
		ASH_SAFE_EXECUTE_END(bResult);
		if (bResult)
		{
			m_bMapped = true;
			m_uMappedDstOffset = uOffset;
			m_uMappedDstSize = uSize;
		}

		return bResult;
	}
	auto VulkanStagingBuffer::unmap() -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(m_bMapped);
		ASH_PROCESS_SUCCESS(m_bPersistentMapping);
		ASH_PROCESS_ERROR_EXIT(m_pVmaAllocation);
		vmaUnmapMemory(VulkanContext::get_vma_allocator(), m_pVmaAllocation);
		ASH_SAFE_EXECUTE_END(bResult);
		if (bResult)
		{
			m_bMapped = false;
			m_uMappedDstOffset = 0;
			m_uMappedDstSize = 0;
		}
		return bResult;
	}
	auto VulkanStagingBuffer::get_mapped_range(VkDeviceSize& uOffset, VkDeviceSize& uSize) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(m_bMapped);
		uOffset = m_uMappedDstOffset;
		uSize = m_uMappedDstSize;
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	VulkanStagingBufferPool::~VulkanStagingBufferPool()
	{
	}
	auto VulkanStagingBufferPool::init() -> bool
	{
		return true;
	}
	auto VulkanStagingBufferPool::uninit() -> bool
	{
		return true;
	}
	auto VulkanStagingBufferPool::alloc_buffer(uint32_t uByteWidth,  void** ppMappedData, bool bReadOperation) -> VulkanStagingBuffer::PACK_BUFFER_ITEM
	{
		VulkanStagingBuffer::PACK_BUFFER_ITEM retBufferItem{};
		VkBufferUsageFlags vkUsageFlags = bReadOperation ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		VkBufferCreateInfo bufCreateInfo{};
		bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufCreateInfo.usage = vkUsageFlags;
		bufCreateInfo.size = uByteWidth;
		//find matched free buffer
		{
			auto& dstGroup = bReadOperation ? readOptionGroup : normalGroup;
			VulkanStagingBuffer::PACK_BUFFER_ITEM tempFinder{};
			tempFinder.u64DeviceSize = uByteWidth;
			const auto iter = dstGroup.buffers.lower_bound(tempFinder);
			if (iter != dstGroup.buffers.end())
			{
				retBufferItem = *iter;
				dstGroup.buffers.erase(iter);
			}
		}
		//no match free buffer, create new
		if (!retBufferItem.vkBuffer)
		{
			bool bRetCode = VulkanContext::get()->vma_create_buffer(uByteWidth, vkUsageFlags, VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_ONLY, retBufferItem.vkBuffer, retBufferItem.m_pVmaAllocation, ppMappedData);
			H_ASSERT(bRetCode);
			retBufferItem.m_bReadOption = bReadOperation;
		}
		H_ASSERT(retBufferItem.m_bReadOption == bReadOperation);
		return retBufferItem;
	}
	auto VulkanStagingBufferPool::free_buffer(VulkanStagingBuffer::PACK_BUFFER_ITEM& freeItem) -> void
	{
		H_ASSERT(freeItem.vkBuffer);
		H_ASSERT(freeItem.m_pVmaAllocation);
		H_ASSERT(freeItem.u64DeviceSize);
		freeItem.m_uTimeStamp = VulkanContext::get_absolute_frame_count();
		if (freeItem.m_bReadOption)
		{
			readOptionGroup.buffers.emplace(freeItem);
		}
		else
		{
			normalGroup.buffers.emplace(freeItem);
		}
	}

	auto VulkanStagingBufferPool::frame_move() -> bool
	{
		_process_free_buffers();
		return true;
	}
	auto VulkanStagingBufferPool::_process_free_buffers() -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		auto currentFrameCount = VulkanContext::get_absolute_frame_count();
		auto ToBeFreedFrame = currentFrameCount - STAGING_BUFFER_POOL_DELAY_RELEASE_FRAME_COUNT;
		ToBeFreedFrame = ToBeFreedFrame > 0 ? ToBeFreedFrame : 0;
		for (auto& iter : readOptionGroup.buffers)
		{
			if (iter.m_uTimeStamp <= ToBeFreedFrame)
			{
				_destroy_buffer(iter.vkBuffer, iter.m_pVmaAllocation);
			}
		}
		for (auto& iter : normalGroup.buffers)
		{
			if (iter.m_uTimeStamp <= ToBeFreedFrame)
			{
				_destroy_buffer(iter.vkBuffer, iter.m_pVmaAllocation);
			}
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanStagingBufferPool::_destroy_buffer(VkBuffer vkBuffer, VmaAllocation pVMAAllocation) -> void
	{
		VulkanContext::get()->vma_destroy_buffer_v(vkBuffer, pVMAAllocation);
	}
};
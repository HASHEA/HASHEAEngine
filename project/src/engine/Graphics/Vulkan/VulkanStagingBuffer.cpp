#include "VulkanStagingBuffer.h"
#include "VulkanContext.h"
#include "VulkanCommandBuffer.h"
#include "Base/hcore.h"
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "Base/hmemory.h"
#include <mutex>

namespace RHI
{
	namespace
	{
		constexpr uint32_t k_staging_page_size           = 4u * 1024u * 1024u;
		constexpr uint32_t k_staging_dedicated_threshold = 2u * 1024u * 1024u;
		constexpr uint32_t k_staging_page_idle_frames    = 16u;
		constexpr uint32_t k_staging_evict_tolerance     = 1u;
	}

	VulkanStagingBuffer::VulkanStagingBuffer(uint32_t uByteWidth, const VK_SUBRESOURCE_DATA* pData, bool bReadOperation)
	{
		auto* pPool = VulkanContext::get()->get_vulkan_staging_buffer_pool();
		H_ASSERT(pPool);
		auto slice = pPool->alloc_slice(uByteWidth, bReadOperation);
		H_ASSERT(slice.page);

		m_pageBuffer    = slice.page->buffer;
		m_pageId        = slice.page->id;
		m_pageOffset    = slice.pageOffset;
		m_size          = uByteWidth;
		m_pageVmaAlloc  = slice.page->alloc;
		m_sliceMapped   = slice.page->mapped + slice.pageOffset;
		m_bReadOption   = bReadOperation;

		// Convenience: ctor immediate-fill (only used if caller passes pData; current callers do not).
		if (pData && pData->DataPtr)
		{
			bool bRetCode = this->map(0, uByteWidth);
			H_ASSERT(bRetCode);
			memory_copy(m_sliceMapped, pData->DataPtr, uByteWidth);
			bRetCode = this->unmap();
			H_ASSERT(bRetCode);
		}
	}

	VulkanStagingBuffer::~VulkanStagingBuffer()
	{
		if (m_pageId != UINT32_MAX)
		{
			const uint32_t pageIdToRelease = m_pageId;
			VulkanContext::get_current_frame_deletion_queue().emplace([pageIdToRelease]() {
				auto* pPool = VulkanContext::get()->get_vulkan_staging_buffer_pool();
				if (pPool)
				{
					pPool->release_slice(pageIdToRelease);
				}
			});
			m_pageBuffer = VK_NULL_HANDLE;
			m_pageId = UINT32_MAX;
			m_pageOffset = 0;
			m_size = 0;
			m_pageVmaAlloc = nullptr;
			m_sliceMapped = nullptr;
		}
	}

	auto VulkanStagingBuffer::map(uint32_t uOffset, uint32_t uSize) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(!m_bMapped);
		ASH_PROCESS_ERROR_EXIT(m_sliceMapped);
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
		ASH_PROCESS_ERROR_EXIT(m_pageVmaAlloc);
		// Flush range is local-to-slice (uMappedDstOffset/Size) plus the slice's offset within the page.
		const VkDeviceSize flushOffset = m_pageOffset + m_uMappedDstOffset;
		ASH_LOG_PROCESS_ERROR(VulkanContext::get()->vma_flush_allocation(m_pageVmaAlloc, flushOffset, m_uMappedDstSize));
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

	// ---------------- VulkanStagingBufferPool ----------------

	VulkanStagingBufferPool::~VulkanStagingBufferPool()
	{
	}

	auto VulkanStagingBufferPool::init() -> bool
	{
		m_inUseRing.fill(0u);
		m_ringIndex = 0;
		return true;
	}

	auto VulkanStagingBufferPool::uninit() -> bool
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto destroy_kind = [this](PerKind& pk)
		{
			if (pk.active) { _destroy_page(pk.active); pk.active = nullptr; }
			for (auto* p : pk.in_use) _destroy_page(p);
			pk.in_use.clear();
			for (auto* p : pk.free) _destroy_page(p);
			pk.free.clear();
			for (auto* p : pk.dedicated_pending) _destroy_page(p);
			pk.dedicated_pending.clear();
		};
		destroy_kind(m_upload);
		destroy_kind(m_readback);
		m_pages.clear();
		m_freePageIds.clear();
		return true;
	}

	auto VulkanStagingBufferPool::_register_page(Page* page) -> void
	{
		if (!m_freePageIds.empty())
		{
			page->id = m_freePageIds.back();
			m_freePageIds.pop_back();
			m_pages[page->id] = page;
		}
		else
		{
			page->id = static_cast<uint32_t>(m_pages.size());
			m_pages.push_back(page);
		}
	}

	auto VulkanStagingBufferPool::_unregister_page(Page* page) -> void
	{
		if (page->id < m_pages.size() && m_pages[page->id] == page)
		{
			m_pages[page->id] = nullptr;
			m_freePageIds.push_back(page->id);
		}
		page->id = UINT32_MAX;
	}

	auto VulkanStagingBufferPool::_create_page(uint32_t size, bool bReadback, bool dedicated) -> Page*
	{
		VkBufferUsageFlags vkUsageFlags = bReadback ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		VkBuffer vkBuffer = VK_NULL_HANDLE;
		VmaAllocation vmaAlloc = nullptr;
		void* mapped = nullptr;
		bool bRetCode = ASH_VMA_CREATE_BUFFER(
			VulkanContext::get(),
			size,
			vkUsageFlags,
			VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_ONLY,
			vkBuffer, vmaAlloc, &mapped,
			bReadback ? "VulkanStagingPage(Readback)" : "VulkanStagingPage(Upload)");
		H_ASSERT(bRetCode);
		H_ASSERT(vkBuffer);
		H_ASSERT(mapped);

		Page* page = Ash_New<Page>();
		page->buffer    = vkBuffer;
		page->alloc     = vmaAlloc;
		page->size      = size;
		page->offset    = 0;
		page->mapped    = static_cast<uint8_t*>(mapped);
		page->bReadback = bReadback;
		page->dedicated = dedicated;
		_register_page(page);

		HLogInfo("VulkanStagingBufferPool: created page id={} size={} kind={} dedicated={}",
			page->id, size, bReadback ? 1 : 0, dedicated ? 1 : 0);
		return page;
	}

	auto VulkanStagingBufferPool::_destroy_page(Page* page) -> void
	{
		if (!page) return;
		HLogInfo("VulkanStagingBufferPool: destroyed page id={} size={} kind={} dedicated={}",
			page->id, static_cast<uint64_t>(page->size), page->bReadback ? 1 : 0, page->dedicated ? 1 : 0);
		ASH_VMA_DESTROY_BUFFER_V(VulkanContext::get(), page->buffer, page->alloc);
		_unregister_page(page);
		Ash_Delete(nullptr, page);
	}

	auto VulkanStagingBufferPool::_activate_or_grow(PerKind& pk, bool bReadback, uint32_t neededBytes) -> Page*
	{
		for (auto it = pk.free.begin(); it != pk.free.end(); ++it)
		{
			if ((*it)->size >= neededBytes)
			{
				Page* page = *it;
				pk.free.erase(it);
				page->offset = 0;
				return page;
			}
		}
		const uint32_t pageSize = neededBytes > k_staging_page_size ? neededBytes : k_staging_page_size;
		return _create_page(pageSize, bReadback, false);
	}

	auto VulkanStagingBufferPool::alloc_slice(uint32_t size, bool bReadback) -> SliceAlloc
	{
		H_ASSERT(size > 0u);
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint64_t currentFrame = VulkanContext::get_absolute_frame_count();
		PerKind& pk = _kind(bReadback);

		if (size > k_staging_dedicated_threshold)
		{
			Page* page = _create_page(size, bReadback, true);
			H_ASSERT(page);
			page->offset = size;
			page->last_use_frame = currentFrame;
			page->outstanding_slices = 1u;
			pk.dedicated_pending.push_back(page);
			return SliceAlloc{ page, 0 };
		}

		Page* active = pk.active;
		if (active)
		{
			if (active->offset + size > active->size)
			{
				pk.in_use.push_back(active);
				active = nullptr;
				pk.active = nullptr;
			}
		}
		if (!active)
		{
			active = _activate_or_grow(pk, bReadback, size);
			H_ASSERT(active);
			pk.active = active;
		}
		const VkDeviceSize sliceOffset = active->offset;
		active->offset += size;
		active->last_use_frame = currentFrame;
		active->outstanding_slices++;
		return SliceAlloc{ active, sliceOffset };
	}

	auto VulkanStagingBufferPool::release_slice(uint32_t pageId) -> void
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (pageId >= m_pages.size()) return;
		Page* page = m_pages[pageId];
		if (!page) return;
		H_ASSERT(page->outstanding_slices > 0);
		page->outstanding_slices--;
	}

	auto VulkanStagingBufferPool::_reclaim_kind(PerKind& pk, uint64_t absoluteFrame) -> void
	{
		// Page becomes Free only when both (a) GPU done AND (b) all slices released.
		for (auto it = pk.in_use.begin(); it != pk.in_use.end(); )
		{
			Page* p = *it;
			if (p->outstanding_slices == 0 && p->last_use_frame + k_max_frames <= absoluteFrame)
			{
				p->offset = 0;
				p->freed_frame = absoluteFrame;
				it = pk.in_use.erase(it);
				pk.free.push_back(p);
			}
			else
			{
				++it;
			}
		}
		for (auto it = pk.dedicated_pending.begin(); it != pk.dedicated_pending.end(); )
		{
			Page* p = *it;
			if (p->outstanding_slices == 0 && p->last_use_frame + k_max_frames <= absoluteFrame)
			{
				H_ASSERT(p->outstanding_slices == 0);
				_destroy_page(p);
				it = pk.dedicated_pending.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	auto VulkanStagingBufferPool::_evict_kind(PerKind& pk, uint64_t absoluteFrame, uint32_t high_water) -> void
	{
		while (pk.free.size() > static_cast<size_t>(high_water + k_staging_evict_tolerance))
		{
			Page* p = pk.free.front();
			if (absoluteFrame - p->freed_frame < k_staging_page_idle_frames) break;
			pk.free.pop_front();
			_destroy_page(p);
		}
	}

	auto VulkanStagingBufferPool::frame_move() -> bool
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint64_t absoluteFrame = VulkanContext::get_absolute_frame_count();

		_reclaim_kind(m_upload, absoluteFrame);
		_reclaim_kind(m_readback, absoluteFrame);

		const uint32_t total_in_use =
			static_cast<uint32_t>(m_upload.in_use.size() + m_readback.in_use.size())
			+ (m_upload.active ? 1u : 0u) + (m_readback.active ? 1u : 0u);
		m_inUseRing[m_ringIndex % k_high_water_window] = total_in_use;
		m_ringIndex++;

		uint32_t high_water = 0;
		for (uint32_t v : m_inUseRing) if (v > high_water) high_water = v;

		_evict_kind(m_upload, absoluteFrame, high_water);
		_evict_kind(m_readback, absoluteFrame, high_water);
		return true;
	}
};

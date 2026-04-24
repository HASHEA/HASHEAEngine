#pragma once
#include "VulkanHelper.hpp"
#include <array>
#include <list>
#include <mutex>
#include <vector>

namespace RHI
{
	class VulkanStagingBufferPool;

	struct VK_SUBRESOURCE_DATA
	{
		void* DataPtr;
		uint32_t DataRowPitch;
		uint32_t DataSlicePitch;
	};

	// Slim slice handle into a pool-owned page. Holds a (page, page_offset, size) triple.
	// Lifetime: ctor allocates a slice from the pool, dtor enqueues a deferred release into
	// VulkanContext::get_current_frame_deletion_queue(); the queued lambda decrements the
	// page's outstanding-slice count when frame N's slot reaches its turn.
	class VulkanStagingBuffer
	{
		friend class VulkanStagingBufferPool;
	public:
		VulkanStagingBuffer(uint32_t uByteWidth, const VK_SUBRESOURCE_DATA* pData, bool bReadOperation);
		~VulkanStagingBuffer();

		// Underlying VkBuffer for the entire page. Callers MUST add get_buffer_offset()
		// to their VkBufferCopy::srcOffset / VkBufferImageCopy::bufferOffset.
		auto get_vkbuffer_handle() const -> VkBuffer { return m_pageBuffer; }

		// Byte offset of this slice within the page.
		auto get_buffer_offset() const -> VkDeviceSize { return m_pageOffset; }

		auto get_device_memory_size() const -> VkDeviceSize { return m_size; }

		// Pages are persistently mapped; map/unmap only manage flush range bookkeeping.
		auto map(uint32_t uOffset, uint32_t uSize) -> bool;
		auto unmap() -> bool;
		auto get_mapped_range(VkDeviceSize& uOffset, VkDeviceSize& uSize) -> bool;

		// Returns mapped pointer ALREADY ADJUSTED by m_pageOffset; callers can do
		// memcpy(get_mapped_memory() + their_local_offset, ...) without knowing about pages.
		inline auto get_mapped_memory() const { return m_sliceMapped; }
		inline auto is_mapped() const -> bool { return m_bMapped; }

	private:
		VkBuffer       m_pageBuffer = VK_NULL_HANDLE;
		uint32_t       m_pageId     = UINT32_MAX;
		VkDeviceSize   m_pageOffset = 0;
		VkDeviceSize   m_size       = 0;
		VmaAllocation  m_pageVmaAlloc = nullptr;
		void*          m_sliceMapped  = nullptr;     // page base + m_pageOffset
		uint32_t       m_uMappedDstOffset = 0;
		uint32_t       m_uMappedDstSize   = 0;
		bool           m_bMapped     = false;
		bool           m_bReadOption = false;
	};

	class VulkanStagingBufferPool
	{
		friend class VulkanStagingBuffer;
	public:
		VulkanStagingBufferPool() = default;
		~VulkanStagingBufferPool();

		auto init() -> bool;
		auto uninit() -> bool;

		// Reclaim pages whose GPU work has completed AND whose slices have all been released;
		// update high-water; evict free pages above water. Must be called from
		// VulkanContext::begin_frame AFTER delayed_deletion_queues[currentFrame].flush().
		auto frame_move() -> bool;

	private:
		struct Page
		{
			VkBuffer        buffer    = VK_NULL_HANDLE;
			VmaAllocation   alloc     = nullptr;
			VkDeviceSize    size      = 0;
			VkDeviceSize    offset    = 0;
			uint8_t*        mapped    = nullptr;
			uint64_t        last_use_frame = 0;
			uint64_t        freed_frame    = 0;
			uint32_t        outstanding_slices = 0;
			uint32_t        id        = UINT32_MAX;
			bool            bReadback = false;
			bool            dedicated = false;
		};

		struct PerKind
		{
			Page* active = nullptr;
			std::list<Page*> in_use;
			std::list<Page*> free;
			std::list<Page*> dedicated_pending;
		};

		struct SliceAlloc
		{
			Page*        page      = nullptr;
			VkDeviceSize pageOffset = 0;
		};

		// Called by VulkanStagingBuffer ctor.
		auto alloc_slice(uint32_t size, bool bReadback) -> SliceAlloc;
		// Called by deferred lambda from VulkanStagingBuffer dtor.
		auto release_slice(uint32_t pageId) -> void;

	private:
		auto _create_page(uint32_t size, bool bReadback, bool dedicated) -> Page*;
		auto _destroy_page(Page* page) -> void;
		auto _activate_or_grow(PerKind& pk, bool bReadback, uint32_t neededBytes) -> Page*;
		auto _reclaim_kind(PerKind& pk, uint64_t absoluteFrame) -> void;
		auto _evict_kind(PerKind& pk, uint64_t absoluteFrame, uint32_t high_water) -> void;

		PerKind& _kind(bool bReadback) { return bReadback ? m_readback : m_upload; }

		auto _register_page(Page* page) -> void;
		auto _unregister_page(Page* page) -> void;

	private:
		PerKind m_upload;
		PerKind m_readback;

		std::vector<Page*> m_pages;          // id -> page (sparse; nullptr entries are free slots)
		std::vector<uint32_t> m_freePageIds; // recycled ids

		static constexpr uint32_t k_high_water_window = 16u;
		std::array<uint32_t, k_high_water_window> m_inUseRing{};
		uint32_t m_ringIndex = 0;

		std::mutex m_mutex;
	};
};

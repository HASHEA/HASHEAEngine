#include "hmemory.h"
#include "tlsf.h"
#include "hlog.h"
#include "hassert.h"
#include <algorithm>
namespace AshEngine
{

	static MemoryService s_memory_service;
	// Locals
	static size_t s_size = ASH_MEGA(512) + tlsf_size() + 8;
	static void ExitWalker(void* ptr, size_t size, int used, void* user);

	void ExitWalker(void* ptr, size_t size, int used, void* user) {
		MemoryStatistics* l_pStats = (MemoryStatistics*)user;
		l_pStats->add(used ? size : 0);

		if (used)
			HLogInfo("Found active allocation {0}, {1}\n", ptr, size);
	}

	void memory_copy(void* destination, void* source, size_t size) {
		memcpy(destination, source, size);
	}

	size_t memory_align(size_t size, size_t alignment) {
		if (alignment <= 1)
		{
			return size;
		}
		const size_t alignment_mask = alignment - 1;
		return (size + alignment_mask) & ~alignment_mask;
	}

	MemoryService* MemoryService::instance() {
		return &s_memory_service;
	}
	
	auto MemoryService::init(void* configuration) -> bool
	{
		HLogInfo("Memory Service Init\n");
		MemoryServiceConfiguration* memory_configuration = static_cast<MemoryServiceConfiguration*>(configuration);
		bool ret = m_heapAllocator.init(memory_configuration ? memory_configuration->m_szMaxDynamicSize : s_size);
		return ret;
	}
	auto MemoryService::shutdown() -> bool
	{
		HLogInfo("Memory Service Shutdown...\n");
		bool ret = m_heapAllocator.shutdown();
		if (!ret)
		{
			HLogError("Memory Service Shutdown Failed!\n");
		}
		
		return ret;
	}

#ifdef ASH_DEBUG
	auto MemoryService::on_gui() -> void
	{

	}
#endif // ASH_DEBUG

	

	/*************** Heap Allocator *****************/

	HeapAllocator::~HeapAllocator()
	{
	}
	auto HeapAllocator::init(size_t size) -> bool
	{
		if (size == 0)
		{
			HLogError("HeapAllocator::init failed: size is zero.");
			return false;
		}
		m_pMemory = (uint8_t*)malloc(size);
		if (!m_pMemory)
		{
			HLogError("HeapAllocator::init failed: malloc returned null for {} bytes.", size);
			return false;
		}
		m_szMaxSize = size;
		m_szAllocatedSize = 0;
		m_szPeakAllocatedSize = 0;
		m_liveAllocationCount = 0;
		m_peakAllocationCount = 0;
		m_pTlsfHandle = tlsf_create_with_pool(m_pMemory,size);
		bool ret = (m_pTlsfHandle != nullptr)? true : false;
		if (!ret)
		{
			free(m_pMemory);
			m_pMemory = nullptr;
			m_szMaxSize = 0;
			m_szAllocatedSize = 0;
			m_szPeakAllocatedSize = 0;
			m_liveAllocationCount = 0;
			m_peakAllocationCount = 0;
			HLogError("tlsf create pool failed with address : {0},  size : {1}", m_pMemory, size);
		}
		return ret;
	}
	auto HeapAllocator::shutdown() -> bool
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pTlsfHandle)
		{
			free(m_pMemory);
			m_pMemory = nullptr;
			m_szMaxSize = 0;
			m_szAllocatedSize = 0;
			m_szPeakAllocatedSize = 0;
			m_liveAllocationCount = 0;
			m_peakAllocationCount = 0;
			return true;
		}
		// Check memory at the application exit.
		MemoryStatistics stats{ 0, m_szMaxSize };
		pool_t pool = tlsf_get_pool(m_pTlsfHandle);
		tlsf_walk_pool(pool, ExitWalker, (void*)&stats);
		if (stats.m_szAllocatedbytes != 0)
		{
			HLogWarning("Heap Allocator Shutdown Failed! Allocated Memory Detected ! Allocated {0} bytes, total {1} bytes", stats.m_szAllocatedbytes,stats.m_szTotalBytes);
		}
		else
		{
			HLogInfo("Heap Allocator Shutdown ! -- All Memory Free!");
		}
		H_ASSERT(stats.m_szAllocatedbytes == 0);
		tlsf_destroy(m_pTlsfHandle);
		free(m_pMemory);
		m_pTlsfHandle = nullptr;
		m_pMemory = nullptr;
		m_szMaxSize = 0;
		m_szAllocatedSize = 0;
		m_szPeakAllocatedSize = 0;
		m_liveAllocationCount = 0;
		m_peakAllocationCount = 0;
		return true;
	}
#ifdef ASH_DEBUG
	auto HeapAllocator::on_gui() -> void
	{
	}
#endif // ASH_DEBUG

	auto HeapAllocator::get_stats() const -> HeapMemoryStats
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		HeapMemoryStats stats{};
		stats.current_allocated_bytes = m_szAllocatedSize;
		stats.peak_allocated_bytes = m_szPeakAllocatedSize;
		stats.live_allocation_count = m_liveAllocationCount;
		stats.peak_allocation_count = m_peakAllocationCount;
		return stats;
	}
	
	auto HeapAllocator::allocate(size_t size, size_t alignment)->void*
	{
		if (size == 0)
		{
			HLogError("HeapAllocator::allocate failed: size is zero.");
			return nullptr;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pTlsfHandle)
		{
			HLogError("HeapAllocator::allocate failed: allocator is not initialized.");
			return nullptr;
		}
		void* pAllocateMemory = alignment == 1? tlsf_malloc(m_pTlsfHandle, size) : tlsf_memalign(m_pTlsfHandle, alignment, size);
		if (!pAllocateMemory)
		{
			HLogError("HeapAllocator::allocate failed: size={}, alignment={}.", size, alignment);
			return nullptr;
		}
		size_t actualSize = tlsf_block_size(pAllocateMemory);
		m_szAllocatedSize += actualSize;
		++m_liveAllocationCount;
		m_szPeakAllocatedSize = std::max(m_szPeakAllocatedSize, m_szAllocatedSize);
		m_peakAllocationCount = std::max(m_peakAllocationCount, m_liveAllocationCount);
		return pAllocateMemory;
	}
	auto HeapAllocator::allocate(size_t size, size_t alignment, char* file, uint32_t line)->void*
	{
		if (size == 0)
		{
			HLogError("HeapAllocator::allocate failed: size is zero at {}:{}.", file ? file : "<unknown>", line);
			return nullptr;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pTlsfHandle)
		{
			HLogError("HeapAllocator::allocate failed: allocator is not initialized at {}:{}.", file ? file : "<unknown>", line);
			return nullptr;
		}
		void* pAllocateMemory = alignment == 1 ? tlsf_malloc(m_pTlsfHandle, size) : tlsf_memalign(m_pTlsfHandle, alignment, size);
		if (!pAllocateMemory)
		{
			HLogError("HeapAllocator::allocate failed: size={}, alignment={}, from {}:{}.", size, alignment, file ? file : "<unknown>", line);
			return nullptr;
		}
		size_t actualSize = tlsf_block_size(pAllocateMemory);
		m_szAllocatedSize += actualSize;
		++m_liveAllocationCount;
		m_szPeakAllocatedSize = std::max(m_szPeakAllocatedSize, m_szAllocatedSize);
		m_peakAllocationCount = std::max(m_peakAllocationCount, m_liveAllocationCount);
#ifdef ASH_TRACE_MEM_ALLOCATE
		HLogTrace("allocate new mem at : {0}, size : {1}, actualSize : {2}, from : {3} - line : {4}", pAllocateMemory, size,actualSize,file,line);
#endif //ASH_DEBUG
		return pAllocateMemory;
	}
	auto HeapAllocator::deallocate(void* pointer, char* file, uint32_t line)->bool
	{
		if (!pointer)
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pTlsfHandle)
		{
			HLogError("HeapAllocator::deallocate failed: allocator is not initialized at {}:{}.", file ? file : "<unknown>", line);
			return false;
		}
		size_t actual_size = tlsf_block_size(pointer);
		m_szAllocatedSize = actual_size <= m_szAllocatedSize ? m_szAllocatedSize - actual_size : 0;
		if (m_liveAllocationCount > 0)
		{
			--m_liveAllocationCount;
		}

		tlsf_free(m_pTlsfHandle, pointer);
#ifdef ASH_TRACE_MEM_DEALLOCATE
		HLogTrace("deallocate mem at : {0}, size : {1}, from : {2} - line : {3}", pointer, actual_size, file, line);
#endif //ASH_DEBUG
		return true;
	}

	auto HeapAllocator::deallocate(void* pointer) -> bool 
	{
		if (!pointer)
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pTlsfHandle)
		{
			HLogError("HeapAllocator::deallocate failed: allocator is not initialized.");
			return false;
		}
		size_t actual_size = tlsf_block_size(pointer);
		m_szAllocatedSize = actual_size <= m_szAllocatedSize ? m_szAllocatedSize - actual_size : 0;
		if (m_liveAllocationCount > 0)
		{
			--m_liveAllocationCount;
		}
		tlsf_free(m_pTlsfHandle, pointer);
		return true;
	}

	auto HeapAllocator::deallocate(const void* pointer) -> bool
	{
		auto dPoint = const_cast<void*>(pointer);
		if (!dPoint)
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pTlsfHandle)
		{
			HLogError("HeapAllocator::deallocate failed: allocator is not initialized.");
			return false;
		}
		size_t actual_size = tlsf_block_size(dPoint);
		m_szAllocatedSize = actual_size <= m_szAllocatedSize ? m_szAllocatedSize - actual_size : 0;
		if (m_liveAllocationCount > 0)
		{
			--m_liveAllocationCount;
		}
		tlsf_free(m_pTlsfHandle, dPoint);
		return true;
	}
	auto HeapAllocator::deallocate(const void* pointer, char* file, uint32_t line) -> bool
	{
		auto dPoint = const_cast<void*>(pointer);
		if (!dPoint)
		{
			return false;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pTlsfHandle)
		{
			HLogError("HeapAllocator::deallocate failed: allocator is not initialized at {}:{}.", file ? file : "<unknown>", line);
			return false;
		}
		size_t actual_size = tlsf_block_size(dPoint);
		m_szAllocatedSize = actual_size <= m_szAllocatedSize ? m_szAllocatedSize - actual_size : 0;
		if (m_liveAllocationCount > 0)
		{
			--m_liveAllocationCount;
		}
		tlsf_free(m_pTlsfHandle, dPoint);
#ifdef ASH_TRACE_MEM_DEALLOCATE
		HLogTrace("deallocate mem at : {0}, size : {1}, from : {2} - line : {3}", pointer, actual_size, file, line);
#endif //ASH_DEBUG
		return true;
	}



	/*************** Stack Allocator *****************/

	auto StackAllocator::init(size_t size) -> bool
	{
		if (size == 0)
		{
			HLogError("StackAllocator::init failed: size is zero.");
			return false;
		}
		m_pMemory = (uint8_t*)malloc(size);
		if (!m_pMemory)
		{
			HLogError("StackAllocator::init failed: malloc returned null for {} bytes.", size);
			return false;
		}
		m_szAllocatedSize = 0;
		m_szTotalSize = size;
		return true;
	}
	auto StackAllocator::shutdown() -> bool
	{
		free(m_pMemory);
		m_pMemory = nullptr;
		m_szAllocatedSize = 0;
		m_szTotalSize = 0;
		return true;
	}
	auto StackAllocator::allocate(size_t size, size_t alignment) -> void*
	{
		if (size == 0 || !m_pMemory)
		{
			return nullptr;
		}
		const size_t newStart = memory_align(m_szAllocatedSize,alignment);
		if (newStart >= m_szTotalSize)
		{
			return nullptr;
		}
		const size_t new_allocated_size = newStart + size;
		if (new_allocated_size < newStart || new_allocated_size > m_szTotalSize)
			return nullptr;
		m_szAllocatedSize = new_allocated_size;
		return m_pMemory + newStart;
	}
	auto StackAllocator::allocate(size_t size, size_t alignment, char* file, uint32_t line)-> void*
	{
		return allocate(size,alignment);
	}
	auto StackAllocator::deallocate(void* pointer,char* file, uint32_t line) -> bool
	{
		H_ASSERT(pointer >= m_pMemory);
		H_ASSERTLOG(pointer < m_pMemory + m_szTotalSize, "out of bounds free on stack allocator Tempting to free {0}, %llu after beginning of buffer (memory {1} size {2}, allocated {3})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		H_ASSERTLOG(pointer < m_pMemory + m_szAllocatedSize, "Out of bound free on stack allocator (inside bounds, after allocated). Tempting to free {0}, {1} after beginning of buffer (memory {2} size {3}, allocated {4})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		const size_t size_at_pointer = (uint8_t*)pointer - m_pMemory;
		m_szAllocatedSize = size_at_pointer;
		return true;
	}
	auto StackAllocator::deallocate(void* pointer) -> bool 
	{
		H_ASSERT(pointer >= m_pMemory);
		H_ASSERTLOG(pointer < m_pMemory + m_szTotalSize, "out of bounds free on stack allocator Tempting to free {0}, %llu after beginning of buffer (memory {1} size {2}, allocated {3})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		H_ASSERTLOG(pointer < m_pMemory + m_szAllocatedSize, "Out of bound free on stack allocator (inside bounds, after allocated). Tempting to free {0}, {1} after beginning of buffer (memory {2} size {3}, allocated {4})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		const size_t size_at_pointer = (uint8_t*)pointer - m_pMemory;
		m_szAllocatedSize = size_at_pointer;
		return true;
	}
	auto StackAllocator::deallocate(const void* pointer) -> bool
	{
		H_ASSERT(pointer >= m_pMemory);
		H_ASSERTLOG(pointer < m_pMemory + m_szTotalSize, "out of bounds free on stack allocator Tempting to free {0}, %llu after beginning of buffer (memory {1} size {2}, allocated {3})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		H_ASSERTLOG(pointer < m_pMemory + m_szAllocatedSize, "Out of bound free on stack allocator (inside bounds, after allocated). Tempting to free {0}, {1} after beginning of buffer (memory {2} size {3}, allocated {4})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		const size_t size_at_pointer = (uint8_t*)pointer - m_pMemory;
		m_szAllocatedSize = size_at_pointer;
		return true;
	}
	auto StackAllocator::deallocate(const void* pointer, char* file, uint32_t line) -> bool
	{
		H_ASSERT(pointer >= m_pMemory);
		H_ASSERTLOG(pointer < m_pMemory + m_szTotalSize, "out of bounds free on stack allocator Tempting to free {0}, %llu after beginning of buffer (memory {1} size {2}, allocated {3})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		H_ASSERTLOG(pointer < m_pMemory + m_szAllocatedSize, "Out of bound free on stack allocator (inside bounds, after allocated). Tempting to free {0}, {1} after beginning of buffer (memory {2} size {3}, allocated {4})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		const size_t size_at_pointer = (uint8_t*)pointer - m_pMemory;
		m_szAllocatedSize = size_at_pointer;
		return true;
	}
	auto StackAllocator::get_marker() -> size_t
	{
		return m_szAllocatedSize;
	}
	auto StackAllocator::free_marker(size_t marker) -> bool
	{
		if (marker > m_szAllocatedSize)
		{
			HLogWarning("StackAllocator rejected forward marker free. marker={}, allocated={}", marker, m_szAllocatedSize);
			return false;
		}
		m_szAllocatedSize = marker;
		return true;
	}
	auto StackAllocator::clear()->bool
	{
		m_szAllocatedSize = 0;
		return true;
	}

	/************** Linear Allocator **********************/


	LinearAllocator::~LinearAllocator()
	{
	}
	auto LinearAllocator::init(size_t size) -> bool
	{
		if (size == 0)
		{
			HLogError("LinearAllocator::init failed: size is zero.");
			return false;
		}
		m_pMemory = (uint8_t*)malloc(size);
		if (!m_pMemory)
		{
			HLogError("LinearAllocator::init failed: malloc returned null for {} bytes.", size);
			return false;
		}
		memset(m_pMemory, 0, size);
		m_szTotalSize = size;
		m_szAllocatedSize = 0;
		return true;
	}
	auto LinearAllocator::shutdown() -> bool
	{
		bool ret = clear();
		H_ASSERT(ret);
		free(m_pMemory);
		m_pMemory = nullptr;
		m_szTotalSize = 0;
		m_szAllocatedSize = 0;
		return true;
	}
	auto LinearAllocator::allocate(size_t size, size_t alignment)->void*
	{
		if (size == 0 || !m_pMemory)
		{
			return nullptr;
		}
		const size_t new_start = memory_align(m_szAllocatedSize, alignment);
		if (new_start >= m_szTotalSize)
		{
			return nullptr;
		}

		const size_t new_allocated_size = new_start + size;
		if (new_allocated_size < new_start || new_allocated_size > m_szTotalSize)
		{
			return nullptr;
		}
		m_szAllocatedSize = new_allocated_size;
		return m_pMemory + new_start;
	}
	auto LinearAllocator::allocate(size_t size, size_t alignment, char* file, uint32_t line) -> void*
	{
		return allocate(size, alignment);
	}
	auto LinearAllocator::deallocate(void* pointer, char* file, uint32_t line) -> bool
	{
		HLogWarning("LinearAllocator does not support individual deallocate. Use clear() to reset the allocator.");
		return false;
	}
	auto LinearAllocator::deallocate(void* pointer) -> bool 
	{
		HLogWarning("LinearAllocator does not support individual deallocate. Use clear() to reset the allocator.");
		return false;
	}
	auto LinearAllocator::deallocate(const void* pointer) -> bool
	{
		HLogWarning("LinearAllocator does not support individual deallocate. Use clear() to reset the allocator.");
		return false;
	}
	auto LinearAllocator::deallocate(const void* pointer, char* file, uint32_t line) -> bool
	{
		HLogWarning("LinearAllocator does not support individual deallocate. Use clear() to reset the allocator.");
		return false;
	}
	auto LinearAllocator::clear() -> bool
	{
		m_szAllocatedSize = 0;
		return true;
	}
};

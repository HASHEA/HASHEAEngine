#include "hmemory.h"
#include "tlsf.h"
#include "hlog.h"
#include "hassert.h"
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
		const size_t alignment_mask = alignment - 1;
		return (size + alignment_mask) & ~alignment_mask;
	}

	MemoryService* MemoryService::instance() {
		return &s_memory_service;
	}
	
	auto MemoryService::init(void* configuration) -> HS_Result
	{
		HLogInfo("Memory Service Init\n");
		MemoryServiceConfiguration* memory_configuration = static_cast<MemoryServiceConfiguration*>(configuration);
		HS_Result ret = m_heapAllocator.init(memory_configuration ? memory_configuration->m_szMaxDynamicSize : s_size);
		return ret;
	}
	auto MemoryService::shutdown() -> HS_Result
	{
		HLogInfo("Memory Service Shutdown...\n");
		HS_Result ret = m_heapAllocator.shutdown();
		if (HS_CHECK_FAILED(ret))
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
	auto HeapAllocator::init(size_t size) -> HS_Result
	{
		H_ASSERT(size > 0);
		m_pMemory = (uint8_t*)malloc(size);
		H_ASSERT(m_pMemory);
		m_szMaxSize = size;
		m_pTlsfHandle = tlsf_create_with_pool(m_pMemory,size);
		HS_Result ret = (m_pTlsfHandle != nullptr)? HS_OK : HS_FAIL;
		if (HS_CHECK_FAILED(ret))
		{
			free(m_pMemory);
			HLogError("tlsf create pool failed with address : {0},  size : {1}", m_pMemory, size);
		}
		return ret;
	}
	auto HeapAllocator::shutdown() -> HS_Result
	{
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
		return HS_OK;
	}
#ifdef ASH_DEBUG
	auto HeapAllocator::on_gui() -> void
	{
	}
#endif // ASH_DEBUG

	
	auto HeapAllocator::allocate(size_t size, size_t alignment)->void*
	{
		H_ASSERT(size > 0);
		void* pAllocateMemory = alignment == 1? tlsf_malloc(m_pTlsfHandle, size) : tlsf_memalign(m_pTlsfHandle, alignment, size);
		H_ASSERT(pAllocateMemory);
		size_t actualSize = tlsf_block_size(pAllocateMemory);
		m_szAllocatedSize += actualSize;
		return pAllocateMemory;
	}
	auto HeapAllocator::allocate(size_t size, size_t alignment, char* file, uint32_t line)->void*
	{
		H_ASSERT(size > 0);
		auto pdata = allocate(size, alignment);
#ifdef ASH_DEBUG
		HLogTrace("allocate new mem at : {0} size : {1} from : {2} - line : {3}",pdata, size,file,line);
#endif //ASH_DEBUG
		return pdata;
	}
	auto HeapAllocator::deallocate(void* pointer)->HS_Result
	{
		H_ASSERT(pointer);
		size_t actual_size = tlsf_block_size(pointer);
		m_szAllocatedSize -= actual_size;

		tlsf_free(m_pTlsfHandle, pointer);
		return HS_OK;
	}


	/*************** Stack Allocator *****************/

	auto StackAllocator::init(size_t size) -> HS_Result
	{
		H_ASSERT(size > 0);
		m_pMemory = (uint8_t*)malloc(size);
		H_ASSERT(m_pMemory);
		m_szAllocatedSize = 0;
		m_szTotalSize = size;
		return HS_OK;
	}
	auto StackAllocator::shutdown() -> HS_Result
	{
		free(m_pMemory);
		return HS_OK;
	}
	auto StackAllocator::allocate(size_t size, size_t alignment) -> void*
	{
		H_ASSERT(size > 0);
		const size_t newStart = memory_align(m_szAllocatedSize,alignment);
		H_ASSERT(newStart < m_szTotalSize);
		const size_t new_allocated_size = newStart + size;
		if (new_allocated_size > m_szTotalSize)
			return nullptr;
		m_szAllocatedSize = new_allocated_size;
		return m_pMemory + newStart;
	}
	auto StackAllocator::allocate(size_t size, size_t alignment, char* file, uint32_t line)-> void*
	{
		return allocate(size,alignment);
	}
	auto StackAllocator::deallocate(void* pointer) -> HS_Result
	{
		H_ASSERT(pointer >= m_pMemory);
		H_ASSERTLOG(pointer < m_pMemory + m_szTotalSize, "out of bounds free on stack allocator Tempting to free {0}, %llu after beginning of buffer (memory {1} size {2}, allocated {3})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		H_ASSERTLOG(pointer < m_pMemory + m_szAllocatedSize, "Out of bound free on stack allocator (inside bounds, after allocated). Tempting to free {0}, {1} after beginning of buffer (memory {2} size {3}, allocated {4})", (uint8_t*)pointer, (uint8_t*)pointer - m_pMemory, m_pMemory, m_szTotalSize, m_szAllocatedSize);
		const size_t size_at_pointer = (uint8_t*)pointer - m_pMemory;
		m_szAllocatedSize = size_at_pointer;
		return HS_OK;
	}
	auto StackAllocator::get_marker() -> size_t
	{
		return m_szAllocatedSize;
	}
	auto StackAllocator::free_marker(size_t marker) -> HS_Result
	{
		const size_t difference = marker - m_szAllocatedSize;
		if (difference > 0)
		{
			m_szAllocatedSize = marker;
		}
		return HS_OK;
	}
	auto StackAllocator::clear()->HS_Result
	{
		m_szAllocatedSize = 0;
		return HS_OK;
	}

	/************** Linear Allocator **********************/


	LinearAllocator::~LinearAllocator()
	{
	}
	auto LinearAllocator::init(size_t size) -> HS_Result
	{
		H_ASSERT(size > 0);
		m_pMemory = (uint8_t*)malloc(size);
		H_ASSERT(m_pMemory);
		memset(m_pMemory, 0, size);
		m_szTotalSize = size;
		m_szAllocatedSize = 0;
		return HS_OK;
	}
	auto LinearAllocator::shutdown() -> HS_Result
	{
		bool ret = clear();
		H_ASSERT(ret);
		free(m_pMemory);
		return HS_OK;
	}
	auto LinearAllocator::allocate(size_t size, size_t alignment)->void*
	{
		H_ASSERT(size > 0);
		const size_t new_start = memory_align(m_szAllocatedSize, alignment);
		H_ASSERT(new_start < m_szTotalSize);

		const size_t new_allocated_size = new_start + size;
		if (new_allocated_size > m_szTotalSize)
		{
			return nullptr;
		}
		m_szAllocatedSize = new_allocated_size;
		return m_pMemory + new_start;
	}
	auto LinearAllocator::allocate(size_t size, size_t alignment, char* file, uint32_t line) -> void*
	{
		H_ASSERT(size > 0);
		return allocate(size, alignment);
	}
	auto LinearAllocator::deallocate(void* pointer) -> HS_Result
	{
		return HS_OK;
	}
	auto LinearAllocator::clear() -> HS_Result
	{
		m_szAllocatedSize = 0;
		return HS_OK;
	}
};
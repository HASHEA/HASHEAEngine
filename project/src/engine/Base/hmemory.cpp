#pragma once
#include "hmemory.h"
#include "tlsf.h"
#include "hlog.h"
#include "hassert.h"
namespace HASHEAENGINE
{

	static MemoryService s_memory_service;
	// Locals
	static size_t s_size = HASHEA_MEGA(512) + tlsf_size() + 8;
	static void ExitWalker(void* ptr, size_t size, int used, void* user);

	void ExitWalker(void* ptr, size_t size, int used, void* user) {
		MemoryStatistics* l_pStats = (MemoryStatistics*)user;
		l_pStats->Add(used ? size : 0);

		if (used)
			HLogInfo("Found active allocation {0}, {1}\n", ptr, size);
	}


	MemoryService* MemoryService::instance() {
		return &s_memory_service;
	}
	
	auto MemoryService::Init(void* configuration) -> bool 
	{
		HLogInfo("Memory Service Init\n");
		MemoryServiceConfiguration* memory_configuration = static_cast<MemoryServiceConfiguration*>(configuration);
		bool ret = m_heapAllocator.Init(memory_configuration ? memory_configuration->m_szMaxDynamicSize : s_size);
		return ret;
	}
	auto MemoryService::Shutdown() -> bool 
	{
		HLogInfo("Memory Service Shutdown...\n");
		bool ret = m_heapAllocator.Shutdown();
		if (!ret)
		{
			HLogError("Memory Service Shutdown Failed!\n");
		}
		
		return ret;
	}

#ifdef HASHEA_DEBUG
	auto MemoryService::OnGUI() -> void
	{

	}
#endif // HASHEA_DEBUG

	

	/*************** Heap Allocator *****************/

	HeapAllocator::~HeapAllocator()
	{
	}
	auto HeapAllocator::Init(size_t size) -> bool
	{
		m_pMemory = malloc(size);
		m_szMaxSize = size;
		m_pTlsfHandle = tlsf_create_with_pool(m_pMemory,size);
		bool ret = m_pTlsfHandle != nullptr;
		if (!ret)
		{
			HLogError("tlsf create pool failed with address : {0},  size : {1}", m_pMemory, size);
		}
		return ret;
	}
	auto HeapAllocator::Shutdown() -> bool
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
		return true;
	}
#ifdef HASHEA_DEBUG
	auto HeapAllocator::OnGUI() -> void
	{
	}
#endif // HASHEA_DEBUG

	
	auto HeapAllocator::Allocate(size_t size, size_t alignment)->void*
	{
		H_ASSERT(size > 0);
		void* pAllocateMemory = alignment == 1? tlsf_malloc(m_pTlsfHandle, size) : tlsf_memalign(m_pTlsfHandle, alignment, size);
		H_ASSERT(pAllocateMemory);
		size_t actualSize = tlsf_block_size(pAllocateMemory);
		m_szAllocatedSize += actualSize;
		return pAllocateMemory;
	}
	auto HeapAllocator::Allocate(size_t size, size_t alignment, char* file, uint32_t line)->void*
	{
		H_ASSERT(size > 0);
		return Allocate(size, alignment);
	}
	auto HeapAllocator::Deallocate(void* pointer)->bool
	{
		size_t actual_size = tlsf_block_size(pointer);
		m_szAllocatedSize -= actual_size;

		tlsf_free(m_pTlsfHandle, pointer);
		return true;
	}


	/*************** Stack Allocator *****************/

	auto StackAllocator::Init(size_t size) -> bool
	{
		m_pMemory = (uint8_t*)malloc(size);
		m_szAllocatedSize = 0;
		m_szTotalSize = size;
	}
	auto StackAllocator::Shutdown() -> bool
	{
		free(m_pMemory);
	}
	auto StackAllocator::Allocate(size_t size, size_t alignment) -> void*
	{
		return nullptr;
	}
	auto StackAllocator::Allocate(size_t size, size_t alignment, char* file, uint32_t line)-> void*
	{
		return nullptr;
	}
	auto StackAllocator::Deallocate(void* pointer) -> bool
	{
	}
	auto StackAllocator::GetMarker() -> size_t
	{
		return size_t();
	}
	auto StackAllocator::FreeMarker(size_t marker) -> bool
	{
	}
	auto StackAllocator::Clear()->bool
	{
	}

	/************** Linear Allocator **********************/

	void MemoryCopy(void* destination, void* source, size_t size) {
		memcpy(destination, source, size);
	}

	size_t MemoryAlign(size_t size, size_t alignment) {
		const size_t alignment_mask = alignment - 1;
		return (size + alignment_mask) & ~alignment_mask;
	}

	LinearAllocator::~LinearAllocator()
	{
	}
	auto LinearAllocator::Init(size_t size) -> bool
	{
		m_pMemory = (uint8_t*)malloc(size);
		m_szTotalSize = size;
		m_szAllocatedSize = 0;
	}
	auto LinearAllocator::Shutdown() -> bool
	{
		bool ret = Clear();
		H_ASSERT(ret);
		free(m_pMemory);
	}
	auto LinearAllocator::Allocate(size_t size, size_t alignment)->void*
	{
		H_ASSERT(size > 0);
		const size_t new_start = MemoryAlign(m_szAllocatedSize, alignment);
		H_ASSERT(new_start < m_szTotalSize);

		const size_t new_allocated_size = new_start + size;
		m_szAllocatedSize = new_allocated_size;
		return m_pMemory + new_start;
	}
	auto LinearAllocator::Allocate(size_t size, size_t alignment, char* file, uint32_t line) -> void*
	{
		H_ASSERT(size > 0);
		return Allocate(size, alignment);
	}
	auto LinearAllocator::Deallocate(void* pointer) -> bool
	{
	}
	auto LinearAllocator::Clear() -> bool
	{

		m_szAllocatedSize = 0;
	}
};
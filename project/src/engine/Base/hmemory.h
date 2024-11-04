#pragma once
#include <string>
#include <stdarg.h>
#include "hcore.h"
#include "hservice.h"
namespace HASHEAENGINE
{
	enum AllocType
	{
		eHeap,
		eStack,
		eLinear,
	};
	
	struct Allocator 
	{
		virtual ~Allocator() { }
		virtual auto Allocate(size_t size, size_t alignment) -> void* = 0;
		virtual auto Allocate(size_t size, size_t alignment, char* file, uint32_t line) -> void* = 0;

		virtual auto Deallocate(void* pointer) -> bool = 0;
	}; // struct Allocator

	struct MemoryStatistics {
		size_t                       m_szAllocatedbytes;
		size_t                       m_szTotalBytes;

		uint32_t                         m_uAllocationCounts;

		auto Add(size_t a) -> bool {
			if (a) {
				m_szAllocatedbytes += a;
				++m_uAllocationCounts;
			}
		}
	}; // struct MemoryStatistics

	class HeapAllocator : public Allocator
	{
	public:
		~HeapAllocator() override;

		auto Init(size_t size) -> bool;
		auto Shutdown()->bool;

#ifdef HASHEA_DEBUG
		auto OnGUI() -> void;
#endif // HASHEA_DEBUG

		auto Allocate(size_t size, size_t alignment) -> void* override;
		auto Allocate(size_t size, size_t alignment, char* file, uint32_t line)->void* override;

		auto Deallocate(void* pointer)->bool override;
	private:
		void* m_pTlsfHandle = nullptr;
		void* m_pMemory = nullptr;
		size_t                       m_szAllocatedSize = 0;
		size_t                       m_szMaxSize = 0;
	}; // struct HeapAllocator

	class StackAllocator : public Allocator
	{
	public:
		auto                        Init(size_t size) -> bool;
		auto                        Shutdown() -> bool;

		auto Allocate(size_t size, size_t alignment)-> void* override;
		auto Allocate(size_t size, size_t alignment, char* file, uint32_t line)-> void* override;

		auto                        Deallocate(void* pointer)->bool override;

		auto                       GetMarker() -> size_t;
		auto                        FreeMarker(size_t marker) -> bool;

		auto                        Clear() -> bool;
	private:
		uint8_t* m_pMemory = nullptr;
		size_t                       m_szTotalSize = 0;
		size_t                       m_szAllocatedSize = 0;

	}; // struct StackAllocator

	//
// Allocator that can only be reset.
//
	class LinearAllocator : public Allocator 
	{
	public:
		~LinearAllocator();

		auto                        Init(size_t size) -> bool;
		auto                        Shutdown()->bool;

		auto Allocate(size_t size, size_t alignment)->void* override;
		auto Allocate(size_t size, size_t alignment, char* file, uint32_t line)->void* override;

		auto                        Deallocate(void* pointer)->bool override;

		auto                        Clear()->bool;
	private:
		uint8_t* m_pMemory = nullptr;
		size_t                       m_szTotalSize = 0;
		size_t                       m_szAllocatedSize = 0;
	}; // struct LinearAllocator
	struct MemoryServiceConfiguration {

		size_t                       m_szMaxDynamicSize = 512 * 1024 * 1024;    // Defaults to max 32MB of dynamic memory.

	};
	class MemoryService : public Service
	{
	public:
		auto Init(void* configuration) -> bool override;
		auto Shutdown() -> bool override;
#ifdef HASHEA_DEBUG
		auto OnGUI() -> void override;
#endif // HASHEA_DEBUG
		HASHEA_DECLARE_SERVICE(MemoryService);
	private:
		// Global Frame allocator
		StackAllocator m_stackAllocator{};
		HeapAllocator m_heapAllocator{};
	};

#define HASHEA_KILO(size)                 (size * 1024)
#define HASHEA_MEGA(size)                 (size * 1024 * 1024)
#define HASHEA_GIGA(size)                 (size * 1024 * 1024 * 1024)

};

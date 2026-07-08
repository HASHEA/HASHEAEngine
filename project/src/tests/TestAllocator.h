#pragma once
#include "Base/hmemory.h"
#include <malloc.h>

namespace AshEngineTests
{
	struct MallocAllocator final : AshEngine::Allocator
	{
		auto allocate(size_t size, size_t alignment) -> void* override
		{
			return _aligned_malloc(size, alignment == 0 ? 1 : alignment);
		}

		auto allocate(size_t size, size_t alignment, char*, uint32_t) -> void* override
		{
			return allocate(size, alignment);
		}

		auto deallocate(void* pointer) -> bool override
		{
			_aligned_free(pointer);
			return true;
		}

		auto deallocate(void* pointer, char*, uint32_t) -> bool override
		{
			return deallocate(pointer);
		}

		auto deallocate(const void* pointer) -> bool override
		{
			return deallocate(const_cast<void*>(pointer));
		}

		auto deallocate(const void* pointer, char*, uint32_t) -> bool override
		{
			return deallocate(const_cast<void*>(pointer));
		}
	};
}

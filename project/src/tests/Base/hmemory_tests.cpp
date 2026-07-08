#include "doctest.h"
#include "Base/hmemory.h"
#include "Base/ProcessMemoryDiagnostics.h"

#include <cstdint>

using AshEngine::HeapMemoryStats;
using AshEngine::LinearAllocator;
using AshEngine::MemoryService;
using AshEngine::ProcessMemorySnapshot;
using AshEngine::StackAllocator;

namespace
{
	struct alignas(64) OverAlignedTestType
	{
		uint32_t value = 0;
	};
}

TEST_CASE("Ash_New respects over-aligned types")
{
	OverAlignedTestType* object = AshEngine::Ash_New<OverAlignedTestType>();
	REQUIRE(object != nullptr);
	CHECK(reinterpret_cast<uintptr_t>(object) % alignof(OverAlignedTestType) == 0);
	AshEngine::_original_destroy(object);
	MemoryService::instance()->get_system_allocator()->deallocate(object);
}

TEST_CASE("MemoryService heap statistics track allocation lifecycle")
{
	const HeapMemoryStats before = MemoryService::instance()->get_heap_stats();
	void* allocation = MemoryService::instance()->get_system_allocator()->allocate(128, 16);
	REQUIRE(allocation != nullptr);
	const HeapMemoryStats during = MemoryService::instance()->get_heap_stats();
	MemoryService::instance()->get_system_allocator()->deallocate(allocation);
	const HeapMemoryStats after = MemoryService::instance()->get_heap_stats();

	CHECK(during.current_allocated_bytes > before.current_allocated_bytes);
	CHECK(during.peak_allocated_bytes >= during.current_allocated_bytes);
	CHECK(during.live_allocation_count > before.live_allocation_count);
	CHECK(after.current_allocated_bytes == before.current_allocated_bytes);
}

TEST_CASE("process memory snapshot reports platform support")
{
	const ProcessMemorySnapshot snapshot = AshEngine::get_current_process_memory_snapshot();
#if defined(ASH_WINDOWS)
	CHECK(snapshot.supported);
	CHECK(snapshot.working_set_bytes > 0);
	CHECK(snapshot.private_bytes > 0);
#else
	CHECK_FALSE(snapshot.supported);
#endif
}

TEST_CASE("StackAllocator marker rollback rejects forward free")
{
	StackAllocator allocator{};
	REQUIRE(allocator.init(256));

	void* first = allocator.allocate(32, 8);
	const size_t marker = allocator.get_marker();
	void* second = allocator.allocate(32, 8);
	CHECK(first != nullptr);
	CHECK(second != nullptr);

	CHECK(allocator.free_marker(marker));
	CHECK(allocator.get_marker() == marker);

	CHECK_FALSE(allocator.free_marker(marker + 64));
	CHECK(allocator.get_marker() == marker);

	allocator.shutdown();
}

TEST_CASE("LinearAllocator deallocate reports unsupported")
{
	LinearAllocator allocator{};
	REQUIRE(allocator.init(128));

	void* allocation = allocator.allocate(16, 8);
	REQUIRE(allocation != nullptr);
	CHECK_FALSE(allocator.deallocate(allocation));

	allocator.shutdown();
}

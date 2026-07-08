#include "doctest.h"
#include "Base/ds/harray.hpp"
#include "TestAllocator.h"

using AshEngine::Array;
using AshEngineTests::MallocAllocator;

TEST_CASE("Array push_back grows past initial capacity and preserves order")
{
	MallocAllocator allocator;
	Array<int> values;
	REQUIRE(values.init(&allocator, 2));

	for (int i = 0; i < 17; ++i)
	{
		REQUIRE(values.push_back(i * 10));
	}

	CHECK(values.size() == 17u);
	CHECK(values.capacity() >= 17u);
	for (uint32_t i = 0; i < values.size(); ++i)
	{
		CHECK(values[i] == static_cast<int>(i) * 10);
	}
	CHECK(values.front() == 0);
	CHECK(values.back() == 160);

	REQUIRE(values.shutdown());
}

TEST_CASE("Array init with initial_size zero-fills trivial elements")
{
	MallocAllocator allocator;
	Array<int> values;
	REQUIRE(values.init(&allocator, 0, 4));

	CHECK(values.size() == 4u);
	for (uint32_t i = 0; i < values.size(); ++i)
	{
		CHECK(values[i] == 0);
	}

	REQUIRE(values.shutdown());
}

TEST_CASE("Array pop and delete_swap adjust size with swap-from-back semantics")
{
	MallocAllocator allocator;
	Array<int> values;
	REQUIRE(values.init(&allocator, 8));
	for (int i = 0; i < 5; ++i)
	{
		REQUIRE(values.push_back(i)); // 0 1 2 3 4
	}

	REQUIRE(values.delete_swap(1)); // 0 4 2 3
	CHECK(values.size() == 4u);
	CHECK(values[1] == 4);

	REQUIRE(values.pop()); // 0 4 2
	CHECK(values.size() == 3u);
	CHECK(values.back() == 2);

	REQUIRE(values.delete_swap(2)); // 0 4
	CHECK(values.size() == 2u);
	CHECK(values[0] == 0);
	CHECK(values[1] == 4);

	REQUIRE(values.clear());
	CHECK(values.size() == 0u);

	REQUIRE(values.shutdown());
}

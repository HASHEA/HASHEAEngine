#include "doctest.h"
#include "Base/hserialization.h"

#include <cstdint>
#include <cstring>

using AshEngine::RelativeArray;
using AshEngine::RelativePointer;

namespace
{
	struct PointerBlob
	{
		RelativePointer<uint32_t> pointer;
		uint32_t value;
	};

	struct ArrayBlob
	{
		RelativeArray<uint16_t> array;
		uint16_t storage[3];
	};
}

TEST_CASE("RelativePointer resolves within a blob and survives memcpy relocation")
{
	PointerBlob blob{};
	blob.pointer.set_null();
	CHECK(blob.pointer.is_null());
	CHECK(blob.pointer.get() == nullptr);

	blob.value = 42;
	blob.pointer.set(reinterpret_cast<char*>(&blob.value));
	CHECK(blob.pointer.is_not_null());
	CHECK(blob.pointer.get() == &blob.value);
	CHECK(*blob.pointer == 42u);

	PointerBlob relocated{};
	std::memcpy(&relocated, &blob, sizeof(PointerBlob));
	CHECK(relocated.pointer.get() == &relocated.value);
	relocated.value = 7;
	CHECK(*relocated.pointer == 7u);
	CHECK(*blob.pointer == 42u);
}

TEST_CASE("RelativeArray indexes blob-local storage and survives memcpy relocation")
{
	ArrayBlob blob{};
	blob.array.set(reinterpret_cast<char*>(blob.storage), 3);
	blob.array[0] = 11;
	blob.array[1] = 22;
	blob.array[2] = 33;

	CHECK(blob.array.size == 3u);
	CHECK(blob.array.get() == blob.storage);
	CHECK(blob.storage[1] == 22);

	ArrayBlob relocated{};
	std::memcpy(&relocated, &blob, sizeof(ArrayBlob));
	CHECK(relocated.array.get() == relocated.storage);
	CHECK(relocated.array[2] == 33);

	blob.array.set_empty();
	CHECK(blob.array.size == 0u);
	CHECK(blob.array.data.is_null());
}

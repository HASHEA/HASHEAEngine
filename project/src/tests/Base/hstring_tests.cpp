#include "doctest.h"
#include "Base/hmemory.h"
#include "Base/hstring.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <malloc.h>
#include <unordered_map>

using AshEngine::Allocator;
using AshEngine::StringArray;
using AshEngine::StringBuffer;
using AshEngine::StringView;

namespace
{
	class GuardedAllocator final : public Allocator
	{
	public:
		~GuardedAllocator() override
		{
			for (const auto& [pointer, allocation] : m_allocations)
			{
				_aligned_free(pointer);
			}
		}

		auto allocate(size_t size, size_t alignment) -> void* override
		{
			const size_t normalized_alignment = std::max(alignment, alignof(void*));
			void* pointer = _aligned_malloc(size + k_guard_size, normalized_alignment);
			if (!pointer)
			{
				return nullptr;
			}
			std::memset(static_cast<uint8_t*>(pointer) + size, k_guard_value, k_guard_size);
			m_allocations.emplace(pointer, Allocation{ size });
			return pointer;
		}

		auto allocate(size_t size, size_t alignment, char*, uint32_t) -> void* override
		{
			return allocate(size, alignment);
		}

		auto deallocate(void* pointer, char*, uint32_t) -> bool override
		{
			return deallocate(pointer);
		}

		auto deallocate(void* pointer) -> bool override
		{
			return release(pointer);
		}

		auto deallocate(const void* pointer) -> bool override
		{
			return release(const_cast<void*>(pointer));
		}

		auto deallocate(const void* pointer, char*, uint32_t) -> bool override
		{
			return release(const_cast<void*>(pointer));
		}

		auto guards_intact() const -> bool
		{
			for (const auto& [pointer, allocation] : m_allocations)
			{
				const uint8_t* guard = static_cast<const uint8_t*>(pointer) + allocation.size;
				for (size_t index = 0; index < k_guard_size; ++index)
				{
					if (guard[index] != k_guard_value)
					{
						return false;
					}
				}
			}
			return true;
		}

		auto live_allocation_count() const -> size_t
		{
			return m_allocations.size();
		}

		auto foreign_free_count() const -> size_t
		{
			return m_foreign_free_count;
		}

	private:
		struct Allocation
		{
			size_t size = 0;
		};

		auto release(void* pointer) -> bool
		{
			if (!pointer)
			{
				return true;
			}
			const auto found = m_allocations.find(pointer);
			if (found == m_allocations.end())
			{
				++m_foreign_free_count;
				return false;
			}
			_aligned_free(pointer);
			m_allocations.erase(found);
			return true;
		}

		static constexpr size_t k_guard_size = 32;
		static constexpr uint8_t k_guard_value = 0xa5;
		std::unordered_map<void*, Allocation> m_allocations{};
		size_t m_foreign_free_count = 0;
	};
}

TEST_CASE("StringView::equals compares length then content")
{
	char hello[] = "hello";
	char hello2[] = "hello";
	char other[] = "helpo";

	StringView a{ hello, 5 };
	StringView b{ hello2, 5 };
	StringView c{ other, 5 };
	StringView shorter{ hello, 4 };

	CHECK(StringView::equals(a, b));
	CHECK_FALSE(StringView::equals(a, c));
	CHECK_FALSE(StringView::equals(a, shorter));

	StringView emptyNull{ nullptr, 0 };
	StringView emptyText{ hello, 0 };
	CHECK(StringView::equals(emptyNull, emptyText));
}

TEST_CASE("StringView::copy_to truncates to buffer and null-terminates")
{
	char source[] = "abcdef";
	StringView view{ source, 6 };

	char exact[8] = {};
	StringView::copy_to(view, exact, sizeof(exact));
	CHECK(std::strcmp(exact, "abcdef") == 0);

	char tight[4] = { 'x', 'x', 'x', 'x' };
	StringView::copy_to(view, tight, sizeof(tight));
	CHECK(std::strcmp(tight, "abc") == 0);

	char one[1] = { 'x' };
	StringView::copy_to(view, one, sizeof(one));
	CHECK(one[0] == 0);
}

TEST_CASE("StringBuffer reinitialization releases storage through its owning allocator")
{
	GuardedAllocator first_allocator{};
	GuardedAllocator second_allocator{};
	StringBuffer buffer{};

	buffer.init(8, &first_allocator);
	REQUIRE(buffer.m_pData != nullptr);
	CHECK(first_allocator.live_allocation_count() == 1);

	buffer.init(16, &second_allocator);
	CHECK(first_allocator.foreign_free_count() == 0);
	CHECK(second_allocator.foreign_free_count() == 0);
	CHECK(first_allocator.live_allocation_count() == 0);
	CHECK(second_allocator.live_allocation_count() == 1);

	buffer.shutdown();
	buffer.shutdown();
	CHECK(second_allocator.live_allocation_count() == 0);
}

TEST_CASE("StringBuffer rejects terminator consumption past capacity without changing state")
{
	GuardedAllocator allocator{};
	StringBuffer buffer{};
	buffer.init(4, &allocator);
	REQUIRE(buffer.m_pData != nullptr);

	buffer.append("abcd");
	CHECK(buffer.m_uCurrentSize == 4);
	CHECK(buffer.append_get(StringView{ const_cast<char*>("x"), 1 }) == nullptr);
	CHECK(buffer.m_uCurrentSize == 4);

	buffer.clear();
	buffer.close_current_string();
	buffer.close_current_string();
	buffer.close_current_string();
	buffer.close_current_string();
	CHECK(buffer.m_uCurrentSize == 4);
	buffer.close_current_string();
	CHECK(buffer.m_uCurrentSize == 4);
	CHECK(allocator.guards_intact());
	buffer.shutdown();
}

TEST_CASE("StringBuffer formatted append accepts exact fit and rejects overflow atomically")
{
	GuardedAllocator allocator{};
	StringBuffer buffer{};
	buffer.init(4, &allocator);
	REQUIRE(buffer.m_pData != nullptr);

	buffer.append_f("%s", "abcd");
	CHECK(buffer.m_uCurrentSize == 4);
	CHECK(std::strcmp(buffer.m_pData, "abcd") == 0);

	buffer.clear();
	buffer.append_f("%s", "abcde");
	CHECK(buffer.m_uCurrentSize == 0);
	CHECK(buffer.m_pData[0] == 0);
	CHECK(allocator.guards_intact());
	buffer.shutdown();
}

TEST_CASE("StringArray rejects capacity overflow and releases all owned allocations")
{
	GuardedAllocator allocator{};
	StringArray strings{};
	strings.init(4, &allocator);

	const char* first = strings.intern("abc");
	REQUIRE(first != nullptr);
	CHECK(std::strcmp(first, "abc") == 0);
	CHECK(strings.intern("x") == nullptr);
	CHECK(strings.m_uCurrentSize == 4);
	CHECK(allocator.guards_intact());

	strings.shutdown();
	strings.shutdown();
	CHECK(allocator.live_allocation_count() == 0);
}

TEST_CASE("StringArray deduplicates strings and iterates packed insertion order")
{
	GuardedAllocator allocator{};
	StringArray strings{};
	strings.init(32, &allocator);

	const char* alpha = strings.intern("alpha");
	const char* beta = strings.intern("beta");
	CHECK(strings.intern("alpha") == alpha);
	REQUIRE(alpha != nullptr);
	REQUIRE(beta != nullptr);
	CHECK(strings.get_string_count() == 2);

	auto* iterator = strings.begin_string_iteration();
	REQUIRE(iterator != nullptr);
	REQUIRE(strings.has_next_string(iterator));
	CHECK(std::strcmp(strings.get_next_string(iterator), "alpha") == 0);
	REQUIRE(strings.has_next_string(iterator));
	CHECK(std::strcmp(strings.get_next_string(iterator), "beta") == 0);
	CHECK_FALSE(strings.has_next_string(iterator));

	strings.shutdown();
	CHECK(allocator.live_allocation_count() == 0);
}

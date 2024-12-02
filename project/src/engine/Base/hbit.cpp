#include "hbit.hpp"
#include "hmemory.h"

namespace AshEngine
{
	auto leading_zeros_u32(uint32_t x)->uint32_t
	{
#if defined(_MSC_VER)
		return __lzcnt(x);
#else
		return __builtin_clz(x);
#endif
	}
#if defined(_MSC_VER)
	auto leading_zeros_u32_msvc(uint32_t x) -> uint32_t
	{
		unsigned long result = 0;  // NOLINT(runtime/int)
		if (_BitScanReverse(&result, x)) {
			return 31 - result;
		}
		return 32;
	}
#endif
	auto             trailing_zeros_u32(uint32_t x) -> uint32_t
	{
#if defined(_MSC_VER)
		return _tzcnt_u32(x);
#else
		return __builtin_ctz(x);
#endif
	}
	auto trailing_zeros_u64(uint64_t x) -> uint32_t
	{
#if defined(_MSC_VER)
		return _tzcnt_u64(x);
#else
		return __builtin_ctzl(x);
#endif
	}
	auto round_up_to_power_of_2(uint32_t v) -> uint32_t
	{
		uint32_t nv = 1 << (32 - leading_zeros_u32(v));
		return nv;
	}
	auto print_binary(uint64_t n) -> void
	{
		HLogInfo("0b");
		for (uint32_t i = 0; i < 64; ++i) {
			uint64_t bit = (n >> (64 - i - 1)) & 0x1;
			HLogInfo("{}", bit);
		}
		HLogInfo(" ");
	}
	auto print_binary(uint32_t n) -> void
	{
		HLogInfo("0b");
		for (uint32_t i = 0; i < 32; ++i) {
			uint32_t bit = (n >> (32 - i - 1)) & 0x1;
			HLogInfo("{}", bit);
		}
		HLogInfo(" ");
	}

	// BitSet /////////////////////////////////////////////////////////////////
	void BitSet::init(Allocator* allocator_, uint32_t totalBits) {
		m_pAllocator = allocator_;
		bits = nullptr;
		size = 0;

		resize(totalBits);
	}
	auto BitSet::shutdown() -> void
	{
		Ash_Free(m_pAllocator, bits);

	}
	auto BitSet::resize(uint32_t totalBits) -> void
	{
		uint8_t* oldBits = bits;
		const uint32_t new_size = (totalBits + 7) / 8;
		if (size == new_size) {
			return;
		}
		bits = (uint8_t*)Ash_Alloc(m_pAllocator, new_size, 1);


		if (oldBits) {
			memcpy(bits, oldBits, size);
			Ash_Free(m_pAllocator, oldBits);
		}
		else {
			memset(bits, 0, new_size);
		}

		size = new_size;
	}
};
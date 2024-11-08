#include "hbit.hpp"
#include "hmemory.h"

namespace HASHEAENGINE
{
	auto LeadingZeroesU32(uint32_t x)->uint32_t
	{
#if defined(_MSC_VER)
		return __lzcnt(x);
#else
		return __builtin_clz(x);
#endif
	}
#if defined(_MSC_VER)
	auto LeadingZeroesU32MSVC(uint32_t x) -> uint32_t
	{
		unsigned long result = 0;  // NOLINT(runtime/int)
		if (_BitScanReverse(&result, x)) {
			return 31 - result;
		}
		return 32;
	}
#endif
	auto             TrailingZeroesU32(uint32_t x) -> uint32_t
	{
#if defined(_MSC_VER)
		return _tzcnt_u32(x);
#else
		return __builtin_ctz(x);
#endif
	}
	auto TrailingZeroesU64(uint64_t x) -> uint32_t
	{
#if defined(_MSC_VER)
		return _tzcnt_u64(x);
#else
		return __builtin_ctzl(x);
#endif
	}
	auto RoundUpToPowerOf2(uint32_t v) -> uint32_t
	{
		uint32_t nv = 1 << (32 - LeadingZeroesU32(v));
		return nv;
	}
	auto PrintBinary(uint64_t n) -> void
	{
		HLogInfo("0b");
		for (uint32_t i = 0; i < 64; ++i) {
			uint64_t bit = (n >> (64 - i - 1)) & 0x1;
			HLogInfo("{}", bit);
		}
		HLogInfo(" ");
	}
	auto PrintBinary(uint32_t n) -> void
	{
		HLogInfo("0b");
		for (uint32_t i = 0; i < 32; ++i) {
			uint32_t bit = (n >> (32 - i - 1)) & 0x1;
			HLogInfo("{}", bit);
		}
		HLogInfo(" ");
	}

	// BitSet /////////////////////////////////////////////////////////////////
	void BitSet::Init(Allocator* allocator_, uint32_t totalBits) {
		m_pAllocator = allocator_;
		bits = nullptr;
		size = 0;

		Resize(totalBits);
	}
	auto BitSet::Shutdown() -> void
	{
		Hashea_Free(m_pAllocator, bits);

	}
	auto BitSet::Resize(uint32_t totalBits) -> void
	{
		uint8_t* oldBits = bits;
		const uint32_t new_size = (totalBits + 7) / 8;
		if (size == new_size) {
			return;
		}
		bits = (uint8_t*)Hashea_Alloc(m_pAllocator, new_size, 1);


		if (oldBits) {
			memcpy(bits, oldBits, size);
			Hashea_Free(m_pAllocator, oldBits);
		}
		else {
			memset(bits, 0, new_size);
		}

		size = new_size;
	}
};
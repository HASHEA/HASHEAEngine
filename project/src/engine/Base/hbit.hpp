#pragma once
#include <type_traits>
#include <limits>
#include "hcore.h"
#include "hplatform.h"
#include "hlog.h"
#include "hassert.h"
namespace HASHEAENGINE
{
	struct Allocator;

    // Common methods /////////////////////////////////////////////////////
    auto             LeadingZeroesU32(uint32_t x) -> uint32_t;

#if defined(_MSC_VER)
    auto             LeadingZeroesU32MSVC(uint32_t x) -> uint32_t;
#endif
    auto             TrailingZeroesU32(uint32_t x) -> uint32_t;
    auto             TrailingZeroesU64(uint64_t x) -> uint32_t;

    auto             RoundUpToPowerOf2(uint32_t v) -> uint32_t;

    auto            PrintBinary(uint64_t n) -> void;
    auto            PrintBinary(uint32_t n) -> void;


#if !_HAS_CXX20
    template <typename T>
    constexpr int bit_width(T x) {
        H_ASSERTLOG(std::is_integral<T>::value && std::is_unsigned<T>::value, "bit_width requires an unsigned integral type.");
        if (x == 0) return 0;
#if defined(__GNUC__) || defined(__clang__)
        return std::numeric_limits<T>::digits - __builtin_clz(x);
#elif defined(_MSC_VER)
        unsigned long index;
        _BitScanReverse(&index, x);
        return index + 1;
#else
        // general iml
        int width = 0;
        while (x != 0) {
            x >>= 1;
            ++width;
        }
        return width;
#endif
    }
#endif
    // class BitMask //////////////////////////////////////////////////////

// An abstraction over a bitmask. It provides an easy way to iterate through the
// indexes of the set bits of a bitmask.  When Shift=0 (platforms with SSE),
// this is a true bitmask.  On non-SSE, platforms the arithematic used to
// emulate the SSE behavior works in bytes (Shift=3) and leaves each bytes as
// either 0x00 or 0x80.
//
// For example:
//   for (int i : BitMask<uint32_t, 16>(0x5)) -> yields 0, 2
//   for (int i : BitMask<uint64_t, 8, 3>(0x0000000080800000)) -> yields 2, 3
    template <class T, int SignificantBits, int Shift = 0>
    class BitMask {
    public:
        // These are useful for unit tests (gunit).
        using value_type = int;
        using iterator = BitMask;
        using const_iterator = BitMask;

        explicit BitMask(T mask) : mask_(mask) {
        }
        BitMask& operator++() {
            mask_ &= (mask_ - 1);
            return *this;
        }
        explicit operator bool() const {
            return mask_ != 0;
        }
        int operator*() const {
            return LowestBitSet();
        }
        uint32_t LowestBitSet() const {
            return TrailingZeroesU32(mask_) >> Shift;
        }
        uint32_t HighestBitSet() const {
            return static_cast<uint32_t>((bit_width(mask_) - 1) >> Shift);
        }

        BitMask begin() const {
            return *this;
        }
        BitMask end() const {
            return BitMask(0);
        }

        uint32_t TrailingZeros() const {
            return TrailingZeroesU32(mask_);// >> Shift;
        }

        uint32_t LeadingZeros() const {
            return LeadingZeroesU32(mask_);// >> Shift;
        }

    private:
        friend bool operator==(const BitMask& a, const BitMask& b) {
            return a.mask_ == b.mask_;
        }
        friend bool operator!=(const BitMask& a, const BitMask& b) {
            return a.mask_ != b.mask_;
        }
        T mask_;
    }; // class BitMask
    // Utility methods
    inline uint32_t              BitMask8(uint32_t bit) { return 1 << (bit & 7); }
    inline uint32_t              BitSlot8(uint32_t bit) { return bit / 8; }
    struct BitSet
    {
        auto Init(Allocator* allocator, uint32_t totalBits) -> void;
        auto Shutdown() -> void;
        auto Resize(uint32_t totalBits) -> void;
        auto SetBit(uint32_t index) -> void { bits[index / 8] |= BitMask8(index); };
        auto ClearBit(uint32_t index) -> void { bits[index / 8] &= ~BitMask8(index); };
        auto GetBit(uint32_t index) -> uint8_t { return bits[index / 8] & BitMask8(index); }
        Allocator* m_pAllocator = nullptr;
        uint8_t* bits = nullptr;
        uint32_t                 size = 0;
    };

    template <uint32_t SizeInBytes>
    struct BitSetFixed
    {
        auto SetBit(uint32_t index) -> void { bits[index / 8] |= BitMask8(index); };
        auto ClearBit(uint32_t index) -> void { bits[index / 8] &= ~BitMask8(index); };
        auto GetBit(uint32_t index) -> uint8_t { return bits[index / 8] & BitMask8(index); }

        uint8_t                  bits[SizeInBytes];
    };
};
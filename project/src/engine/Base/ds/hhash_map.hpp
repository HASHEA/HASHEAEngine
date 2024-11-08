#pragma once
#include "Base/hcore.h"
#include "Base/hplatform.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include "Base/hbit.hpp"
#include "wyhash.h"
#include <cstring>
#include <type_traits>  
#include <utility> 
#include <new> 
namespace HASHEAENGINE
{
	static const uint64_t k_iterator_end = UINT64_MAX;
	struct FoundInfo
	{
		uint64_t offset;
		uint64_t probeLength;
	};

	struct FoundResult
	{
		uint64_t index;
		bool bFreeIndex = true;
	};

	struct FlatHashMapIterator
	{
		uint64_t index = 0;
		auto isValid() const -> bool { return index != k_iterator_end; }
		auto isInvalid() const -> bool { return index == k_iterator_end; }
	};

	// A single block of empty control bytes for tables without any slots allocated.
	// This enables removing a branch in the hot path of find().
	auto GroupInitEmpty() -> int8_t*;

	struct ProbeSequence
	{
		static const uint64_t k_width = 16;
		static const size_t k_engine_hash = 0x31d3a36013e;
		ProbeSequence(uint64_t hash, uint64_t mask);
		auto GetOffset() const->uint64_t;
		auto GetOffset(uint64_t) const->uint64_t;
		auto GetIndex() const->uint64_t;
		auto Next() -> void;
		uint64_t mask;
		uint64_t offset;
		uint64_t index = 0;
	};

	template <typename K, typename V>
	class FlatHashMap
	{
	public:
		struct KeyValue
		{
			K key;
			V value;
		};
		auto Init(Allocator* allocator_, uint64_t initial_capacity) -> void;
		auto Shutdown()->void;

		FlatHashMapIterator Find(const K& key);
	
		template <typename K1, typename V1>
		auto Insert(K1&& key, V1&& value)->std::enable_if_t<std::is_base_of_v<std::decay_t<K>, K1>&& std::is_base_of_v<std::decay_t<V>, V1>, HS_Result>
		{
			const FoundResult find_result = _FindOrPrepareInsert(key);
			if (find_result.bFreeIndex) {
				// Construct in place
				slots_[find_result.index].key = std::forward<K1>(key);
				slots_[find_result.index].value = std::forward<V1>(value);
			}
			else {
				// Replace existing value
				HLogError("insert existed key");
				return HS_FAIL;
			}
			return HS_OK;
		}

		template <typename K1, typename V1>
		auto Emplace(K1&& key, V1&& value) -> std::enable_if_t<std::is_base_of_v<std::decay_t<K>,K1> && std::is_base_of_v<std::decay_t<V>,V1>,HS_Result>
		{
			const FoundResult find_result = _FindOrPrepareInsert(key);
			if (find_result.bFreeIndex) {
				new (&slots_[find_result.index]) KeyValue(std::forward<K1>(key), std::forward<V1>(value));
			}
			else {
				slots_[find_result.index].value = std::forward<V1>(value);
			}
			return HS_OK; 
		}

		auto Remove(const K& key) -> uint32_t;
		auto Remove(const FlatHashMapIterator& it) -> uint32_t;
		auto Get(const K& key) -> V&;
		auto Get(const FlatHashMapIterator& it) -> V&;
		auto GetPair(const K& key) -> KeyValue&;
		auto GetPair(const FlatHashMapIterator& it) -> KeyValue&;
		auto SetDefaultValue(const V& value) -> void;

		auto IteratorBegin() -> FlatHashMapIterator;
		auto IteratorAdvance(FlatHashMapIterator& it) -> void;
		auto Size() -> uint64_t { return m_uSize; };
		auto Clear() -> void;
		auto Reserve(uint64_t newSize) -> void;
	private:
		auto _EraseMeta(const FlatHashMapIterator& it) -> void;
		auto _FindOrPrepareInsert(const K& key) -> FoundResult;
		auto _FindFirstNonFull(uint64_t hash) -> FoundInfo;
		auto _PrepareInsert(uint64_t hash) -> uint64_t;
		auto _Probe(uint64_t hash) -> ProbeSequence;
		auto _ReHashAndGrowIfNecessaty() -> void;
		auto _DropDeletesWidthoutResize() -> void;
		auto _CalculateSize(uint64_t newCapacity) -> uint64_t;
		auto _InitializeSlots() -> void;
		auto _Resize(uint64_t newCapacity) -> void;
		auto _IteratorSkipEmptyOrDeleted(FlatHashMapIterator& it) -> void;

		auto _SetCtrl(uint64_t i, int8_t h) -> void;
		auto _ResetCtrl() -> void;
		auto _ResetGrowthLeft() -> void;
	private:

		uint64_t m_uSize = 0;
		uint64_t m_uCapacity = 0;
		int8_t* controlBytes = GroupInitEmpty();
		KeyValue* slots_ = nullptr;
		uint64_t growthLeft = 0;
		Allocator* m_pAllocator = nullptr;
		KeyValue defaultKeyValue = { (K)-1,0 };
	};

	template<typename T>
	inline auto HashCaculate(const T& value, size_t seed = 0) -> uint64_t{
		return wyhash(&value, sizeof(T), seed, _wyp);
	}

	template <size_t N>
	inline auto HashCaculate(const char(&value)[N], size_t seed = 0) -> uint64_t {
		return wyhash(value, strlen(value), seed, _wyp);
	}

	template <>
	inline auto HashCaculate(const cstring& value, size_t seed) -> uint64_t {
		return wyhash(value, strlen(value), seed, _wyp);
	}

	// Method to hash a memory itself.
	inline auto HashBytes(void* data, size_t length, size_t seed = 0) -> uint64_t {
		return wyhash(data, length, seed, _wyp);
	}

// https://gankra.github.io/blah/hashbrown-tldr/
// https://blog.waffles.space/2018/12/07/deep-dive-into-hashbrown/
// https://abseil.io/blog/20180927-swisstables


	// Control byte ///////////////////////////////////////////////////////
	// Following Google's abseil library convetion - based on performance.
	static const int8_t         k_control_bitmask_empty = -128; //0b10000000;
	static const int8_t         k_control_bitmask_deleted = -2;   //0b11111110;
	static const int8_t         k_control_bitmask_sentinel = -1;   //0b11111111;


	static auto             ControlIsEmpty(int8_t control) -> bool{ return control == k_control_bitmask_empty; }
	static auto             ControlIsFull(int8_t control) -> bool { return control >= 0; }
	static auto             ControlIsDeleted(int8_t control) -> bool { return control == k_control_bitmask_deleted; }
	static auto             ControlIsEmptyOrDeleted(int8_t control) -> bool { return control < k_control_bitmask_sentinel; }

	// Hashing ////////////////////////////////////////////////////////////

// Returns a hash seed.
//
// The seed consists of the ctrl_ pointer, which adds enough entropy to ensure
// non-determinism of iteration order in most cases.
// Implementation details: the low bits of the pointer have little or no entropy because of
// alignment. We shift the pointer to try to use higher entropy bits. A
// good number seems to be 12 bits, because that aligns with page size.
	static auto HashSeed(const int8_t* control) -> uint64_t{ return reinterpret_cast<uintptr_t>(control) >> 12; }

	static auto Hash_1(uint64_t hash, const int8_t* ctrl) -> uint64_t { return (hash >> 7) ^ HashSeed(ctrl); }
	static auto Hash_2(uint64_t hash) -> int8_t { return hash & 0x7F; }

	struct GroupSse2Impl
	{
		static constexpr size_t kWidth = 16;  // the number of slots per group
		explicit GroupSse2Impl(const int8_t* pos) {
			ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
		}
		// Returns a bitmask representing the positions of slots that match hash.
		BitMask<uint32_t, kWidth> Match(int8_t hash) const {
			auto match = _mm_set1_epi8(hash);
			return BitMask<uint32_t, kWidth>(
				_mm_movemask_epi8(_mm_cmpeq_epi8(match, ctrl)));
		}
		// Returns a bitmask representing the positions of empty slots.
		BitMask<uint32_t, kWidth> MatchEmpty() const {
#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3
			// This only works because kEmpty is -128.
			return BitMask<uint32_t, kWidth>(
				_mm_movemask_epi8(_mm_sign_epi8(ctrl, ctrl)));
#else
			return Match(static_cast<int8_t>(k_control_bitmask_empty));
#endif
		}
		// Returns a bitmask representing the positions of empty or deleted slots.
		BitMask<uint32_t, kWidth> MatchEmptyOrDeleted() const {
			auto special = _mm_set1_epi8(k_control_bitmask_sentinel);
			return BitMask<uint32_t, kWidth>(
				_mm_movemask_epi8(_mm_cmpgt_epi8(special, ctrl)));
		}
		// Returns the number of trailing empty or deleted elements in the group.
		uint32_t CountLeadingEmptyOrDeleted() const {
			auto special = _mm_set1_epi8(k_control_bitmask_sentinel);
			return TrailingZeroesU32(static_cast<uint32_t>(
				_mm_movemask_epi8(_mm_cmpgt_epi8(special, ctrl)) + 1));
		}
		void ConvertSpecialToEmptyAndFullToDeleted(int8_t* dst) const {
			auto msbs = _mm_set1_epi8(static_cast<char>(-128));
			auto x126 = _mm_set1_epi8(126);
#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3
			auto res = _mm_or_si128(_mm_shuffle_epi8(x126, ctrl), msbs);
#else
			auto zero = _mm_setzero_si128();
			auto special_mask = _mm_cmpgt_epi8(zero, ctrl);
			auto res = _mm_or_si128(msbs, _mm_andnot_si128(special_mask, x126));
#endif
			_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
		}
		__m128i ctrl;
	};

	static auto CapacityIsValid(size_t n) -> bool;
	// Rounds up the capacity to the next power of 2 minus 1, with a minimum of 1.
	static auto CapacityNormalize(uint64_t n) -> uint64_t;

	// General notes on capacity/growth methods below:
	// - We use 7/8th as maximum load factor. For 16-wide groups, that gives an
	//   average of two empty slots per group.
	// - For (capacity+1) >= Group::kWidth, growth is 7/8*capacity.
	// - For (capacity+1) < Group::kWidth, growth == capacity. In this case, we
	//   never need to probe (the whole table fits in one group) so we don't need a
	//   load factor less than 1.
	
	// Given `capacity` of the table, returns the size (i.e. number of full slots)
	// at which we should grow the capacity.
	// if ( Group::kWidth == 8 && capacity == 7 ) { return 6 }
	// x-x/8 does not work when x==7.

	static uint64_t       CapacityToGrowth(uint64_t capacity);
	static uint64_t       CapacityGrowthToLowerBound(uint64_t growth);


	static void ConvertDeletedToEmptyAndFullToDeleted(int8_t* ctrl, size_t capacity) {
		//assert( ctrl[ capacity ] == k_control_bitmask_sentinel );
		//assert( IsValidCapacity( capacity ) );
		for (int8_t* pos = ctrl; pos != ctrl + capacity + 1; pos += GroupSse2Impl::kWidth) {
			GroupSse2Impl{ pos }.ConvertSpecialToEmptyAndFullToDeleted(pos);
		}
		// Copy the cloned ctrl bytes.
		MemoryCopy(ctrl + capacity + 1, ctrl, GroupSse2Impl::kWidth);
		ctrl[capacity] = k_control_bitmask_sentinel;
	}

	// FlatHashMap ////////////////////////////////////////////////////////
	template <typename K, typename V>
	auto FlatHashMap<K, V>::_ResetCtrl() -> void {
		memset(controlBytes, k_control_bitmask_empty, m_uCapacity + GroupSse2Impl::kWidth);
		controlBytes[m_uCapacity] = k_control_bitmask_sentinel;
		//SanitizerPoisonMemoryRegion( slots_, sizeof( slot_type ) * capacity_ );
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_ResetGrowthLeft() ->void {
		growthLeft = CapacityToGrowth(m_uCapacity) - m_uSize;
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_Probe(uint64_t hash) ->ProbeSequence {
		return ProbeSequence(Hash_1(hash, controlBytes), m_uCapacity);
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::Init(Allocator* allocator_, uint64_t initial_capacity) -> void {
		m_pAllocator = allocator_;
		m_uSize = m_uCapacity = growthLeft = 0;
		defaultKeyValue = { (K)-1, (V)0 };

		controlBytes = GroupInitEmpty();
		slots_ = nullptr;
		Reserve(initial_capacity < 4 ? 4 : initial_capacity);
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::Shutdown() -> void {
		Hashea_Free(m_pAllocator, controlBytes);
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::Find(const K& key) -> FlatHashMapIterator {

		const uint64_t hash = HashCaculate(key);
		ProbeSequence sequence = _Probe(hash);

		while (true) {
			const GroupSse2Impl group{ controlBytes + sequence.GetOffset() };
			const int8_t hash2 = Hash_2(hash);
			for (int i : group.Match(hash2)) {
				const KeyValue& key_value = *(slots_ + sequence.GetOffset(i));
				if (key_value.key == key)
					return { sequence.GetOffset(i) };
			}

			if (group.MatchEmpty()) {
				break;
			}

			sequence.Next();
		}

		return { k_iterator_end };
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_EraseMeta(const FlatHashMapIterator& iterator) -> void{
		--m_uSize;
		const uint64_t index = iterator.index;
		const uint64_t index_before = (index - GroupSse2Impl::kWidth) & m_uCapacity;
		const auto empty_after = GroupSse2Impl(controlBytes + index).MatchEmpty();
		const auto empty_before = GroupSse2Impl(controlBytes + index_before).MatchEmpty();

		// We count how many consecutive non empties we have to the right and to the
		// left of `it`. If the sum is >= kWidth then there is at least one probe
		// window that might have seen a full group.
		const uint64_t trailing_zeros = empty_after.TrailingZeros();
		const uint64_t leading_zeros = empty_before.LeadingZeros();
		const uint64_t zeros = trailing_zeros + leading_zeros;
		//printf( "%x, %x", empty_after.TrailingZeros(), empty_before.LeadingZeros() );
		bool was_never_full = empty_before && empty_after;
		was_never_full = was_never_full && (zeros < GroupSse2Impl::kWidth);

		_SetCtrl(index, was_never_full ? k_control_bitmask_empty : k_control_bitmask_deleted);
		growthLeft += was_never_full;
	}


	template <typename K, typename V>
	auto FlatHashMap<K, V>::Remove(const K& key) -> uint32_t{
		FlatHashMapIterator iterator = Find(key);
		if (iterator.index == k_iterator_end)
			return 0;

		_EraseMeta(iterator);
		return 1;
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::Remove(const FlatHashMapIterator& iterator) -> uint32_t {
		if (iterator.index == k_iterator_end)
			return 0;

		_EraseMeta(iterator);
		return 1;
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_FindOrPrepareInsert(const K& key) -> FoundResult{
		uint64_t hash = HashCaculate(key);
		ProbeSequence sequence = _Probe(hash);

		while (true) {
			const GroupSse2Impl group{ controlBytes + sequence.GetOffset() };
			for (int i : group.Match(Hash_2(hash))) {
				const KeyValue& key_value = *(slots_ + sequence.GetOffset(i));
				if (key_value.key == key)
					return { sequence.GetOffset(i), false };
			}

			if (group.MatchEmpty()) {
				break;
			}

			sequence.Next();
		}
		return { _PrepareInsert(hash), true };
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_FindFirstNonFull(uint64_t hash) -> FoundInfo {
		ProbeSequence sequence = _Probe(hash);

		while (true) {
			const GroupSse2Impl group{ controlBytes + sequence.GetOffset() };
			auto mask = group.MatchEmptyOrDeleted();

			if (mask) {
				return { sequence.GetOffset(mask.LowestBitSet()), sequence.GetIndex() };
			}

			sequence.Next();
		}

		return FoundInfo();
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_PrepareInsert(uint64_t hash) -> uint64_t{
		FoundInfo find_info = _FindFirstNonFull(hash);
		if (growthLeft == 0 && !ControlIsDeleted(controlBytes[find_info.offset])) {
			_ReHashAndGrowIfNecessaty();
			find_info = _FindFirstNonFull(hash);
		}
		++size;

		growthLeft -= ControlIsEmpty(controlBytes[find_info.offset]) ? 1 : 0;
		_SetCtrl(find_info.offset, Hash_2(hash));
		return find_info.offset;
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_ReHashAndGrowIfNecessaty() -> void{
		if (m_uCapacity == 0) {
			resize(1);
		}
		else if (m_uSize <= CapacityToGrowth(m_uCapacity) / 2) {
			// Squash DELETED without growing if there is enough capacity.
			_DropDeletesWidthoutResize();
		}
		else {
			// Otherwise grow the container.
			_Resize(m_uCapacity * 2 + 1);
		}
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_DropDeletesWidthoutResize() -> void{
		// Algorithm:
		// - mark all DELETED slots as EMPTY
		// - mark all FULL slots as DELETED
		// - for each slot marked as DELETED
		//     hash = Hash(element)
		//     target = find_first_non_full(hash)
		//     if target is in the same group
		//       mark slot as FULL
		//     else if target is EMPTY
		//       transfer element to target
		//       mark slot as EMPTY
		//       mark target as FULL
		//     else if target is DELETED
		//       swap current element with target element
		//       mark target as FULL
		//       repeat procedure for current slot with moved from element (target)
		//ConvertDeletedToEmptyAndFullToDeleted( control_bytes, capacity );

		alignas(KeyValue) unsigned char raw[sizeof(KeyValue)];
		size_t total_probe_length = 0;
		KeyValue* slot = reinterpret_cast<KeyValue*>(&raw);
		for (size_t i = 0; i != m_uCapacity; ++i) {
			if (!ControlIsDeleted(controlBytes[i])) {
				continue;
			}

			const KeyValue* current_slot = slots_ + i;
			size_t hash = HashCaculate(current_slot->key);
			auto target = _FindFirstNonFull(hash);
			size_t new_i = target.offset;
			total_probe_length += target.probeLength;

			// Verify if the old and new i fall within the same group wrt the hash.
			// If they do, we don't need to move the object as it falls already in the
			// best probe we can.
			const auto probe_index = [&](size_t pos) {
				return ((pos - _Probe(hash).GetOffset()) & m_uCapacity) / GroupSse2Impl::kWidth;
			};

			// Element doesn't move.
			if ((probe_index(new_i) == probe_index(i))) {
				_SetCtrl(i, Hash_2(hash));
				continue;
			}
			if (ControlIsEmpty(controlBytes[new_i])) {
				// Transfer element to the empty spot.
				// set_ctrl poisons/unpoisons the slots so we have to call it at the
				// right time.
				_SetCtrl(new_i, Hash_2(hash));
				memcpy(slots_ + new_i, slots_ + i, sizeof(KeyValue));
				_SetCtrl(i, k_control_bitmask_empty);
			}
			else {
				//assert( control_is_deleted( control_bytes[ new_i ] ) );
				_SetCtrl(new_i, Hash_2(hash));
				// Until we are done rehashing, DELETED marks previously FULL slots.
				// Swap i and new_i elements.
				memcpy(slot, slots_ + i, sizeof(KeyValue));
				memcpy(slots_ + i, slots_ + new_i, sizeof(KeyValue));
				memcpy(slots_ + new_i, slot, sizeof(KeyValue));
				--i;  // repeat
			}
		}
		_ResetGrowthLeft();
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_CalculateSize(uint64_t new_capacity) -> uint64_t{
		return (new_capacity + GroupSse2Impl::kWidth + new_capacity * (sizeof(KeyValue)));
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_InitializeSlots() -> void{

		char* new_memory = (char*)Hashea_Alloc(m_pAllocator,calculate_size(m_uCapacity),1);

		controlBytes = reinterpret_cast<int8_t*>(new_memory);
		slots_ = reinterpret_cast<KeyValue*>(new_memory + m_uCapacity + GroupSse2Impl::kWidth);

		_ResetCtrl();
		_ResetGrowthLeft();
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_Resize(uint64_t new_capacity) -> void{
		//assert( IsValidCapacity( new_capacity ) );
		int8_t* old_control_bytes = controlBytes;
		KeyValue* old_slots = slots_;
		const u64 old_capacity = m_uCapacity;

		m_uCapacity = new_capacity;

		_InitializeSlots();

		size_t total_probe_length = 0;
		for (size_t i = 0; i != old_capacity; ++i) {
			if (ControlIsFull(old_control_bytes[i])) {
				const KeyValue* old_value = old_slots + i;
				uint64_t hash = HashCaculate(old_value->key);

				FoundInfo find_info = _FindFirstNonFull(hash);

				uint64_t new_i = find_info.offset;
				total_probe_length += find_info.probeLength;

				_SetCtrl(new_i, hash_2(hash));

				MemoryCopy(slots_ + new_i, old_slots + i, sizeof(KeyValue));
			}
		}

		if (old_capacity) {
			Hashea_Free(m_pAllocator, old_control_bytes);
		}
	}

	// Sets the control byte, and if `i < Group::kWidth - 1`, set the cloned byte
   // at the end too.
	template <typename K, typename V>
	auto FlatHashMap<K, V>::_SetCtrl(uint64_t i, int8_t h) -> void {
		controlBytes[i] = h;
		constexpr size_t kClonedBytes = GroupSse2Impl::kWidth - 1;
		controlBytes[((i - kClonedBytes) & m_uCapacity) + (kClonedBytes & m_uCapacity)] = h;
	}


	template <typename K, typename V>
	auto FlatHashMap<K, V>::Get(const K& key) ->V& {
		FlatHashMapIterator iterator = Find(key);
		if (iterator.index != k_iterator_end)
			return slots_[iterator.index].value;
		return defaultKeyValue.value;
	}

	template<typename K, typename V>
	auto FlatHashMap<K, V>::Get(const FlatHashMapIterator& iterator) -> V& {
		if (iterator.index != k_iterator_end)
			return slots_[iterator.index].value;
		return defaultKeyValue.value;
	}

	template <typename K, typename V>
	typename FlatHashMap<K, V>::KeyValue& FlatHashMap<K, V>::GetPair(const K& key) {
		FlatHashMapIterator iterator = Find(key);
		if (iterator.index != k_iterator_end)
			return slots_[iterator.index];
		return defaultKeyValue;
	}

	template<typename K, typename V>
	typename FlatHashMap<K, V>::KeyValue& FlatHashMap<K, V>::GetPair(const FlatHashMapIterator& iterator) {
		return slots_[iterator.index];
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::SetDefaultValue(const V& value) -> void{
		defaultKeyValue.value = value;
	}

	template<typename K, typename V>
	auto FlatHashMap<K, V>::IteratorBegin() -> FlatHashMapIterator {
		FlatHashMapIterator it{ 0 };

		_IteratorSkipEmptyOrDeleted(it);

		return it;
	}

	template<typename K, typename V>
	auto FlatHashMap<K, V>::IteratorAdvance(FlatHashMapIterator& iterator) ->void {

		iterator.index++;

		_IteratorSkipEmptyOrDeleted(iterator);
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::_IteratorSkipEmptyOrDeleted(FlatHashMapIterator& it) -> void {
		int8_t* ctrl = controlBytes + it.index;

		while (control_is_empty_or_deleted(*ctrl)) {
			u32 shift = GroupSse2Impl{ ctrl }.CountLeadingEmptyOrDeleted();
			ctrl += shift;
			it.index += shift;
		}
		if (*ctrl == k_control_bitmask_sentinel)
			it.index = k_iterator_end;
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::Clear() -> void{
		m_uSize = 0;
		_ResetCtrl();
		_ResetGrowthLeft();
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::Reserve(uint64_t new_size) -> void{
		if (new_size > m_uSize + growthLeft) {
			size_t m = CapacityGrowthToLowerBound(new_size);
			_Resize(CapacityNormalize(m));
		}
	}

	// Capacity ///////////////////////////////////////////////////////////
	auto CapacityIsValid(size_t n) -> bool { return ((n + 1) & n) == 0 && n > 0; }

	inline auto lzcnt_soft(uint64_t n) -> uint64_t {
		// NOTE(marco): the __lzcnt intrisics require at least haswell
#if defined(_MSC_VER)
		unsigned long index = 0;
		_BitScanReverse64(&index, n);
		uint64_t cnt = index ^ 63;
#else
		uint64_t cnt = __builtin_clzl(n);
#endif
		return cnt;
	}

	// Rounds up the capacity to the next power of 2 minus 1, with a minimum of 1.
	auto CapacityNormalize(uint64_t n) -> uint64_t { return n ? ~uint64_t{} >> lzcnt_soft(n) : 1; }

	auto CapacityToGrowth(uint64_t capacity) -> uint64_t { return capacity - capacity / 8; }

	auto CapacityGrowthToLowerBound(uint64_t growth) -> uint64_t { return growth + static_cast<uint64_t>((static_cast<int64_t>(growth) - 1) / 7); }

	// Grouping: implementation ///////////////////////////////////////////
	inline auto GroupInitEmpty() -> int8_t* {
		alignas(16) static constexpr int8_t empty_group[] = {
			k_control_bitmask_sentinel, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty,
			k_control_bitmask_empty,    k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty, k_control_bitmask_empty };
		return const_cast<int8_t*>(empty_group);
	}

	// Probing: implementation ////////////////////////////////////////////
	inline ProbeSequence::ProbeSequence(uint64_t hash_, uint64_t mask_) {
		//assert( ( ( mask_ + 1 ) & mask_ ) == 0 && "not a mask" );
		mask = mask_;
		offset = hash_ & mask_;
	}

	inline uint64_t ProbeSequence::GetOffset() const {
		return offset;
	}

	inline uint64_t ProbeSequence::GetOffset(uint64_t i) const {
		return (offset + i) & mask;
	}

	inline uint64_t ProbeSequence::GetIndex() const {
		return index;
	}

	inline void ProbeSequence::Next() {
		index += k_width;
		offset += index;
		offset &= mask;
	}
}
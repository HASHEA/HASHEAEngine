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
namespace AshEngine
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
		auto is_valid() const -> bool { return index != k_iterator_end; }
		auto is_invalid() const -> bool { return index == k_iterator_end; }
	};

	// A single block of empty control bytes for tables without any slots allocated.
	// This enables removing a branch in the hot path of find().
	auto group_init_empty() -> int8_t*;

	struct ProbeSequence
	{
		static const uint64_t k_width = 16;
		static const size_t k_engine_hash = 0x31d3a36013e;
		ProbeSequence(uint64_t hash, uint64_t mask);
		auto get_offset() const->uint64_t;
		auto get_offset(uint64_t) const->uint64_t;
		auto get_index() const->uint64_t;
		auto next() -> void;
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
			template <typename K1, typename V1>
			KeyValue(K1&& _key, V1&& _value)
				: key(std::forward<K1>(_key)), 
				value(std::forward<V1>(_value)) {
			}
		};
		auto init(Allocator* allocator_, uint64_t initial_capacity) -> void;
		auto shutdown()->void;

		FlatHashMapIterator find(const K& key);
	
		template <typename K1, typename V1>
		auto insert(K1&& key, V1&& value) -> std::enable_if_t<std::is_same_v<std::decay_t<K>, std::decay_t<K1>> && (std::is_base_of_v<std::decay_t<V>, std::decay_t<V1>> || std::is_same_v<std::decay_t<V>, std::decay_t<V1>>), bool>
		{
			const FoundResult find_result = _find_or_prepare_insert(key);
			if (find_result.bFreeIndex) {
				// Construct in place
				new (&slots_[find_result.index]) KeyValue(std::forward<K1>(key), std::forward<V1>(value));
			}
			else {
				// Replace existing value
				HLogError("insert existed key");
				return false;
			}
			return true;
		}

		template <typename K1, typename V1>
		auto emplace(K1&& key, V1&& value) -> std::enable_if_t<std::is_same_v<std::decay_t<K>, std::decay_t<K1>> && (std::is_base_of_v<std::decay_t<V>, std::decay_t<V1>> || std::is_same_v<std::decay_t<V>, std::decay_t<V1>>), bool>
		{
			const FoundResult find_result = _find_or_prepare_insert(key);
			if (find_result.bFreeIndex) {
				new (&slots_[find_result.index]) KeyValue(std::forward<K1>(key), std::forward<V1>(value));
			}
			else {
				slots_[find_result.index].value = std::forward<V1>(value);
			}
			return true; 
		}

		auto remove(const K& key) -> uint32_t;
		auto remove(const FlatHashMapIterator& it) -> uint32_t;
		auto get(const K& key) -> V&;
		auto get(const FlatHashMapIterator& it) -> V&;
		auto get_pair(const K& key) -> KeyValue&;
		auto get_pair(const FlatHashMapIterator& it) -> KeyValue&;
		auto set_default_value(const V& value) -> void;

		auto iterator_begin() -> FlatHashMapIterator;
		auto iterator_advance(FlatHashMapIterator& it) -> void;
		auto size() -> uint64_t { return m_uSize; };
		auto clear() -> void;
		auto reserve(uint64_t newSize) -> void;
	private:
		auto _erase_meta(const FlatHashMapIterator& it) -> void;
		auto _find_or_prepare_insert(const K& key) -> FoundResult;
		auto _find_first_non_full(uint64_t hash) -> FoundInfo;
		auto _prepare_insert(uint64_t hash) -> uint64_t;
		auto _probe(uint64_t hash) -> ProbeSequence;
		auto _rehash_and_grow_if_necessary() -> void;
		auto _drop_deletes_widthout_resize() -> void;
		auto _calculate_size(uint64_t newCapacity) -> uint64_t;
		auto _initialize_slots() -> void;
		auto _resize(uint64_t newCapacity) -> void;
		auto _iterator_skip_empty_or_deleted(FlatHashMapIterator& it) -> void;

		auto _set_ctrl(uint64_t i, int8_t h) -> void;
		auto _reset_ctrl() -> void;
		auto _reset_grow_left() -> void;
	private:

		uint64_t m_uSize = 0;
		uint64_t m_uCapacity = 0;
		int8_t* controlBytes = group_init_empty();
		KeyValue* slots_ = nullptr;
		uint64_t growthLeft = 0;
		Allocator* m_pAllocator = nullptr;
		KeyValue defaultKeyValue = { K(),V()};
	};

	template<typename T>
	inline auto hash_calculate(const T& value, size_t seed = 0) -> uint64_t{
		return wyhash(&value, sizeof(T), seed, _wyp);
	}

	template <size_t N>
	inline auto hash_calculate(const char(&value)[N], size_t seed = 0) -> uint64_t {
		return wyhash(value, strlen(value), seed, _wyp);
	}

	template <>
	inline auto hash_calculate(const cstring& value, size_t seed) -> uint64_t {
		return wyhash(value, strlen(value), seed, _wyp);
	}

	// Method to hash a memory itself.
	inline auto hash_bytes(void* data, size_t length, size_t seed = 0) -> uint64_t {
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


	static auto             control_is_empty(int8_t control) -> bool{ return control == k_control_bitmask_empty; }
	static auto             control_is_full(int8_t control) -> bool { return control >= 0; }
	static auto             control_is_deleted(int8_t control) -> bool { return control == k_control_bitmask_deleted; }
	static auto             control_is_empty_or_deleted(int8_t control) -> bool { return control < k_control_bitmask_sentinel; }

	// Hashing ////////////////////////////////////////////////////////////

// Returns a hash seed.
//
// The seed consists of the ctrl_ pointer, which adds enough entropy to ensure
// non-determinism of iteration order in most cases.
// Implementation details: the low bits of the pointer have little or no entropy because of
// alignment. We shift the pointer to try to use higher entropy bits. A
// good number seems to be 12 bits, because that aligns with page size.
	static auto hash_seed(const int8_t* control) -> uint64_t{ return reinterpret_cast<uintptr_t>(control) >> 12; }

	static auto hash_1(uint64_t hash, const int8_t* ctrl) -> uint64_t { return (hash >> 7) ^ hash_seed(ctrl); }
	static auto hash_2(uint64_t hash) -> int8_t { return hash & 0x7F; }

	struct GroupSse2Impl
	{
		static constexpr size_t kWidth = 16;  // the number of slots per group
		explicit GroupSse2Impl(const int8_t* pos) {
			ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
		}
		// Returns a bitmask representing the positions of slots that match hash.
		BitMask<uint32_t, kWidth> match(int8_t hash) const {
			auto match = _mm_set1_epi8(hash);
			return BitMask<uint32_t, kWidth>(
				_mm_movemask_epi8(_mm_cmpeq_epi8(match, ctrl)));
		}
		// Returns a bitmask representing the positions of empty slots.
		BitMask<uint32_t, kWidth> match_empty() const {
#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3
			// This only works because kEmpty is -128.
			return BitMask<uint32_t, kWidth>(
				_mm_movemask_epi8(_mm_sign_epi8(ctrl, ctrl)));
#else
			return match(static_cast<int8_t>(k_control_bitmask_empty));
#endif
		}
		// Returns a bitmask representing the positions of empty or deleted slots.
		BitMask<uint32_t, kWidth> match_empty_or_deleted() const {
			auto special = _mm_set1_epi8(k_control_bitmask_sentinel);
			return BitMask<uint32_t, kWidth>(
				_mm_movemask_epi8(_mm_cmpgt_epi8(special, ctrl)));
		}
		// Returns the number of trailing empty or deleted elements in the group.
		uint32_t count_leading_empty_or_deleted() const {
			auto special = _mm_set1_epi8(k_control_bitmask_sentinel);
			return trailing_zeros_u32(static_cast<uint32_t>(
				_mm_movemask_epi8(_mm_cmpgt_epi8(special, ctrl)) + 1));
		}
		void convert_special_to_empty_and_full_to_deleted(int8_t* dst) const {
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

	static auto capacity_is_valid(size_t n) -> bool;
	// Rounds up the capacity to the next power of 2 minus 1, with a minimum of 1.
	static auto capacity_normalize(uint64_t n) -> uint64_t;

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

	static uint64_t       capacity_to_growth(uint64_t capacity);
	static uint64_t       capacity_growth_to_lower_bound(uint64_t growth);


	static void convert_deleted_to_empty_and_full_to_deleted(int8_t* ctrl, size_t capacity) {
		//assert( ctrl[ capacity ] == k_control_bitmask_sentinel );
		//assert( IsValidCapacity( capacity ) );
		for (int8_t* pos = ctrl; pos != ctrl + capacity + 1; pos += GroupSse2Impl::kWidth) {
			GroupSse2Impl{ pos }.convert_special_to_empty_and_full_to_deleted(pos);
		}
		// Copy the cloned ctrl bytes.
		memory_copy(ctrl + capacity + 1, ctrl, GroupSse2Impl::kWidth);
		ctrl[capacity] = k_control_bitmask_sentinel;
	}

	// FlatHashMap ////////////////////////////////////////////////////////
	template <typename K, typename V>
	auto FlatHashMap<K, V>::_reset_ctrl() -> void {
		memset(controlBytes, k_control_bitmask_empty, m_uCapacity + GroupSse2Impl::kWidth);
		controlBytes[m_uCapacity] = k_control_bitmask_sentinel;
		//SanitizerPoisonMemoryRegion( slots_, sizeof( slot_type ) * capacity_ );
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_reset_grow_left() ->void {
		growthLeft = capacity_to_growth(m_uCapacity) - m_uSize;
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_probe(uint64_t hash) ->ProbeSequence {
		return ProbeSequence(hash_1(hash, controlBytes), m_uCapacity);
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::init(Allocator* allocator_, uint64_t initial_capacity) -> void {
		m_pAllocator = allocator_;
		m_uSize = m_uCapacity = growthLeft = 0;
		defaultKeyValue = { K(), V()};

		controlBytes = group_init_empty();
		slots_ = nullptr;
		reserve(initial_capacity < 4 ? 4 : initial_capacity);
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::shutdown() -> void {
		Ash_Free(m_pAllocator, controlBytes);
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::find(const K& key) -> FlatHashMapIterator {

		const uint64_t hash = hash_calculate(key);
		ProbeSequence sequence = _probe(hash);

		while (true) {
			const GroupSse2Impl group{ controlBytes + sequence.get_offset() };
			const int8_t hash2 = hash_2(hash);
			for (int i : group.match(hash2)) {
				const KeyValue& key_value = *(slots_ + sequence.get_offset(i));
				if (key_value.key == key)
					return { sequence.get_offset(i) };
			}

			if (group.match_empty()) {
				break;
			}

			sequence.next();
		}

		return { k_iterator_end };
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_erase_meta(const FlatHashMapIterator& iterator) -> void{
		--m_uSize;
		const uint64_t index = iterator.index;
		const uint64_t index_before = (index - GroupSse2Impl::kWidth) & m_uCapacity;
		const auto empty_after = GroupSse2Impl(controlBytes + index).match_empty();
		const auto empty_before = GroupSse2Impl(controlBytes + index_before).match_empty();

		// We count how many consecutive non empties we have to the right and to the
		// left of `it`. If the sum is >= kWidth then there is at least one probe
		// window that might have seen a full group.
		const uint64_t trailing_zeros = empty_after.trailing_zeros();
		const uint64_t leading_zeros = empty_before.leading_zeros();
		const uint64_t zeros = trailing_zeros + leading_zeros;
		//printf( "%x, %x", empty_after.TrailingZeros(), empty_before.LeadingZeros() );
		bool was_never_full = empty_before && empty_after;
		was_never_full = was_never_full && (zeros < GroupSse2Impl::kWidth);

		_set_ctrl(index, was_never_full ? k_control_bitmask_empty : k_control_bitmask_deleted);
		growthLeft += was_never_full;
	}


	template <typename K, typename V>
	auto FlatHashMap<K, V>::remove(const K& key) -> uint32_t{
		FlatHashMapIterator iterator = find(key);
		if (iterator.index == k_iterator_end)
			return 0;

		_erase_meta(iterator);
		return 1;
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::remove(const FlatHashMapIterator& iterator) -> uint32_t {
		if (iterator.index == k_iterator_end)
			return 0;

		_erase_meta(iterator);
		return 1;
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_find_or_prepare_insert(const K& key) -> FoundResult{
		uint64_t hash = hash_calculate(key);
		ProbeSequence sequence = _probe(hash);

		while (true) {
			const GroupSse2Impl group{ controlBytes + sequence.get_offset() };
			for (int i : group.match(hash_2(hash))) {
				const KeyValue& key_value = *(slots_ + sequence.get_offset(i));
				if (key_value.key == key)
					return { sequence.get_offset(i), false };
			}

			if (group.match_empty()) {
				break;
			}

			sequence.next();
		}
		return { _prepare_insert(hash), true };
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_find_first_non_full(uint64_t hash) -> FoundInfo {
		ProbeSequence sequence = _probe(hash);

		while (true) {
			const GroupSse2Impl group{ controlBytes + sequence.get_offset() };
			auto mask = group.match_empty_or_deleted();

			if (mask) {
				return { sequence.get_offset(mask.lowest_bitset()), sequence.get_index() };
			}

			sequence.next();
		}

		return FoundInfo();
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_prepare_insert(uint64_t hash) -> uint64_t{
		FoundInfo find_info = _find_first_non_full(hash);
		if (growthLeft == 0 && !control_is_deleted(controlBytes[find_info.offset])) {
			_rehash_and_grow_if_necessary();
			find_info = _find_first_non_full(hash);
		}
		++m_uSize;

		growthLeft -= control_is_empty(controlBytes[find_info.offset]) ? 1 : 0;
		_set_ctrl(find_info.offset, hash_2(hash));
		return find_info.offset;
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_rehash_and_grow_if_necessary() -> void{
		if (m_uCapacity == 0) {
			_resize(1);
		}
		else if (m_uSize <= capacity_to_growth(m_uCapacity) / 2) {
			// Squash DELETED without growing if there is enough capacity.
			_drop_deletes_widthout_resize();
		}
		else {
			// Otherwise grow the container.
			_resize(m_uCapacity * 2 + 1);
		}
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_drop_deletes_widthout_resize() -> void{
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
			if (!control_is_deleted(controlBytes[i])) {
				continue;
			}

			const KeyValue* current_slot = slots_ + i;
			size_t hash = hash_calculate(current_slot->key);
			auto target = _find_first_non_full(hash);
			size_t new_i = target.offset;
			total_probe_length += target.probeLength;

			// Verify if the old and new i fall within the same group wrt the hash.
			// If they do, we don't need to move the object as it falls already in the
			// best probe we can.
			const auto probe_index = [&](size_t pos) {
				return ((pos - _probe(hash).get_offset()) & m_uCapacity) / GroupSse2Impl::kWidth;
			};

			// Element doesn't move.
			if ((probe_index(new_i) == probe_index(i))) {
				_set_ctrl(i, hash_2(hash));
				continue;
			}
			if (control_is_empty(controlBytes[new_i])) {
				// Transfer element to the empty spot.
				// set_ctrl poisons/unpoisons the slots so we have to call it at the
				// right time.
				_set_ctrl(new_i, hash_2(hash));
				memcpy(slots_ + new_i, slots_ + i, sizeof(KeyValue));
				_set_ctrl(i, k_control_bitmask_empty);
			}
			else {
				//assert( control_is_deleted( control_bytes[ new_i ] ) );
				_set_ctrl(new_i, hash_2(hash));
				// Until we are done rehashing, DELETED marks previously FULL slots.
				// Swap i and new_i elements.
				memcpy(slot, slots_ + i, sizeof(KeyValue));
				memcpy(slots_ + i, slots_ + new_i, sizeof(KeyValue));
				memcpy(slots_ + new_i, slot, sizeof(KeyValue));
				--i;  // repeat
			}
		}
		_reset_grow_left();
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_calculate_size(uint64_t new_capacity) -> uint64_t{
		return (new_capacity + GroupSse2Impl::kWidth + new_capacity * (sizeof(KeyValue)));
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_initialize_slots() -> void{

		char* new_memory = (char*)Ash_Alloc(m_pAllocator,_calculate_size(m_uCapacity),1);

		controlBytes = reinterpret_cast<int8_t*>(new_memory);
		slots_ = reinterpret_cast<KeyValue*>(new_memory + m_uCapacity + GroupSse2Impl::kWidth);

		_reset_ctrl();
		_reset_grow_left();
	}

	template <typename K, typename V>
	auto FlatHashMap<K, V>::_resize(uint64_t new_capacity) -> void{
		//assert( IsValidCapacity( new_capacity ) );
		int8_t* old_control_bytes = controlBytes;
		KeyValue* old_slots = slots_;
		const uint64_t old_capacity = m_uCapacity;

		m_uCapacity = new_capacity;

		_initialize_slots();

		size_t total_probe_length = 0;
		for (size_t i = 0; i != old_capacity; ++i) {
			if (control_is_full(old_control_bytes[i])) {
				const KeyValue* old_value = old_slots + i;
				uint64_t hash = hash_calculate(old_value->key);

				FoundInfo find_info = _find_first_non_full(hash);

				uint64_t new_i = find_info.offset;
				total_probe_length += find_info.probeLength;

				_set_ctrl(new_i, hash_2(hash));

				memory_copy(slots_ + new_i, old_slots + i, sizeof(KeyValue));
			}
		}

		if (old_capacity) {
			Ash_Free(m_pAllocator, old_control_bytes);
		}
	}

	// Sets the control byte, and if `i < Group::kWidth - 1`, set the cloned byte
   // at the end too.
	template <typename K, typename V>
	auto FlatHashMap<K, V>::_set_ctrl(uint64_t i, int8_t h) -> void {
		controlBytes[i] = h;
		constexpr size_t kClonedBytes = GroupSse2Impl::kWidth - 1;
		controlBytes[((i - kClonedBytes) & m_uCapacity) + (kClonedBytes & m_uCapacity)] = h;
	}


	template <typename K, typename V>
	auto FlatHashMap<K, V>::get(const K& key) ->V& {
		FlatHashMapIterator iterator = find(key);
		if (iterator.index != k_iterator_end)
			return slots_[iterator.index].value;
		return defaultKeyValue.value;
	}

	template<typename K, typename V>
	auto FlatHashMap<K, V>::get(const FlatHashMapIterator& iterator) -> V& {
		if (iterator.index != k_iterator_end)
			return slots_[iterator.index].value;
		return defaultKeyValue.value;
	}

	template <typename K, typename V>
	typename FlatHashMap<K, V>::KeyValue& FlatHashMap<K, V>::get_pair(const K& key) {
		FlatHashMapIterator iterator = find(key);
		if (iterator.index != k_iterator_end)
			return slots_[iterator.index];
		return defaultKeyValue;
	}

	template<typename K, typename V>
	typename FlatHashMap<K, V>::KeyValue& FlatHashMap<K, V>::get_pair(const FlatHashMapIterator& iterator) {
		return slots_[iterator.index];
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::set_default_value(const V& value) -> void{
		defaultKeyValue.value = value;
	}

	template<typename K, typename V>
	auto FlatHashMap<K, V>::iterator_begin() -> FlatHashMapIterator {
		FlatHashMapIterator it{ 0 };

		_iterator_skip_empty_or_deleted(it);

		return it;
	}

	template<typename K, typename V>
	auto FlatHashMap<K, V>::iterator_advance(FlatHashMapIterator& iterator) ->void {

		iterator.index++;

		_iterator_skip_empty_or_deleted(iterator);
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::_iterator_skip_empty_or_deleted(FlatHashMapIterator& it) -> void {
		int8_t* ctrl = controlBytes + it.index;

		while (control_is_empty_or_deleted(*ctrl)) {
			uint32_t shift = GroupSse2Impl{ ctrl }.count_leading_empty_or_deleted();
			ctrl += shift;
			it.index += shift;
		}
		if (*ctrl == k_control_bitmask_sentinel)
			it.index = k_iterator_end;
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::clear() -> void{
		m_uSize = 0;
		_reset_ctrl();
		_reset_grow_left();
	}

	template<typename K, typename V>
	inline auto FlatHashMap<K, V>::reserve(uint64_t new_size) -> void{
		if (new_size > m_uSize + growthLeft) {
			size_t m = capacity_growth_to_lower_bound(new_size);
			_resize(capacity_normalize(m));
		}
	}

	// Capacity ///////////////////////////////////////////////////////////
	auto capacity_is_valid(size_t n) -> bool { return ((n + 1) & n) == 0 && n > 0; }

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
	auto capacity_normalize(uint64_t n) -> uint64_t { return n ? ~uint64_t{} >> lzcnt_soft(n) : 1; }

	auto capacity_to_growth(uint64_t capacity) -> uint64_t { return capacity - capacity / 8; }

	auto capacity_growth_to_lower_bound(uint64_t growth) -> uint64_t { return growth + static_cast<uint64_t>((static_cast<int64_t>(growth) - 1) / 7); }

	// Grouping: implementation ///////////////////////////////////////////
	inline auto group_init_empty() -> int8_t* {
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

	inline uint64_t ProbeSequence::get_offset() const {
		return offset;
	}

	inline uint64_t ProbeSequence::get_offset(uint64_t i) const {
		return (offset + i) & mask;
	}

	inline uint64_t ProbeSequence::get_index() const {
		return index;
	}

	inline void ProbeSequence::next() {
		index += k_width;
		offset += index;
		offset &= mask;
	}
}
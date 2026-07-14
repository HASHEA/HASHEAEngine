#include "hstring.h"
#include "hmemory.h"
#include "ds/hhash_map.hpp"
#include "hassert.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
namespace AshEngine
{
	namespace
	{
		auto has_valid_storage(const StringBuffer& buffer) -> bool
		{
			return buffer.m_pData && buffer.m_uCurrentSize <= buffer.m_uBufferSize;
		}

		auto remaining_capacity(const StringBuffer& buffer) -> size_t
		{
			if (!has_valid_storage(buffer))
			{
				return 0;
			}
			return static_cast<size_t>(buffer.m_uBufferSize - buffer.m_uCurrentSize);
		}

		auto formatted_length(const char* format, va_list args) -> int
		{
			va_list measurement_args;
			va_copy(measurement_args, args);
#if defined(_MSC_VER)
			const int length = _vscprintf(format, measurement_args);
#else
			const int length = std::vsnprintf(nullptr, 0, format, measurement_args);
#endif
			va_end(measurement_args);
			return length;
		}

		auto append_formatted(StringBuffer& buffer, const char* format, va_list args, bool consume_terminator) -> char*
		{
			if (!has_valid_storage(buffer) || !format)
			{
				HLogError("StringBuffer formatted append requires initialized storage and a format string.");
				return nullptr;
			}

			const int required_characters = formatted_length(format, args);
			if (required_characters < 0)
			{
				HLogError("StringBuffer failed to measure formatted text.");
				return nullptr;
			}

			const size_t content_size = static_cast<size_t>(required_characters);
			const size_t consumed_size = content_size + (consume_terminator ? 1u : 0u);
			if (consumed_size < content_size || consumed_size > remaining_capacity(buffer))
			{
				HLogError("StringBuffer formatted append exceeds remaining capacity.");
				return nullptr;
			}

			char* const result = buffer.m_pData + buffer.m_uCurrentSize;
			va_list write_args;
			va_copy(write_args, args);
			const int written = std::vsnprintf(result, content_size + 1u, format, write_args);
			va_end(write_args);
			if (written != required_characters)
			{
				result[0] = 0;
				HLogError("StringBuffer formatted append wrote an unexpected number of characters.");
				return nullptr;
			}

			buffer.m_uCurrentSize += static_cast<uint32_t>(consumed_size);
			buffer.m_pData[buffer.m_uCurrentSize] = 0;
			return result;
		}

		constexpr auto align_up(size_t value, size_t alignment) -> size_t
		{
			return (value + alignment - 1u) & ~(alignment - 1u);
		}
	}


	bool AshEngine::StringView::equals(const StringView& a, const StringView& b)
	{
		if (a.length != b.length)
		{
			return false;
		}
		if (a.length == 0)
		{
			return true;
		}
		if (a.text == b.text)
		{
			return true;
		}
		if (a.text == nullptr || b.text == nullptr)
		{
			return false;
		}
		return memcmp(a.text, b.text, a.length) == 0;
	}

	void StringView::copy_to(const StringView& a, char* buffer, size_t bufferSize)
	{
		if (buffer == nullptr || bufferSize == 0)
		{
			return;
		}
		const size_t copyLength = a.length < (bufferSize - 1) ? a.length : (bufferSize - 1);
		if (copyLength > 0 && a.text != nullptr)
		{
			memcpy(buffer, a.text, copyLength);
		}
		buffer[copyLength] = 0;
	}	 
		 
	auto StringBuffer::init(size_t size, Allocator* allocator) -> void
	{	 
		shutdown();
		if (size < 1 || size > std::numeric_limits<uint32_t>::max())
		{
			HLogError("StringBuffer capacity must be in [1, UINT32_MAX].");
			return;
		}
		m_pAllocator = allocator;
		m_pData = (char*) Ash_Alloc(m_pAllocator,size + 1,1);
		if (!m_pData)
		{
			HLogError("StringBuffer failed to allocate {} bytes.", size + 1u);
			m_pAllocator = nullptr;
			return;
		}
		memset(m_pData, 0, size + 1);
		m_uBufferSize = (uint32_t)size;
		m_uCurrentSize = 0;
	}	 
		 
	auto StringBuffer::shutdown() -> void
	{	 
		if (m_pData)
		{
			Ash_Free(m_pAllocator,m_pData);
		}
		m_pData = nullptr;
		m_uBufferSize = m_uCurrentSize = 0;
		m_pAllocator = nullptr;
	}	 
		 
	auto StringBuffer::append(const char* string) -> void
	{	 
		if (!string)
		{
			HLogError("StringBuffer cannot append a null string.");
			return;
		}
		StringView text{ const_cast<char*>(string), std::strlen(string) };
		append(text);
	}	 
		 
	auto StringBuffer::append(const StringView& text) -> void
	{	 
		if (!has_valid_storage(*this) || (text.length > 0 && !text.text))
		{
			HLogError("StringBuffer append requires initialized storage and valid text.");
			return;
		}
		if (text.length > remaining_capacity(*this))
		{
			HLogError("StringBuffer append exceeds remaining capacity.");
			return;
		}
		if (text.length > 0)
		{
			memcpy(&m_pData[m_uCurrentSize], text.text, text.length);
		}
		m_uCurrentSize += static_cast<uint32_t>(text.length);
		m_pData[m_uCurrentSize] = 0;
	}	 
		 
	auto StringBuffer::append_m(void* memory, size_t size) -> void
	{	 
		if (!has_valid_storage(*this) || (size > 0 && !memory))
		{
			HLogError("StringBuffer binary append requires initialized storage and valid memory.");
			return;
		}
		if (size > remaining_capacity(*this))
		{
			HLogError("StringBuffer binary append exceeds remaining capacity.");
			return;
		}
		if (size > 0)
		{
			memcpy(&m_pData[m_uCurrentSize], memory, size);
		}
		m_uCurrentSize += static_cast<uint32_t>(size);
		m_pData[m_uCurrentSize] = 0;
	}	 
		 
	auto StringBuffer::append(const StringBuffer& otherBuffer) -> void
	{	 
		if (otherBuffer.m_uCurrentSize == 0)
		{
			return;
		}
		append_m(otherBuffer.m_pData, otherBuffer.m_uCurrentSize);
	}	 
		 
	auto StringBuffer::append_f(const char* format, ...) -> void
	{	 
		va_list args;
		va_start(args,format);
		append_formatted(*this, format, args, false);
		va_end(args);
	}	 
		 
	auto StringBuffer::append_get(const char* string) -> char*
	{	 
		if (!string)
		{
			return nullptr;
		}
		return append_get(StringView{ const_cast<char*>(string), std::strlen(string) });
	}	 
		 
	auto StringBuffer::append_get_f(const char* format, ...) -> char*
	{	 
		va_list args;
		va_start(args, format);
		char* result = append_formatted(*this, format, args, true);
		va_end(args);
		return result;
	}	 
		 
	auto StringBuffer::append_get(const StringView& text) -> char*
	{	 
		if (!has_valid_storage(*this) || (text.length > 0 && !text.text))
		{
			return nullptr;
		}
		if (text.length == std::numeric_limits<size_t>::max() || text.length + 1u > remaining_capacity(*this))
		{
			HLogError("StringBuffer packed append exceeds remaining capacity.");
			return nullptr;
		}
		char* const result = m_pData + m_uCurrentSize;
		if (text.length > 0)
		{
			memcpy(result, text.text, text.length);
		}
		result[text.length] = 0;
		m_uCurrentSize += static_cast<uint32_t>(text.length + 1u);
		m_pData[m_uCurrentSize] = 0;
		return result;
	}	 
		 
	auto StringBuffer::append_get_substring(const char* string, uint32_t start_index, uint32_t end_index) -> char*
	{	 
		if (!string || start_index > end_index || end_index > std::strlen(string))
		{
			HLogError("StringBuffer substring range is invalid.");
			return nullptr;
		}
		return append_get(StringView{ const_cast<char*>(string + start_index), end_index - start_index });
	}	 
		 
	auto StringBuffer::close_current_string() -> void
	{	 
		if (!m_pData || remaining_capacity(*this) < 1u)
		{
			HLogError("StringBuffer cannot close a string past capacity.");
			return;
		}
		m_pData[m_uCurrentSize] = 0;
		++m_uCurrentSize;
		m_pData[m_uCurrentSize] = 0;
	}	 
		 
	auto StringBuffer::get_index(const char* text) const -> uint32_t
	{	 
		if (!text || !m_pData || m_uCurrentSize == 0 || m_uCurrentSize > m_uBufferSize)
		{
			return UINT32_MAX;
		}
		const uintptr_t address = reinterpret_cast<uintptr_t>(text);
		const uintptr_t begin = reinterpret_cast<uintptr_t>(m_pData);
		const uintptr_t end = begin + m_uCurrentSize;
		return address >= begin && address < end ? static_cast<uint32_t>(address - begin) : UINT32_MAX;
	}	 
		 
	auto StringBuffer::get_text(uint32_t index) const -> const char*
	{	 
		//why buffer size? not current size ?
		return m_pData && m_uCurrentSize <= m_uBufferSize && index < m_uCurrentSize
			? cstring(m_pData + index)
			: nullptr;

	}	 
		 
	auto StringBuffer::reserve(size_t size) -> char*
	{	 
		if (!has_valid_storage(*this) || size > remaining_capacity(*this))
			return nullptr;

		uint32_t offset = m_uCurrentSize;
		m_uCurrentSize += static_cast<uint32_t>(size);
		m_pData[m_uCurrentSize] = 0;

		return m_pData + offset;
	}	 
		 
	auto StringBuffer::clear() -> void
	{	 
		m_uCurrentSize = 0;
		if (m_pData)
		{
			m_pData[0] = 0;
		}
	}	 
		 
	auto StringArray::init(uint32_t size, Allocator* allocator) -> void
	{	 
		shutdown();
		if (size == 0)
		{
			HLogError("StringArray capacity must be greater than zero.");
			return;
		}

		using StringIndexMap = FlatHashMap<uint64_t, uint32_t>;
		constexpr size_t allocation_alignment = std::max(alignof(StringIndexMap), alignof(FlatHashMapIterator));
		constexpr size_t iterator_offset = align_up(sizeof(StringIndexMap), alignof(FlatHashMapIterator));
		constexpr size_t data_offset = iterator_offset + sizeof(FlatHashMapIterator);
		if (size > std::numeric_limits<size_t>::max() - data_offset)
		{
			HLogError("StringArray allocation size overflow.");
			return;
		}

		m_pAllocator = allocator;
		void* allocation = Ash_Alloc(m_pAllocator, data_offset + size, allocation_alignment);
		char* allocated_memory = static_cast<char*>(allocation);
		if (!allocated_memory)
		{
			HLogError("StringArray failed to allocate {} bytes.", data_offset + size);
			m_pAllocator = nullptr;
			return;
		}

		string2Index = new (allocated_memory) StringIndexMap();
		stringsIterator = new (allocated_memory + iterator_offset) FlatHashMapIterator();
		m_pData = allocated_memory + data_offset;
		string2Index->init(allocator, 8);
		string2Index->set_default_value(UINT32_MAX);
		m_uBufferSize = size;
		m_uCurrentSize = 0;
		m_uStringCount = 0;
	}	 
		 
	auto StringArray::shutdown() -> void
	{	 
		if (stringsIterator)
		{
			stringsIterator->~FlatHashMapIterator();
		}
		if (string2Index)
		{
			using StringIndexMap = FlatHashMap<uint64_t, uint32_t>;
			string2Index->~StringIndexMap();
			Ash_Free(m_pAllocator, string2Index);
		}
		string2Index = nullptr;
		stringsIterator = nullptr;
		m_pData = nullptr;
		m_uBufferSize = m_uCurrentSize = 0;
		m_uStringCount = 0;
		m_pAllocator = nullptr;
	}	 
		 
	auto StringArray::clear() -> void
	{	 
		m_uCurrentSize = 0;
		m_uStringCount = 0;
		if (m_pData && m_uBufferSize > 0)
		{
			m_pData[0] = 0;
		}
		if (string2Index)
		{
			string2Index->clear();
		}
	}	 
		 
	auto StringArray::begin_string_iteration() -> FlatHashMapIterator*
	{	 
		if (!stringsIterator)
		{
			return nullptr;
		}
		stringsIterator->index = m_uCurrentSize > 0 && m_uCurrentSize <= m_uBufferSize ? 0u : k_iterator_end;
		return stringsIterator;
	}	 
		 
	auto StringArray::get_string_count() const -> size_t
	{	 
		return m_uStringCount;
	}	 
		 
	auto StringArray::get_string(uint32_t index) const -> const char*
	{	 
		uint32_t data_index = index;
		if (m_pData && m_uCurrentSize <= m_uBufferSize && data_index < m_uCurrentSize) {
			return m_pData + data_index;
		}
		return nullptr;
	}	 
		 
	auto StringArray::get_next_string(FlatHashMapIterator* it) const -> const char*
	{	 
		if (!m_pData || m_uCurrentSize > m_uBufferSize || !it || it->is_invalid() || it->index >= m_uCurrentSize)
		{
			return nullptr;
		}
		const uint32_t index = static_cast<uint32_t>(it->index);
		const char* string = m_pData + index;
		const size_t next_index = static_cast<size_t>(index) + std::strlen(string) + 1u;
		it->index = next_index < m_uCurrentSize ? next_index : k_iterator_end;
		return string;
	}	 
		 
	auto StringArray::has_next_string(FlatHashMapIterator* it) const -> bool
	{	 
		return m_pData && m_uCurrentSize <= m_uBufferSize && it && it->is_valid() && it->index < m_uCurrentSize;
	}	 
		 
	auto StringArray::intern(const char* string) -> const char*
	{	 
		if (!string || !m_pData || !string2Index || m_uCurrentSize > m_uBufferSize)
		{
			return nullptr;
		}
		constexpr size_t seed = 0xf2ea4ffad;
		const size_t length = strlen(string);
		uint64_t hashed_string = hash_bytes((void*)string, length, seed);

		uint32_t string_index = string2Index->get(hashed_string);
		if (string_index != UINT32_MAX)
		{
			if (string_index < m_uCurrentSize && std::strcmp(m_pData + string_index, string) == 0)
			{
				return m_pData + string_index;
			}
			for (uint32_t offset = 0; offset < m_uCurrentSize;)
			{
				const char* existing = m_pData + offset;
				if (std::strcmp(existing, string) == 0)
				{
					return existing;
				}
				offset += static_cast<uint32_t>(std::strlen(existing) + 1u);
			}
		}

		if (length == std::numeric_limits<size_t>::max() ||
			length + 1u > m_uBufferSize - m_uCurrentSize)
		{
			HLogError("StringArray capacity exceeded while interning '{}'.", string);
			return nullptr;
		}
		string_index = m_uCurrentSize;
		if (string2Index->get(hashed_string) == UINT32_MAX && !string2Index->insert(hashed_string, string_index))
		{
			HLogError("StringArray failed to index interned string '{}'.", string);
			return nullptr;
		}
		memcpy(m_pData + string_index, string, length + 1u);
		m_uCurrentSize += static_cast<uint32_t>(length + 1u);
		++m_uStringCount;
		return m_pData + string_index;
	}
};

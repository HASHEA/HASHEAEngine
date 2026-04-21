#include "hstring.h"
#include "hmemory.h"
#include "ds/hhash_map.hpp"
#include "hassert.h"
namespace AshEngine
{


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
		if  (m_pData)
		{ 
			Ash_Free(allocator, m_pData);
		} 
		if (size < 1)
		{
			HLogError("Error : Buffer cannot be empty !\n");
			return;
		}
		m_pAllocator = allocator;
		m_pData = (char*) Ash_Alloc(m_pAllocator,size + 1,1);
		H_ASSERT(m_pData);
		memset(m_pData, 0, size + 1);
		m_pData[0] = 0;
		m_uBufferSize = (uint32_t)size;
		m_uCurrentSize = 0;
	}	 
		 
	auto StringBuffer::shutdown() -> void
	{	 
		Ash_Free(m_pAllocator,m_pData);
		m_uBufferSize = m_uCurrentSize = 0;
	}	 
		 
	auto StringBuffer::append(const char* string) -> void
	{	 
		append_f("%s", string);
	}	 
		 
	auto StringBuffer::append(const StringView& text) -> void
	{	 
		const size_t maxLength = (m_uCurrentSize + text.length) < m_uBufferSize ? text.length : m_uBufferSize - m_uCurrentSize;
		if (maxLength == 0 || maxLength >= m_uBufferSize)//cannot equal because 0 at the end
		{
			HLogError("Buffer full! Please allocate more size.\n");
			return;
		}
		memcpy(&m_pData[m_uCurrentSize], text.text, maxLength);
		m_uCurrentSize += (uint32_t)maxLength;
		// Add null termination for string.
		// By allocating one extra character for the null termination this is always safe to do.
		m_pData[m_uCurrentSize] = 0;
	}	 
		 
	auto StringBuffer::append_m(void* memory, size_t size) -> void
	{	 
		if (m_uCurrentSize + size >= m_uBufferSize) {
			HLogError("Buffer full! Please allocate more size.\n");
			return;
		}

		memcpy(&m_pData[m_uCurrentSize], memory, size);
		m_uCurrentSize += (uint32_t)size;
	}	 
		 
	auto StringBuffer::append(const StringBuffer& otherBuffer) -> void
	{	 
		if (otherBuffer.m_uCurrentSize == 0) {
			return;
		}
		if (m_uCurrentSize + otherBuffer.m_uCurrentSize >= m_uBufferSize) {
			HLogError("Buffer full! Please allocate more size.\n");
			return;
		}
		memcpy(&m_pData[m_uCurrentSize], otherBuffer.m_pData, otherBuffer.m_uCurrentSize);
		m_uCurrentSize += otherBuffer.m_uCurrentSize;
	}	 
		 
	auto StringBuffer::append_f(const char* format, ...) -> void
	{	 
		if (m_uCurrentSize >= m_uBufferSize)
		{
			HLogError("Buffer full! Please allocate more size.\n");
			return;
		}
		va_list args;
		va_start(args,format);
#if defined(_MSC_VER)
		int written_chars = vsnprintf_s(&m_pData[m_uCurrentSize], m_uBufferSize - m_uCurrentSize, _TRUNCATE, format, args);
#else
		int written_chars = vsnprintf(&m_pData[m_uCurrentSize], m_uBufferSize - m_uCurrentSize, format, args);
#endif
		m_uCurrentSize += written_chars > 0 ? written_chars : 0;
		va_end(args);
		if (written_chars < 0) {
			HLogError("New string too big for current buffer! Please allocate more size.\n");
		}
	}	 
		 
	auto StringBuffer::append_get(const char* string) -> char*
	{	 
		return append_get_f("%s", string);;
	}	 
		 
	auto StringBuffer::append_get_f(const char* format, ...) -> char*
	{	 
		uint32_t cached_offset = this->m_uCurrentSize;

		// TODO: safer version!
		// TODO: do not copy paste!
		if (m_uCurrentSize >= m_uBufferSize) {
			HLogError("Buffer full! Please allocate more size.\n");
			return nullptr;
		}

		va_list args;
		va_start(args, format);
#if defined(_MSC_VER)
		int written_chars = vsnprintf_s(&m_pData[m_uCurrentSize], m_uBufferSize - m_uCurrentSize, _TRUNCATE, format, args);
#else
		int written_chars = vsnprintf(&m_pData[m_uCurrentSize], m_uBufferSize - m_uCurrentSize, format, args);
#endif
		m_uCurrentSize += written_chars > 0 ? written_chars : 0;
		va_end(args);

		if (written_chars < 0) {
			HLogError("New string too big for current buffer! Please allocate more size.\n");
		}

		// Add null termination for string.
		// By allocating one extra character for the null termination this is always safe to do.
		m_pData[m_uCurrentSize] = 0;
		++m_uCurrentSize;

		return this->m_pData + cached_offset;
	}	 
		 
	auto StringBuffer::append_get(const StringView& text) -> char*
	{	 
		uint32_t cached_offset = this->m_uCurrentSize;

		append(text);
		++m_uCurrentSize;

		return this->m_pData + cached_offset;
	}	 
		 
	auto StringBuffer::append_get_substring(const char* string, uint32_t start_index, uint32_t end_index) -> char*
	{	 
		uint32_t size = end_index - start_index;
		if (m_uCurrentSize + size >= m_uBufferSize) {
			HLogError("Buffer full! Please allocate more size.\n");
			return nullptr;
		}

		uint32_t cached_offset = this->m_uCurrentSize;

		memcpy(&m_pData[m_uCurrentSize], string, size);
		m_uCurrentSize += size;

		m_pData[m_uCurrentSize] = 0;
		++m_uCurrentSize;

		return this->m_pData + cached_offset;
	}	 
		 
	auto StringBuffer::close_current_string() -> void
	{	 
		m_pData[m_uCurrentSize] = 0;
		++m_uCurrentSize;
	}	 
		 
	auto StringBuffer::get_index(const char* text) const -> uint32_t
	{	 
		uint64_t text_distance = text - m_pData;
		return text_distance < m_uCurrentSize ? uint32_t(text_distance) : UINT32_MAX;
	}	 
		 
	auto StringBuffer::get_text(uint32_t index) const -> const char*
	{	 
		//why buffer size? not current size ?
		return index < m_uCurrentSize ? cstring(m_pData + index) : nullptr;

	}	 
		 
	auto StringBuffer::reserve(size_t size) -> char*
	{	 
		if (m_uCurrentSize + size >= m_uBufferSize)
			return nullptr;

		uint32_t offset = m_uCurrentSize;
		m_uCurrentSize += (uint32_t)size;

		return m_pData + offset;
	}	 
		 
	auto StringBuffer::clear() -> void
	{	 
		m_uCurrentSize = 0;
		m_pData[0] = 0;
	}	 
		 
	auto StringArray::init(uint32_t size, Allocator* allocator)
	{	 
		m_pAllocator = allocator;
		// Allocate also memory for the hash map
		char* allocated_memory = (char*)Ash_Alloc(m_pAllocator, size + sizeof(FlatHashMap<uint64_t, uint32_t>) + sizeof(FlatHashMapIterator),1);
		string2Index = (FlatHashMap<uint64_t, uint32_t>*)allocated_memory;
		string2Index->init(allocator, 8);
		string2Index->set_default_value(UINT32_MAX);
		stringsIterator = (FlatHashMapIterator*)(allocated_memory + sizeof(FlatHashMap<uint64_t, uint32_t>));
		m_pData = allocated_memory + sizeof(FlatHashMap<uint64_t, uint32_t>) + sizeof(FlatHashMapIterator);
		m_uBufferSize = size;
		m_uCurrentSize = 0;
	}	 
		 
	auto StringArray::shutdown() -> void
	{	 
		// string_to_index contains ALL the memory including data.
		Ash_Free(m_pAllocator, string2Index);
		m_uBufferSize = m_uCurrentSize = 0;
	}	 
		 
	auto StringArray::clear() -> void
	{	 
		m_uCurrentSize = 0;

		string2Index->clear();
	}	 
		 
	auto StringArray::begin_string_iteration() -> FlatHashMapIterator*
	{	 
		*stringsIterator = string2Index->iterator_begin();
		return stringsIterator;
	}	 
		 
	auto StringArray::get_string_count() const -> size_t
	{	 
		return string2Index->size();
	}	 
		 
	auto StringArray::get_string(uint32_t index) const -> const char*
	{	 
		uint32_t data_index = index;
		if (data_index < m_uCurrentSize) {
			return m_pData + data_index;
		}
		return nullptr;
	}	 
		 
	auto StringArray::get_next_string(FlatHashMapIterator* it) const -> const char*
	{	 
		uint32_t index = string2Index->get(*it);
		string2Index->iterator_advance(*it);
		cstring string = get_string(index);
		return string;
	}	 
		 
	auto StringArray::has_next_string(FlatHashMapIterator* it) const -> bool
	{	 
		return it->is_valid();
	}	 
		 
	auto StringArray::intern(const char* string) -> const char*
	{	 
		static size_t seed = 0xf2ea4ffad;
		const size_t length = strlen(string);
		uint64_t hashed_string = hash_bytes((void*)string, length, seed);

		uint32_t string_index = string2Index->get(hashed_string);
		if (string_index != UINT32_MAX) {
			return m_pData + string_index;
		}

		string_index = m_uCurrentSize;
		// Increase current buffer with new interned string
		m_uCurrentSize += (uint32_t)length + 1; // null termination
		strcpy(m_pData + string_index, string);
		// Update hash map
		bool ret = string2Index->insert(hashed_string, string_index);
		if (!ret)
		{
			HLogError("insert failed at {0} {1}, index : {2}",__FILE__,__LINE__, string_index);
		}
		return m_pData + string_index;
	}	 
};		 
		 
		 
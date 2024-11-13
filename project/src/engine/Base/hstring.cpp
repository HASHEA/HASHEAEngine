#include "hstring.h"
#include "hmemory.h"
#include "ds/hhash_map.hpp"
#include "hassert.h"
namespace HASHEAENGINE
{


	bool HASHEAENGINE::StringView::Equals(const StringView& a, const StringView& b)
	{
		return false;
	}

	void StringView::CopyTo(const StringView& a, char* buffer, size_t bufferSize)
	{	 
	}	 
		 
	auto StringBuffer::Init(size_t size, Allocator* allocator) -> void
	{	 
		if  (m_pData)
		{ 
			Hashea_Free(allocator, m_pData);
		} 
		if (size < 1)
		{
			HLogError("Error : Buffer cannot be empty !\n");
			return;
		}
		m_pAllocator = allocator;
		m_pData = (char*) Hashea_Alloc(m_pAllocator,size + 1,1);
		H_ASSERT(m_pData);
		m_pData[0] = 0;
		m_uBufferSize = (uint32_t)size;
		m_uCurrentSize = 0;
	}	 
		 
	auto StringBuffer::Shutdown() -> void
	{	 
		Hashea_Free(m_pAllocator,m_pData);
		m_uBufferSize = m_uCurrentSize = 0;
	}	 
		 
	auto StringBuffer::Append(const char* string) -> void
	{	 
		Append("%s", string);
	}	 
		 
	auto StringBuffer::Append(const StringView& text) -> void
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
		 
	auto StringBuffer::Append(void* memory, size_t size) -> void
	{	 
		if (m_uCurrentSize + size >= m_uBufferSize) {
			HLogError("Buffer full! Please allocate more size.\n");
			return;
		}

		memcpy(&m_pData[m_uCurrentSize], memory, size);
		m_uCurrentSize += (uint32_t)size;
	}	 
		 
	auto StringBuffer::Append(const StringBuffer& otherBuffer) -> void
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
		 
	auto StringBuffer::Append(const char* format, ...) -> void
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
		 
	auto StringBuffer::AppendGet(const char* string) -> char*
	{	 
		return AppendGet("%s", string);;
	}	 
		 
	auto StringBuffer::AppendGet(const char* format, ...) -> char*
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
		 
	auto StringBuffer::AppendGet(const StringView& text) -> char*
	{	 
		uint32_t cached_offset = this->m_uCurrentSize;

		Append(text);
		++m_uCurrentSize;

		return this->m_pData + cached_offset;
	}	 
		 
	auto StringBuffer::AppendGetSubstring(const char* string, uint32_t start_index, uint32_t end_index) -> char*
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
		 
	auto StringBuffer::CloseCurrentString() -> void
	{	 
		m_pData[m_uCurrentSize] = 0;
		++m_uCurrentSize;
	}	 
		 
	auto StringBuffer::GetIndex(const char* text) const -> uint32_t
	{	 
		uint64_t text_distance = text - m_pData;
		return text_distance < m_uCurrentSize ? uint32_t(text_distance) : UINT32_MAX;
	}	 
		 
	auto StringBuffer::GetText(uint32_t index) const -> const char*
	{	 
		//why buffer size? not current size ?
		return index < m_uCurrentSize ? cstring(m_pData + index) : nullptr;

	}	 
		 
	auto StringBuffer::Reserve(size_t size) -> char*
	{	 
		if (m_uCurrentSize + size >= m_uBufferSize)
			return nullptr;

		uint32_t offset = m_uCurrentSize;
		m_uCurrentSize += (uint32_t)size;

		return m_pData + offset;
	}	 
		 
	auto StringBuffer::Clear() -> void
	{	 
		m_uCurrentSize = 0;
		m_pData[0] = 0;
	}	 
		 
	auto StringArray::Init(uint32_t size, Allocator* allocator)
	{	 
		m_pAllocator = allocator;
		// Allocate also memory for the hash map
		char* allocated_memory = (char*)Hashea_Alloc(m_pAllocator, size + sizeof(FlatHashMap<uint64_t, uint32_t>) + sizeof(FlatHashMapIterator),1);
		string2Index = (FlatHashMap<uint64_t, uint32_t>*)allocated_memory;
		string2Index->Init(allocator, 8);
		string2Index->SetDefaultValue(UINT32_MAX);
		stringsIterator = (FlatHashMapIterator*)(allocated_memory + sizeof(FlatHashMap<uint64_t, uint32_t>));
		m_pData = allocated_memory + sizeof(FlatHashMap<uint64_t, uint32_t>) + sizeof(FlatHashMapIterator);
		m_uBufferSize = size;
		m_uCurrentSize = 0;
	}	 
		 
	auto StringArray::Shutdown() -> void
	{	 
		// string_to_index contains ALL the memory including data.
		Hashea_Free(m_pAllocator, string2Index);
		m_uBufferSize = m_uCurrentSize = 0;
	}	 
		 
	auto StringArray::Clear() -> void
	{	 
		m_uCurrentSize = 0;

		string2Index->Clear();
	}	 
		 
	auto StringArray::BeginStringIteration() -> FlatHashMapIterator*
	{	 
		*stringsIterator = string2Index->IteratorBegin();
		return stringsIterator;
	}	 
		 
	auto StringArray::GetStringCount() const -> size_t
	{	 
		return string2Index->Size();
	}	 
		 
	auto StringArray::GetString(uint32_t index) const -> const char*
	{	 
		uint32_t data_index = index;
		if (data_index < m_uCurrentSize) {
			return m_pData + data_index;
		}
		return nullptr;
	}	 
		 
	auto StringArray::GetNextString(FlatHashMapIterator* it) const -> const char*
	{	 
		uint32_t index = string2Index->Get(*it);
		string2Index->IteratorAdvance(*it);
		cstring string = GetString(index);
		return string;
	}	 
		 
	auto StringArray::HasNextString(FlatHashMapIterator* it) const -> bool
	{	 
		return it->isValid();
	}	 
		 
	auto StringArray::Intern(const char* string) -> const char*
	{	 
		static size_t seed = 0xf2ea4ffad;
		const size_t length = strlen(string);
		uint64_t hashed_string = HashBytes((void*)string, length, seed);

		uint32_t string_index = string2Index->Get(hashed_string);
		if (string_index != UINT32_MAX) {
			return m_pData + string_index;
		}

		string_index = m_uCurrentSize;
		// Increase current buffer with new interned string
		m_uCurrentSize += (uint32_t)length + 1; // null termination
		strcpy(m_pData + string_index, string);
		// Update hash map
		HS_Result ret = string2Index->Insert(hashed_string, string_index);
		if (HS_CHECK_FAILED(ret))
		{
			HLogError("insert failed at {0} {1}, index : {2}",__FILE__,__LINE__, string_index);
		}
		return m_pData + string_index;
	}	 
};		 
		 
		 
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
		 
	auto StringBuffer::Append(const StringView& test) -> void
	{	 
		if (m_uCurrentSize >= m_uBufferSize)
		{

		}
	}	 
		 
	auto StringBuffer::Append(void* memory, size_t size) -> void
	{	 
	}	 
		 
	auto StringBuffer::Append(const StringBuffer& otherBuffer) -> void
	{	 
	}	 
		 
	auto StringBuffer::Append(const char* format, ...) -> void
	{	 
	}	 
		 
	auto StringBuffer::AppendGet(const char* string) -> char*
	{	 
		return nullptr;
	}	 
		 
	auto StringBuffer::AppendGet(const char* format, ...) -> char*
	{	 
		return nullptr;
	}	 
		 
	auto StringBuffer::AppendGet(const StringView& test) -> char*
	{	 
		return nullptr;
	}	 
		 
	auto StringBuffer::AppendGetSubstring(const char* string, uint32_t start_index, uint32_t end_index) -> char*
	{	 
		return nullptr;
	}	 
		 
	auto StringBuffer::CloseCurrentString() -> void
	{	 
	}	 
		 
	auto StringBuffer::GetIndex(const char* text) const -> uint32_t
	{	 
		return 0;
	}	 
		 
	auto StringBuffer::GetText(uint32_t index) const -> const char*
	{	 
		return nullptr;
	}	 
		 
	auto StringBuffer::Reserve(size_t size) -> char*
	{	 
		return nullptr;
	}	 
		 
	auto StringBuffer::Clear() -> void
	{	 
	}	 
		 
	auto StringArray::Init(uint32_t size, Allocator* allocator)
	{	 
	}	 
		 
	auto StringArray::Shutdown() -> void
	{	 
	}	 
		 
	auto StringArray::Clear() -> void
	{	 
	}	 
		 
	auto StringArray::BeginStringIteration() -> FlatHashMapIterator*
	{	 
		return nullptr;
	}	 
		 
	auto StringArray::GetStringCount() const -> size_t
	{	 
		return size_t();
	}	 
		 
	auto StringArray::GetString(uint32_t index) const -> const char*
	{	 
		return nullptr;
	}	 
		 
	auto StringArray::GetNextString(FlatHashMapIterator* it) const -> const char*
	{	 
		return nullptr;
	}	 
		 
	auto StringArray::HasNextString(FlatHashMapIterator* it) const -> bool
	{	 
		return false;
	}	 
		 
	auto StringArray::Intern(const char* string) -> const char*
	{	 
		return nullptr;
	}	 
		 
		 
};		 
		 
		 
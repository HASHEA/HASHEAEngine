#pragma once
#include "hcore.h"
#include "hplatform.h"

namespace AshEngine
{
	struct Allocator;
	template <typename K, typename V>
	class FlatHashMap;
	struct FlatHashMapIterator;

	struct ASH_API StringView
	{
		char* text;
		size_t length;
		static bool equals(const StringView& a, const StringView& b);
		static void copy_to(const StringView& a, char* buffer, size_t bufferSize);
	};

	struct StringBuffer
	{
		ASH_API auto init(size_t size,Allocator* allocator) -> void;
		ASH_API auto shutdown() -> void;
		ASH_API auto append(const char* string) -> void;
		ASH_API auto append(const StringView& text) -> void;
		ASH_API auto append_m(void* memory, size_t size) -> void;
		ASH_API auto append(const StringBuffer& otherBuffer) -> void;
		ASH_API auto append_f(const char* format,...) -> void;

		ASH_API auto append_get(const char* string) -> char*;
		ASH_API auto append_get_f(const char* format, ...) -> char*;
		ASH_API auto append_get(const StringView& text) -> char*;
		ASH_API auto append_get_substring(const char* string, uint32_t start_index, uint32_t end_index) -> char*;
		ASH_API auto close_current_string() -> void;
		ASH_API auto get_index(const char* text) const -> uint32_t ;
		ASH_API auto get_text(uint32_t index) const -> const char*;
		ASH_API auto reserve(size_t size) -> char*;
		auto current() -> char* { return m_pData && m_uCurrentSize <= m_uBufferSize ? m_pData + m_uCurrentSize : nullptr; }
		ASH_API auto clear() -> void;
		char* m_pData = nullptr;
		uint32_t                         m_uBufferSize = 0;
		uint32_t                         m_uCurrentSize = 0;
		Allocator* m_pAllocator = nullptr;
	};

	struct StringArray
	{
		ASH_API auto init(uint32_t size, Allocator* allocator) -> void;
		ASH_API auto shutdown() -> void;
		ASH_API auto clear() -> void;
		ASH_API auto begin_string_iteration() -> FlatHashMapIterator*;
		ASH_API auto get_string_count()const->size_t;
		ASH_API auto get_string(uint32_t index)const -> const char*;
		ASH_API auto get_next_string(FlatHashMapIterator* it)const -> const char*;
		ASH_API auto has_next_string(FlatHashMapIterator* it)const -> bool;
		ASH_API auto intern(const char* string) -> const char*;

		FlatHashMap<uint64_t, uint32_t>* string2Index = nullptr;
		FlatHashMapIterator* stringsIterator = nullptr;

		char* m_pData = nullptr;
		uint32_t                         m_uBufferSize = 0;
		uint32_t                         m_uCurrentSize = 0;
		uint32_t                         m_uStringCount = 0;
		Allocator* m_pAllocator = nullptr;
	};
};

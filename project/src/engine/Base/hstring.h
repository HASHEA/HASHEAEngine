#pragma once
#include "hcore.h"
#include "hplatform.h"

namespace HASHEAENGINE
{
	struct Allocator;
	template <typename K, typename V>
	struct FlatHashMap;
	struct FlatHashMapIterator;

	struct HASHEA_API StringView
	{
		char* text;
		size_t length;
		static bool equals(const StringView& a, const StringView& b);
		static void copy_to(const StringView& a, char* buffer, size_t bufferSize);
	};

	struct StringBuffer
	{
		auto init(size_t size,Allocator* allocator) -> void;
		auto shutdown() -> void;
		auto append(const char* string) -> void;
		auto append(const StringView& text) -> void;
		auto append(void* memory, size_t size) -> void;
		auto append(const StringBuffer& otherBuffer) -> void;
		auto append(const char* format,...) -> void;

		auto append_get(const char* string) -> char*;
		auto append_get(const char* format, ...) -> char*;
		auto append_get(const StringView& text) -> char*;
		auto append_get_substring(const char* string, uint32_t start_index, uint32_t end_index) -> char*;
		auto close_current_string() -> void;
		auto get_index(const char* text) const -> uint32_t ;
		auto get_text(uint32_t index) const -> const char*;
		auto reserve(size_t size) -> char*;
		auto current() -> char* { return m_pData + m_uCurrentSize; }
		auto clear() -> void;
		char* m_pData = nullptr;
		uint32_t                         m_uBufferSize = 1024;
		uint32_t                         m_uCurrentSize = 0;
		Allocator* m_pAllocator = nullptr;
	};

	struct StringArray
	{
		auto init(uint32_t size, Allocator* allocator);
		auto shutdown() -> void;
		auto clear() -> void;
		auto begin_string_iteration() -> FlatHashMapIterator*;
		auto get_string_count()const->size_t;
		auto get_string(uint32_t index)const -> const char*;
		auto get_next_string(FlatHashMapIterator* it)const -> const char*;
		auto has_next_string(FlatHashMapIterator* it)const -> bool;
		auto intern(const char* string) -> const char*;

		FlatHashMap<uint64_t, uint32_t>* string2Index = nullptr;
		FlatHashMapIterator* stringsIterator = nullptr;

		char* m_pData = nullptr;
		uint32_t                         m_uBufferSize = 1024;
		uint32_t                         m_uCurrentSize = 0;
		Allocator* m_pAllocator = nullptr;
	};
};
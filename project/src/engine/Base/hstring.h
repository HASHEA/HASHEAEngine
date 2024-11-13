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
		static bool Equals(const StringView& a, const StringView& b);
		static void CopyTo(const StringView& a, char* buffer, size_t bufferSize);
	};

	struct StringBuffer
	{
		auto Init(size_t size,Allocator* allocator) -> void;
		auto Shutdown() -> void;
		auto Append(const char* string) -> void;
		auto Append(const StringView& text) -> void;
		auto Append(void* memory, size_t size) -> void;
		auto Append(const StringBuffer& otherBuffer) -> void;
		auto Append(const char* format,...) -> void;

		auto AppendGet(const char* string) -> char*;
		auto AppendGet(const char* format, ...) -> char*;
		auto AppendGet(const StringView& text) -> char*;
		auto AppendGetSubstring(const char* string, uint32_t start_index, uint32_t end_index) -> char*;
		auto CloseCurrentString() -> void;
		auto GetIndex(const char* text) const -> uint32_t ;
		auto GetText(uint32_t index) const -> const char*;
		auto Reserve(size_t size) -> char*;
		auto Current() -> char* { return m_pData + m_uCurrentSize; }
		auto Clear() -> void;
		char* m_pData = nullptr;
		uint32_t                         m_uBufferSize = 1024;
		uint32_t                         m_uCurrentSize = 0;
		Allocator* m_pAllocator = nullptr;
	};

	struct StringArray
	{
		auto Init(uint32_t size, Allocator* allocator);
		auto Shutdown() -> void;
		auto Clear() -> void;
		auto BeginStringIteration() -> FlatHashMapIterator*;
		auto GetStringCount()const->size_t;
		auto GetString(uint32_t index)const -> const char*;
		auto GetNextString(FlatHashMapIterator* it)const -> const char*;
		auto HasNextString(FlatHashMapIterator* it)const -> bool;
		auto Intern(const char* string) -> const char*;

		FlatHashMap<uint64_t, uint32_t>* string2Index = nullptr;
		FlatHashMapIterator* stringsIterator = nullptr;

		char* m_pData = nullptr;
		uint32_t                         m_uBufferSize = 1024;
		uint32_t                         m_uCurrentSize = 0;
		Allocator* m_pAllocator = nullptr;
	};
};
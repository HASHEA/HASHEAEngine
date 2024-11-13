#pragma once
#include "hplatform.h"
#include "hserialization.h"
#include "hmemory.h"
namespace HASHEAENGINE
{
	auto Serializer::WriteCommon(Allocator* _allocator, uint32_t serializer_version, size_t size) -> void
	{
		allocator = _allocator;
		blobMemory = (char*)Hashea_Alloc(allocator,size + sizeof(BlobHeader),1);
		H_ASSERT(blobMemory);
		hasAllocatedMemory = 1;
		totalSize = (uint32_t)size + sizeof(BlobHeader);
		serializedOffset = allocatedOffset = 0;
		serializerVersion = serializer_version;
		dataVersion = serializer_version;
		isReading = 0;
		isMappable = 0;
		BlobHeader* l_pHeader = (BlobHeader*)AllocateStatic(sizeof(BlobHeader));
		l_pHeader->version = serializerVersion;
		l_pHeader->mappable = isMappable;
		serializedOffset = allocatedOffset;
	}
	auto Serializer::Shutdown() -> void
	{
		if (isReading) {
			if (blobMemory && hasAllocatedMemory/*if false, map*/)
			{
				Hashea_Free(allocator, blobMemory);
			}
		}
		else {
			if (blobMemory)
			{
				Hashea_Free(allocator, blobMemory);
			}		
		}
		serializedOffset = allocatedOffset = 0;
	}

	auto Serializer::Serialize(RelativeString* data) -> void
	{
		if (isReading) {
			// Blob --> Data
			Serialize(&data->size);

			int32_t source_data_offset;
			Serialize(&source_data_offset);

			if (source_data_offset > 0) {
				// Cache serialized
				uint32_t cached_serialized = serializedOffset;

				serializedOffset = allocatedOffset;

				data->data.offset = GetRelativeDataOffset(data) - 4;

				// Reserve memory + string ending
				AllocateStatic((size_t)data->size + 1);

				char* source_data = blobMemory + cached_serialized + source_data_offset - 4;
				memcpy((char*)data->c_str(), source_data, (size_t)data->size + 1);
				// Restore serialized
				serializedOffset = cached_serialized;
			}
			else {
				data->set_empty();
			}
		}
		else {
			// Data --> Blob
			Serialize(&data->size);
			// Data will be copied at the end of the current blob
			int32_t data_offset = allocatedOffset - serializedOffset;
			Serialize(&data_offset);

			uint32_t cached_serialized = serializedOffset;
			// Move serialization to at the end of the blob.
			serializedOffset = allocatedOffset;
			// Allocate memory in the blob
			AllocateStatic((size_t)data->size + 1);

			char* destination_data = blobMemory + serializedOffset;
			memcpy(destination_data, (char*)data->c_str(), (size_t)data->size + 1);
			// Restore serialized
			serializedOffset = cached_serialized;
		}
	}

	auto Serializer::AllocateStatic(size_t size) -> char*
	{
		if (allocatedOffset + size > totalSize)
		{
			HLogError("Blob allocation error: allocated, requested, total - {0} + {1} > {2}\n", allocatedOffset, size, totalSize);
			return nullptr;
		}
		uint32_t offset = allocatedOffset;
		allocatedOffset += (uint32_t)size;
		return isReading ? dataMemory + offset : blobMemory + offset;
	}

	auto Serializer::AllocateAndSet(RelativeString& string, cstring format, ...) -> void
	{
		uint32_t cached_offset = allocatedOffset;

		char* dst = isReading ? dataMemory : blobMemory;

		va_list args;
		va_start(args, format);
#if (_MSC_VER)
		int written_chars = vsnprintf_s(&(dst[allocatedOffset]), totalSize - allocatedOffset, _TRUNCATE, format, args);
#else
		int written_chars = vsnprintf(&dst[allocatedOffset], totalSize - allocatedOffset, format, args);
#endif
		allocatedOffset += written_chars > 0 ? written_chars : 0;
		va_end(args);

		if (written_chars < 0) {
			HLogError("New string too big for current buffer! Please allocate more size.\n");
		}

		// Add null termination for string.
		// By allocating one extra character for the null termination this is always safe to do.
		dst[allocatedOffset] = 0;
		++allocatedOffset;

		string.set(dst + cached_offset, written_chars);
	}

	auto Serializer::AllocateAndSet(RelativeString& string, char* text, uint32_t length) -> void
	{
		if (allocatedOffset + length > totalSize) {
			HLogError("New string too big for current buffer! Please allocate more size.\n");
			return;
		}
		uint32_t cached_offset = allocatedOffset;

		char* destination_memory = isReading ? dataMemory : blobMemory;
		memcpy(&destination_memory[allocatedOffset], text, length);

		allocatedOffset += length;

		// Add null termination for string.
		// By allocating one extra character for the null termination this is always safe to do.
		destination_memory[allocatedOffset] = 0;
		++allocatedOffset;

		string.set(destination_memory + cached_offset, length);

	}

	auto Serializer::GetRelativeDataOffset(void* data) -> int32_t
	{
		const int32_t data_offset_from_start = (int32_t)((char*)data - dataMemory);
		const int32_t data_offset = allocatedOffset - data_offset_from_start;
		return data_offset;
	}

	auto Serializer::Serialize(char* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(char));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(char));
		}

		serializedOffset += sizeof(char);
	}

	auto Serializer::Serialize(const char* data) -> void
	{
		/*size_t len = strlen(data);
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(char) * len);
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(char) * len);
		}

		serializedOffset += sizeof(char);*/
		//useless...
	}
	auto Serializer::Serialize(int8_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(int8_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(int8_t));
		}

		serializedOffset += sizeof(int8_t);
	}
	auto Serializer::Serialize(uint8_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(uint8_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(uint8_t));
		}

		serializedOffset += sizeof(uint8_t);
	}
	auto Serializer::Serialize(int16_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(int16_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(int16_t));
		}

		serializedOffset += sizeof(int16_t);
	}
	auto Serializer::Serialize(uint16_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(uint16_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(uint16_t));
		}

		serializedOffset += sizeof(uint16_t);
	}
	auto Serializer::Serialize(int32_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(int32_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(int32_t));
		}

		serializedOffset += sizeof(int32_t);
	}
	auto Serializer::Serialize(uint32_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(uint32_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(uint32_t));
		}

		serializedOffset += sizeof(uint32_t);
	}
	auto Serializer::Serialize(int64_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(int64_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(int64_t));
		}

		serializedOffset += sizeof(int64_t);
	}
	auto Serializer::Serialize(uint64_t* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(uint64_t));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(uint64_t));
		}

		serializedOffset += sizeof(uint64_t);
	}
	auto Serializer::Serialize(float* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(float));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(float));
		}

		serializedOffset += sizeof(float);
	}
	auto Serializer::Serialize(double* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(double));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(double));
		}

		serializedOffset += sizeof(double);
	}
	auto Serializer::Serialize(bool* data) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], sizeof(bool));
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, sizeof(bool));
		}

		serializedOffset += sizeof(bool);
	}
	auto Serializer::SerializeMemroy(void* data, size_t size) -> void
	{
		if (isReading) {
			memcpy(data, &blobMemory[serializedOffset], size);
		}
		else {
			memcpy(&blobMemory[serializedOffset], data, size);
		}

		serializedOffset += size;
	}
	auto Serializer::SerializeMemoryBlock(void** data, uint32_t* size) -> void
	{
		//write /read size first and move the  serialized pointer
		Serialize(size);
		if (isReading)
		{
			//read the offset first
			int32_t offset = 0;
			Serialize(&offset);
			if (offset > 0)
			{
				uint32_t cachedSerializedOffset = serializedOffset;
				serializedOffset = allocatedOffset;
				AllocateStatic(*size);
				*data = dataMemory + serializedOffset;
				char* srcData = blobMemory + cachedSerializedOffset + offset - sizeof(int32_t);//[-sizeof(int32_t)]here for when writing we calculate the offset before sz it, 
				//which means when reading,we should return back the size of offset before we apply the offset;
				memcpy(*data, srcData,*size);
				serializedOffset = cachedSerializedOffset;
			}
			else
			{
				*data = nullptr;
				size = 0;
			}
		}
		else
		{
			int32_t offset = allocatedOffset - serializedOffset;
			//write the offset at the position instead of the value of the absolute pointer
			//and move the serialized pointer
			Serialize(&offset);
			//cache the serialized offset pointer
			uint32_t cachedSerializedOffset = serializedOffset;
			serializedOffset = allocatedOffset;
			//allocate the data mem in the blob
			AllocateStatic(*size);
			char* destData = blobMemory + serializedOffset;
			memcpy(destData,*data,*size);
			serializedOffset = cachedSerializedOffset;
		}
	}
};
#pragma once
#include "hplatform.h"
#include "hfile.h"
#include "hassert.h"
#include "ds/harray.hpp"
namespace HASHEAENGINE
{
    struct Allocator;

    struct BlobHeader {
        uint32_t                 version;
        uint32_t                 mappable;
    }; // struct BlobHeader

    //
    //Base class for datas that needs to be szd
    struct Blob {
        BlobHeader          header;
    }; // struct Blob

    template <typename T>
    struct RelativePointer {

        T* get() const;

        bool                is_equal(const RelativePointer& other) const;
        bool                is_null() const;
        bool                is_not_null() const;

        // Operator overloading to give a cleaner interface
        T* operator->() const;
        T& operator*() const;
        void                set(char* raw_pointer);
        void                set_null();
        int32_t                 offset;

    }; // struct RelativePointer

    template <typename T>
    struct RelativeArray {

        const T& operator[](uint32_t index) const;
        T& operator[](uint32_t index);

        const T* get() const;
        T* get();


        void                    set(char* raw_pointer, uint32_t size);
        void                    set_empty();


        uint32_t                     size;
        RelativePointer<T>      data;
    }; // struct RelativeArray

    // RelativeString /////////////////////////////////////////////////////////

//
//
    struct RelativeString : public RelativeArray<char> {

        cstring             c_str() const { return data.get(); }

        void                set(char* pointer_, uint32_t size_) { RelativeArray<char>::set(pointer_, size_); }
    }; // struct RelativeString


    template<typename T>
    inline T* RelativePointer<T>::get() const {
        char* address = ((char*)&offset) + offset;
        return offset != 0 ? (T*)address : nullptr;
    }

    template<typename T>
    inline bool RelativePointer<T>::is_equal(const RelativePointer& other) const {
        return get() == other.get();
    }

    template<typename T>
    inline bool RelativePointer<T>::is_null() const {
        return offset == 0;
    }

    template<typename T>
    inline bool RelativePointer<T>::is_not_null() const {
        return offset != 0;
    }

    template<typename T>
    inline T* RelativePointer<T>::operator->() const {
        return get();
    }

    template<typename T>
    inline T& RelativePointer<T>::operator*() const {
        return *(get());
    }

    template<typename T>
    inline void RelativePointer<T>::set(char* raw_pointer) {
        offset = raw_pointer ? (int32_t)(raw_pointer - (char*)this) : 0;
    }
    template<typename T>
    inline void RelativePointer<T>::set_null() {
        offset = 0;
    }


    // RelativeArray //////////////////////////////////////////////////////////
    template<typename T>
    inline const T& RelativeArray<T>::operator[](uint32_t index) const {
        H_ASSERT(index < size);
        return data.get()[index];
    }

    template<typename T>
    inline T& RelativeArray<T>::operator[](uint32_t index) {
        H_ASSERT(index < size);
        return data.get()[index];
    }

    template<typename T>
    inline const T* RelativeArray<T>::get() const {
        return data.get();
    }

    template<typename T>
    inline T* RelativeArray<T>::get() {
        return data.get();
    }


    template<typename T>
    inline void RelativeArray<T>::set(char* raw_pointer, uint32_t size_) {
        data.set(raw_pointer);
        size = size_;
    }
    template<typename T>
    inline void RelativeArray<T>::set_empty() {
        size = 0;
        data.set_null();
    }

    /*https://yave.handmade.network/blogs/p/2723-how_media_molecule_does_serialization*/
    struct Serializer {

        template <typename T>
        auto WriteAndPrepare(Allocator* allocator, uint32_t serializer_version, size_t size) -> T*;

        template <typename T>
        auto WriteAndSerialize(Allocator* allocator, uint32_t serializer_version, size_t size, T* root_data) -> void;

        auto WriteCommon(Allocator* allocator, uint32_t serializer_version, size_t size) -> void;

        template <typename T>
        auto Read(Allocator* allocator, uint32_t serializer_version, size_t size, char* blob_memory, bool force_serialization = false) -> T*;
        auto Shutdown() -> void;
        //FILE* file_handle;
        //uint32_t             data_version;
        //uint8_t              is_writing;

        auto Serialize(char* data) -> void;
        auto Serialize(const char* data) -> void;
        auto Serialize(int8_t* data) -> void;
        auto Serialize(uint8_t* data) -> void;
        auto Serialize(int16_t* data) -> void;
        auto Serialize(uint16_t* data) -> void;
        auto Serialize(int32_t* data) -> void;
        auto Serialize(uint32_t* data) -> void;
        auto Serialize(int64_t* data) -> void;
        auto Serialize(uint64_t* data) -> void;
        auto Serialize(float* data) -> void;
        auto Serialize(double* data) -> void;
        auto Serialize(bool* data) -> void;

        auto SerializeMemroy(void* data, size_t size) -> void;
        auto SerializeMemoryBlock(void** data, uint32_t* size) -> void;

        template <typename T>
        auto Serialize(RelativePointer<T>* data) -> void;

        template <typename T>
        auto Serialize(RelativeArray<T>* data) -> void;

        template <typename T>
        auto                Serialize(Array<T>* data) -> void;

        template <typename T>
        auto                Serialize(T* data) -> void;

        auto                Serialize(RelativeString* data) -> void;

        // Static allocation from the blob allocated memory.
        auto AllocateStatic(size_t size) -> char*;  // Just allocate size bytes and return. Used to fill in structures.

        template <typename T>
        auto AllocateStatic() -> T*;

        template <typename T>
        auto                AllocateAndSet(RelativePointer<T>& data, void* source_data = nullptr) -> void;


        template <typename T>
        auto                AllocateAndSet(RelativeArray<T>& data, uint32_t num_elements, void* source_data = nullptr) -> void;

        auto                AllocateAndSet(RelativeString& string, cstring format, ...) -> void;            // Allocate and set a static string.
        auto                AllocateAndSet(RelativeString& string, char* text, uint32_t length) -> void;      // Allocate and set a static string.

        auto GetRelativeDataOffset(void* data) -> int32_t;

        char* blobMemory = nullptr;
        char* dataMemory = nullptr;
        Allocator* allocator = nullptr;
        uint32_t totalSize = 0;
        uint32_t serializedOffset = 0;
        uint32_t allocatedOffset = 0;
        uint32_t serializerVersion = 0xffffffff;
        uint32_t dataVersion = 0xffffffff;
        uint32_t isReading = 0;
        uint32_t isMappable = 0;
        uint32_t hasAllocatedMemory = 0;
    };
    
    template<typename T>
    inline auto Serializer::WriteAndPrepare(Allocator* allocator, uint32_t serializer_version, size_t size) -> T*
    {
        WriteCommon(allocator, serializer_version, size);
        AllocateStatic(sizeof(T) - sizeof(BlobHeader));
        data_memory = nullptr;
        return (T*)blobMemory;
    }

    template<typename T>
    inline auto Serializer::WriteAndSerialize(Allocator* allocator, uint32_t serializer_version, size_t size, T* root_data) -> void
    {
        H_ASSERT(root_data);
        WriteCommon(allocator, serializer_version, size);
        AllocateStatic(sizeof(T) - sizeof(BlobHeader));
        data_memory = char* (root_data);
        Serialize(root_data);
    }

    template<typename T>
    inline auto Serializer::Read(Allocator* _allocator, uint32_t serializer_version, size_t size, char* blob_memory, bool force_serialization) -> T*
    {
        allocator = _allocator;
        blobMemory = blob_memory;
        dataMemory = nullptr;
        totalSize = (uint32_t)size;
        serializedOffset = allocatedOffset = 0;
        serializerVersion = serializer_version;
        isReading = 1;
        hasAllocatedMemory = 0;
        // Read header from blob.
        BlobHeader* header = (BlobHeader*)blob_memory;
        dataVersion = header->version;
        isMappable = header->mappable;
        if (serializerVersion == dataVersion && !force_serialization)
        {
            return (T*)(blobMemory);
        }
        hasAllocatedMemory = 1;
        serializerVersion = dataVersion;
        dataMemory = (char*)Hashea_Alloc(allocator,size,1);
        T* dst = (T*)dataMemory;
        serializedOffset += sizeof(BlobHeader);
        AllocateStatic(sizeof(T));
        Serialize(dst);
        return dst;
    }

    

    template<typename T>
    inline auto Serializer::Serialize(RelativePointer<T>* data) -> void
    {
        if (isReading)
        {
            //the offset in the blob
            int offset = 0;
            Serialize(&offset);
            if (offset == 0)
            {
                data->offset = 0;
                return;
            }
            //the offset in the data memory
            data->offset = GetRelativeDataOffset(data);
            AllocateStatic<T>();
            uint32_t cachedSerializeOffet = serializedOffset;
            serializedOffset = cachedSerializeOffet + offset - sizeof(uint32_t);
            Serialize(data->get());
            serializedOffset = cachedSerializeOffet;
        }
        else
        {
            int32_t offset = allocatedOffset - serializedOffset;
            Serialize(&offset);
            uint32_t cachedSerializedOffset = serializedOffset;
            serializedOffset = allocatedOffset;
            AllocateStatic<T>();
            Serialize(data->get());
            serializedOffset = cachedSerializedOffset;
        }
    }

    template<typename T>
    inline auto Serializer::Serialize(RelativeArray<T>* data) -> void
    {
        if (isReading)
        {
            Serialize(&(data->size));
            int32_t offset = 0;
            Serialize(&offset);
            uint32_t cachedSerializedOffset = serializedOffset;
            data->data.offset = GetRelativeDataOffset(data) - sizeof(uint32_t);
            AllocateStatic(data->size*sizeof(T));
            //when writing , the sz offset is the offset that before sz (offset), so we need to walk back the sizeof (offset)
            //or we can record the cached sz offset before we sz (offset) when reading
            serializedOffset = cachedSerializedOffset + offset - sizeof(uint32_t);
            for (uint32_t i = 0; i < data->size; i++)
            {
                T* dst = &data->get()[i];
                Serialize(dst);
            }
            serializedOffset = cachedSerializedOffset;
        }
        else
        {
            Serialize(&(data->size));
            int32_t offset = allocatedOffset - serializedOffset;
            Serialize(&offset);
            uint32_t cachedSerializedOffset = serializedOffset;
            serializedOffset = allocatedOffset;
            AllocateStatic(data->size * sizeof(T));
            for (uint32_t i = 0; i < data->size; i++)
            {
                T* srcData = &data->get()[i];
                Serialize(srcData;)
            }
            serializedOffset = cachedSerializedOffset;
        }
    }

    template<typename T>
    inline auto Serializer::Serialize(Array<T>* data) -> void
    {
        if (isReading)
        {
            Serialize(&(data->m_uSize));
            uint64_t pad = 0;
            Serialize(&pad);
            Serialize(&pad);
            uint32_t offset = 0;
            Serialize(&offset);
            uint32_t cacheSerializedOffset = serializedOffset;
            data->m_pAllocator = nullptr;
            data->m_uCapacity = data->m_uSize;
            data->m_pData = (T*)(dataMemory + allocatedOffset);
            AllocateStatic(data->m_uSize * sizeof(T));
            serialized_offset = cacheSerializedOffset + offset - sizeof(uint32_t);
            for (uint32_t i = 0; i < data->m_uSize; i++)
            {
                T* dst = &((*data)[i]);
                Serialize(dst);
            }
            serialized_offset = cacheSerializedOffset;
        }
        else
        {
            Serialize(&(data->m_uSize));
            //for 2 pointer member - -!
            uint64_t serializationPad = 0;
            Serialize(&serializationPad);
            Serialize(&serializationPad);
            int32_t offset = allocatedOffset - serializedOffset;
            Serialize(&offset);
            uint32_t cachedSerializedOffset = serializedOffset;
            serializedOffset = allocatedOffset;
            AllocateStatic(data->m_uSize * sizeof(T));
            for (uint32_t i = 0; i < data->m_uSize; i++)
            {
                T* src = &((*data)[i]);
                Serialize(src);
            }
            serializedOffset = cachedSerializedOffset;
        }
    }

    template<typename T>
    inline auto Serializer::Serialize(T* data) -> void
    {
        H_ASSERTLOG(false,"No Serialize impl for type : {}", typeid(T).name());
    }

    template<typename T>
    inline auto Serializer::AllocateStatic() -> T*
    {
        return (T*)AllocateStatic(sizeof(T));
    }

    template<typename T>
    inline auto Serializer::AllocateAndSet(RelativePointer<T>& data, void* source_data) -> void
    {
        char* dst = AllocateStatic(sizeof(T));
        data.set(dst);

        if (source_data) {
            MemoryCopy(dst, source_data, sizeof(T));
        }
    }

    template<typename T>
    inline auto Serializer::AllocateAndSet(RelativeArray<T>& data, uint32_t num_elements, void* source_data) -> void
    {
        char* dst = AllocateStatic(sizeof(T) * num_elements);
        data.set(dst,num_elements);

        if (source_data) {
            MemoryCopy(dst, source_data, sizeof(T)* num_elements);
        }
    }

};
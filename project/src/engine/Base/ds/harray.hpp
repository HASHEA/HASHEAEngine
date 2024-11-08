#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include "Base/hmemory.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
namespace HASHEAENGINE
{
    template <typename T>
    class HASHEA_API Array {

    public:
        Array();
        ~Array();

        auto                        Init(Allocator* allocator, uint32_t initial_capacity, uint32_t initial_size = 0) -> HS_Result;
        auto                        Shutdown() -> HS_Result;

        auto                        Push(const T& element) -> HS_Result;
        auto PushUse() -> T&;                 // Grow the size and return T to be filled.

        auto                        Pop() -> HS_Result;
        auto                        DeleteSwap(uint32_t index) -> HS_Result;

        T& operator[](uint32_t index);
        const T& operator[](uint32_t index) const;

        auto                        Clear() -> HS_Result;
        auto                        SetSize(uint32_t new_size) -> HS_Result;
        auto                        SetCapacity(uint32_t new_capacity) -> HS_Result;

        T& Back();
        const T& Back() const;

        T& Front();
        const T& Front() const;

        auto                         SizeInBytes() const -> uint32_t;
        auto                         CapacityInBytes() const -> uint32_t;


      
        uint32_t                         m_uSize = 0;       // Occupied size
        uint32_t                         m_uCapacity = 0;   // Allocated capacity
    private:
        auto                        Grow(uint32_t new_capacity) -> HS_Result;

    private:
        Allocator* m_pAllocator = nullptr;
        T* m_pData = nullptr;
    };


    // ArrayView //////////////////////////////////////////////////////////

    // View over a contiguous memory block.
    template <typename T>
    class ArrayView 
    {
    public:
        ArrayView(T* data, uint32_t size);

        auto Set(T* data, uint32_t size) -> void;

        T& operator[](uint32_t index);
        const T& operator[](uint32_t index) const;

        T* m_pData = nullptr;
        uint32_t                         m_uSize = 0;
    }; // struct ArrayView


    template<typename T>
    inline Array<T>::Array() {
        
    }

    template<typename T>
    inline Array<T>::~Array() {
        
    }

    template<typename T>
    inline auto Array<T>::Init(Allocator* allocator_, uint32_t initial_capacity, uint32_t initial_size)->HS_Result {
        m_pData = nullptr;
        m_uSize = initial_size;
        m_uCapacity = 0;
        m_pAllocator = allocator_;
        HS_Result ret = HS_OK;
        if (initial_capacity > 0) {
            ret = Grow(initial_capacity);
        }
        return ret;
    }

    template<typename T>
    inline auto Array<T>::Shutdown() -> HS_Result {
        if (m_uCapacity > 0) {
            m_pAllocator->Deallocate(m_pData);
        }
        m_pData = nullptr;
        m_uSize = m_uCapacity = 0;
        return HS_OK;
    }

    template<typename T>
    inline auto Array<T>::Push(const T& element) -> HS_Result {
        HS_Result ret = HS_OK;
        if (m_uSize >= m_uCapacity) {
            ret = Grow(m_uCapacity + 1);
        }

        m_pData[size++] = element;
        return HS_OK;
    }
    // push and return back
    template<typename T>
    inline auto Array<T>::PushUse() -> T&{
        HS_Result ret = HS_OK;

        if (m_uSize >= m_uCapacity) {
            ret = Grow(m_uCapacity + 1);
            HS_PROCESS_AND_LOG_RESULT(ret);
        }
        ++m_uSize;
        return Back();
    }

    template<typename T>
    inline auto Array<T>::Pop() ->HS_Result{
        H_ASSERT(m_uSize > 0);
        --m_uSize;
        return HS_OK;
    }

    //no order 
    template<typename T>
    inline auto Array<T>::DeleteSwap(uint32_t index) -> HS_Result {
        H_ASSERT(m_uSize > 0 && index < m_uSize);
        m_pData[index] = m_pData[--m_uSize];
        return HS_OK;
    }

    template<typename T>
    inline auto Array<T>::operator [](uint32_t index) ->T&  {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

    template<typename T>
    inline const T& Array<T>::operator [](uint32_t index) const {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

    template<typename T>
    inline auto Array<T>::Clear() -> HS_Result{
        m_uSize = 0;
        return HS_OK;
    }


    template<typename T>
    inline auto Array<T>::SetSize(uint32_t new_size) -> HS_Result {
        HS_Result ret = HS_OK;

        if (new_size > m_uCapacity) {
            ret = Grow(new_size);
            HS_PROCESS_AND_LOG_RESULT(ret);
        }
        m_uSize = new_size;
        return ret;
    }

    template<typename T>
    inline auto Array<T>::SetCapacity(uint32_t new_capacity) -> HS_Result {
        HS_Result ret = HS_OK;

        if (new_capacity > m_uCapacity) {
            ret = Grow(new_capacity);
            HS_PROCESS_AND_LOG_RESULT(ret);
        }
        return ret;
    }

    template<typename T>
    inline auto Array<T>::Grow(uint32_t new_capacity) ->HS_Result{
        HS_Result ret = HS_OK;

        if (new_capacity <= (m_uCapacity * 1.5f)) {
            new_capacity = m_uCapacity * 1.5f;
        }
        else if (new_capacity < 4) {
            new_capacity = 4;
        }

        T* new_data = (T*)m_pAllocator->Allocate(new_capacity * sizeof(T), alignof(T));
        if (m_uCapacity) {
            MemoryCopy(new_data, m_pData, m_uCapacity * sizeof(T));

            ret = m_pAllocator->Deallocate(m_pData);
            HS_PROCESS_AND_LOG_RESULT(ret);

        }

        m_pData = new_data;
        m_uCapacity = new_capacity;
        return ret;
    }

    template<typename T>
    inline auto Array<T>::Back() ->T& {
        H_ASSERT(m_uSize);
        return m_pData[m_uSize - 1];
    }


    template<typename T>
    inline auto Array<T>::Back() const -> const T& {
        H_ASSERT(m_uSize);
        return m_pData[m_uSize - 1];
    }

    template<typename T>
    inline auto Array<T>::Front() -> T& {
        H_ASSERT(m_uSize);
        return m_pData[0];
    }

    template<typename T>
    inline auto Array<T>::Front() const -> const T& {
        H_ASSERT(m_uSize);
        return m_pData[0];
    }

    template<typename T>
    inline auto Array<T>::SizeInBytes() const -> uint32_t {
        return m_uSize * sizeof(T);
    }

    template<typename T>
    inline auto Array<T>::CapacityInBytes() const -> uint32_t {
        return m_uCapacity * sizeof(T);
    }

    // ArrayView //////////////////////////////////////////////////////////
    template<typename T>
    inline ArrayView<T>::ArrayView(T* data_, uint32_t size_)
        : m_pData(data_), m_uSize(size_) {
    }

    template<typename T>
    inline void ArrayView<T>::Set(T* data_, uint32_t size_) {
        m_pData = data_;
        m_uSize = size_;
    }

    template<typename T>
    inline auto ArrayView<T>::operator[](uint32_t index) -> T& {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

    template<typename T>
    inline auto ArrayView<T>::operator[](uint32_t index) const -> const T& {
        H_ASSERT(index < m_uSize);
        return m_pData[index];
    }

    template<typename T>
    Array<T>* NewArray()
    {
        HS_Result result;
        Array<T>* ptr = nullptr;
        ptr = Hashea_New(nullptr, Array<T>);
        H_ASSERT(ptr);
        result = ptr->Init(&(MemoryService::instance()->GetSystemAllocator()),0,0);
        HS_PROCESS_AND_LOG_RESULT(result);
        H_ASSERT(result == HS_OK);
        return ptr
    };
};

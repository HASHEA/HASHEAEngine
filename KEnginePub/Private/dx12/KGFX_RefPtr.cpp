#include "KGFX_RefPtr.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <algorithm>

namespace gfx
{
    ScopedAllocation::ScopedAllocation() : m_data(nullptr), m_sizeInBytes(0), m_capacityInBytes(0)
    {
    }

    ScopedAllocation::~ScopedAllocation()
    {
        Deallocate();
    }

    void* ScopedAllocation::Allocate(size_t size)
    {
        Deallocate();
        if (size > 0)
        {
            m_data = ::malloc(size);
        }
        m_sizeInBytes = size;
        m_capacityInBytes = size;
        return m_data;
    }

    void* ScopedAllocation::AllocateTerminated(size_t size)
    {
        assert(size != std::numeric_limits<size_t>::max());
        uint8_t* data = static_cast<uint8_t*>(Allocate(size + 1));
        data[size] = 0;
        m_sizeInBytes = size;
        return data;
    }

    void ScopedAllocation::Deallocate()
    {
        if (m_data)
        {
            ::free(m_data);
            m_data = nullptr;
        }
        m_sizeInBytes = 0;
        m_capacityInBytes = 0;
    }

    void ScopedAllocation::Reallocate(size_t capacity)
    {
        if (capacity != m_capacityInBytes)
        {
            m_data = ::realloc(m_data, capacity);
            m_sizeInBytes = capacity;
            m_capacityInBytes = capacity;
        }
    }

    void* ScopedAllocation::Detach()
    {
        void* data = m_data;
        m_data = nullptr;
        m_sizeInBytes = 0;
        m_capacityInBytes = 0;
        return data;
    }

    void ScopedAllocation::Attach(void* data, size_t size)
    {
        Deallocate();
        m_data = data;
        m_sizeInBytes = size;
        m_capacityInBytes = size;
    }

    void* ScopedAllocation::Set(const void* data, size_t size)
    {
        void* dst = Allocate(size);
        if (dst)
        {
            memcpy(dst, data, size);
        }
        return dst;
    }

    void* ScopedAllocation::GetData() const
    {
        return m_data;
    }

    size_t ScopedAllocation::GetSizeInBytes() const
    {
        return m_sizeInBytes;
    }

    size_t ScopedAllocation::GetCapacityInBytes() const
    {
        return m_capacityInBytes;
    }

    void ScopedAllocation::SetSizeInBytes(size_t size)
    {
        assert(size <= m_capacityInBytes);
        m_sizeInBytes = size;
    }

    void ScopedAllocation::Swap(ThisType& rhs) noexcept
    {
        std::swap(m_data, rhs.m_data);
        std::swap(m_sizeInBytes, rhs.m_sizeInBytes);
        std::swap(m_capacityInBytes, rhs.m_capacityInBytes);
    }

    bool ScopedAllocation::IsTerminated() const
    {
        return m_capacityInBytes > m_sizeInBytes && ((const char*)m_data)[m_sizeInBytes] == 0;
    }


    RawBlob::~RawBlob() = default;
    RawBlob::RawBlob() = default;

    void const* RawBlob::GetBufferPointer()
    {
        return m_data.GetData();
    }

    size_t RawBlob::GetBufferSize()
    {
        return m_data.GetSizeInBytes();
    }

    KGFX_IBlob* RawBlob::MoveCreate(ScopedAllocation& alloc)
    {
        RawBlob* blob = new RawBlob;
        blob->m_data.Swap(alloc);
        return (blob);
    }

    KGFX_IBlob* RawBlob::Create(void const* inData, size_t size)
    {
        return (new RawBlob(inData, size));
    }

    RawBlob::RawBlob(const void* data, size_t size)
    {
        memcpy(m_data.AllocateTerminated(size), data, size);
    }

}

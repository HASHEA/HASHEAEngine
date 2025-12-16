#include "KGFX_DescriptorDX12.h"

namespace gfx
{
    DescriptorHeapReference::DescriptorHeapReference(D3D12DescriptorHeap* heap)
    {
        m_HeapType = Type::Linear;
        m_HeapPtr.linearHeap = heap;
    }

    DescriptorHeapReference::DescriptorHeapReference(D3D12GeneralDescriptorHeap* heap)
    {
        m_HeapType = Type::General;
        m_HeapPtr.generalHeap = heap;
    }

    DescriptorHeapReference::DescriptorHeapReference(D3D12GeneralExpandingDescriptorHeap* heap)
    {
        m_HeapType = Type::ExpandingGeneral;
        m_HeapPtr.generalExpandingHeap = heap;
    }

    DescriptorHeapReference::DescriptorHeapReference(D3D12LinearExpandingDescriptorHeap* heap)
    {
        m_HeapType = Type::ExpandingLinear;
        m_HeapPtr.linearExpandingHeap = heap;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapReference::GetCpuHandle(int index) const
    {
        switch (m_HeapType)
        {
        case Type::Linear:
            return m_HeapPtr.linearHeap->GetCpuHandle(index);
        case Type::General:
            return m_HeapPtr.generalHeap->GetCpuHandle(index);
        case Type::ExpandingGeneral:
            return m_HeapPtr.generalExpandingHeap->GetCpuHandle(index);
        case Type::ExpandingLinear:
            return m_HeapPtr.linearExpandingHeap->GetCpuHandle(index);
        default:
            return D3D12_CPU_DESCRIPTOR_HANDLE();
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapReference::GetGpuHandle(int index) const
    {
        switch (m_HeapType)
        {
        case Type::Linear:
            return m_HeapPtr.linearHeap->GetGpuHandle(index);
        case Type::General:
            return m_HeapPtr.generalHeap->GetGpuHandle(index);
        case Type::ExpandingGeneral:
            return m_HeapPtr.generalExpandingHeap->GetGpuHandle(index);
        default:
            return D3D12_GPU_DESCRIPTOR_HANDLE();
        }
    }

    int DescriptorHeapReference::Allocate(int numDescriptors) const
    {
        switch (m_HeapType)
        {
        case Type::Linear:
            return m_HeapPtr.linearHeap->Allocate(numDescriptors);
        case Type::General:
            return m_HeapPtr.generalHeap->Allocate(numDescriptors);
        case Type::ExpandingGeneral:
            return m_HeapPtr.generalExpandingHeap->Allocate(numDescriptors);
        default:
            return m_HeapPtr.linearExpandingHeap->Allocate(numDescriptors);
        }
    }

    void DescriptorHeapReference::Free(int index, int count) const
    {
        switch (m_HeapType)
        {
        default:
        case Type::Linear:
            assert(false);
            break;
        case Type::General:
            return m_HeapPtr.generalHeap->Free(index, count);
        case Type::ExpandingGeneral:
            return m_HeapPtr.generalExpandingHeap->Free(index, count);
        }
    }

    void DescriptorHeapReference::FreeIfSupported(int index, int count) const
    {
        switch (m_HeapType)
        {
        case Type::Linear:
            return;
        case Type::General:
            return m_HeapPtr.generalHeap->Free(index, count);
        case Type::ExpandingGeneral:
            return m_HeapPtr.generalExpandingHeap->Free(index, count);
        default:
            break;
        }
    }

    uint32_t DescriptorTable::GetDescriptorCount() const
    {
        return m_DescriptorCount;
    }

    void DescriptorTable::SetHeapReference(DescriptorHeapReference other)
    {
        m_Heap = other;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorTable::GetGpuHandle(uint32_t index) const
    {
        assert(index < GetDescriptorCount());
        return m_Heap.GetGpuHandle(m_Offset + index);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorTable::GetCpuHandle(uint32_t index) const
    {
        assert(index < GetDescriptorCount());
        return m_Heap.GetCpuHandle(m_Offset + index);
    }

    void DescriptorTable::FreeIfSupported()
    {
        if (m_DescriptorCount)
        {
            m_Heap.FreeIfSupported(m_Offset, m_DescriptorCount);
            m_Offset = 0;
            m_DescriptorCount = 0;
        }
    }

    bool DescriptorTable::Allocate(uint32_t count)
    {
        auto allocatedOffset = m_Heap.Allocate(count);
        if (allocatedOffset == -1)
            return false;
        m_Offset = allocatedOffset;
        m_DescriptorCount = count;
        return true;
    }

    bool DescriptorTable::Allocate(DescriptorHeapReference heap, uint32_t count)
    {
        auto allocatedOffset = heap.Allocate(count);
        if (allocatedOffset == -1)
            return false;
        m_Heap = heap;
        m_Offset = allocatedOffset;
        m_DescriptorCount = count;
        return true;
    }
}

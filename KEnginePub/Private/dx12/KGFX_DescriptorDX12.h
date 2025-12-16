#pragma once
#include <cstdint>
#include "KGFX_DescriptorHeapDX12.h"

namespace gfx
{
    struct DescriptorHeapReference
    {
        DescriptorHeapReference() = default;
        DescriptorHeapReference(D3D12DescriptorHeap* heap);

        DescriptorHeapReference(D3D12GeneralDescriptorHeap* heap);

        DescriptorHeapReference(D3D12GeneralExpandingDescriptorHeap* heap);

        DescriptorHeapReference(D3D12LinearExpandingDescriptorHeap* heap);

        enum class Type
        {
            Linear,
            General,
            ExpandingGeneral,
            ExpandingLinear
        };

        union Ptr
        {
            D3D12DescriptorHeap* linearHeap = nullptr;
            D3D12GeneralDescriptorHeap* generalHeap;
            D3D12GeneralExpandingDescriptorHeap* generalExpandingHeap;
            D3D12LinearExpandingDescriptorHeap* linearExpandingHeap;
        };

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int index) const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int index) const;

        int Allocate(int numDescriptors) const;

        void Free(int index, int count) const;

        void FreeIfSupported(int index, int count) const;

        operator bool() const
        {
            return m_HeapPtr.linearHeap != nullptr;
        }

    private:
        Type m_HeapType = {};

        Ptr m_HeapPtr = {};
    };

    /**
     * 表示DX12中一系列连续的描述符，可以是资源描述符，也可以是采样器描述符，可以是CPU可见的，也可以是GPU可见的
     * CPU可见的用于缓存shader descriptorSet参数
     * GPU可见的仅在提交时使用，不可以緩存	
     */
    struct DescriptorTable
    {
        DescriptorTable() = default;

        uint32_t GetDescriptorCount() const;

        void SetHeapReference(DescriptorHeapReference other);

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t index = 0) const;

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(uint32_t index = 0) const;

        void FreeIfSupported();

        bool Allocate(uint32_t count);

        bool Allocate(DescriptorHeapReference heap, uint32_t count);

        DescriptorHeapReference m_Heap = {};
        uint32_t m_Offset = 0;
        uint32_t m_DescriptorCount = 0;
    };
}

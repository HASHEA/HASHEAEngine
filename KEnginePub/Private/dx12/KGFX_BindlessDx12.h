#pragma once
#include "KGFX_DescriptorHeapDX12.h"
#include "KGFX_Dx12Header.h"

namespace gfx
{
    class D3D12BindlessDescriptorHeap
    {
    public:
        D3D12BindlessDescriptorHeap() = default;
        ~D3D12BindlessDescriptorHeap();

        bool Init(BindlessConfiguration Configuration, BindlessHeapType HeapType);
        int Allocate(uint32_t Num = 1);
        void Free(int Index, uint32_t Num = 1);

        bool HasType(BindlessHeapType HeapType) { return HeapType == m_HeapType; }
        bool HasConfiguration(BindlessConfiguration Configuration) { return Configuration == m_Configuration; }
        bool HandleAllocate(BindlessHeapType HeapType, BindlessConfiguration Configuration) { return HeapType == m_HeapType && Configuration == m_Configuration; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int Index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int Index) const;
        ID3D12DescriptorHeap* GetGPUHeap() { return m_BindlessGPHeap.GetHeap(); }
        
    private:
        // really need cpu cache?
        D3D12GeneralDescriptorHeap m_BindlessCPUHeap;
        D3D12GeneralDescriptorHeap m_BindlessGPHeap;
        BindlessConfiguration m_Configuration;
        BindlessHeapType m_HeapType;
    };

    class D3D12BindlessDescriptorHeapManager
    {
    public:
        D3D12BindlessDescriptorHeapManager() = default;
        ~D3D12BindlessDescriptorHeapManager();

        bool Init();
        int Allocate(BindlessHeapType Type, uint32_t Num = 1) const;
        void Free(BindlessHeapType Type, int Index, uint32_t Num = 1) const;
        void CopyDescriptorToBindless(BindlessDescriptor Descriptor, D3D12_CPU_DESCRIPTOR_HANDLE SrcHandle) const;
        bool HasHeap(BindlessHeapType Type, BindlessConfiguration Configuration) const;
        D3D12BindlessDescriptorHeap* GetHeap(BindlessHeapType Type, BindlessConfiguration Configuration) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(BindlessHeapType Type, int Index) const;

    private:
        std::vector< D3D12BindlessDescriptorHeap* > m_vecBindlesDescriptorHeaps;
    };
}

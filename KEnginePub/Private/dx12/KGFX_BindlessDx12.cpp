#include "KGFX_GraphiceDeviceDX12.h"
#include "KGFX_BindlessDx12.h"


namespace gfx
{
    int BindlessResourceDescriptorHeapSize = 1000 * 1000;
    int BindlessSamplerDescriptorHeapSize = 2048;

    static BindlessConfiguration GetBindlessConfigguration()
    {
        //should according engine config

        BindlessConfiguration Configuration = BindlessConfiguration::RayTracingShader;
        return Configuration;
    }

    D3D12_DESCRIPTOR_HEAP_TYPE TransitionHeapType(BindlessHeapType HeapType)
    {
        switch (HeapType)
        {
        case BindlessHeapType::Standard:
            return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        case BindlessHeapType::Sampler:
            return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        case BindlessHeapType::RenderTarget:
            return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        case BindlessHeapType::DepthStencil:
            return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        default:
            return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        }
    }

    D3D12BindlessDescriptorHeap::~D3D12BindlessDescriptorHeap() = default;

    bool D3D12BindlessDescriptorHeap::Init(BindlessConfiguration Configuration, BindlessHeapType HeapType)
    {
        bool bResult = false;
        bool bRetCode = false;

        assert(HeapType == BindlessHeapType::Standard || HeapType == BindlessHeapType::Sampler);

        int Size = HeapType == BindlessHeapType::Standard ? BindlessResourceDescriptorHeapSize : BindlessSamplerDescriptorHeapSize;
        D3D12_DESCRIPTOR_HEAP_TYPE Type = HeapType == BindlessHeapType::Standard ? D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV : D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

        bRetCode = m_BindlessCPUHeap.Init(Size, Type, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        KGLOG_ASSERT_EXIT(bRetCode);

        bRetCode = m_BindlessGPHeap.Init(Size, Type, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        KGLOG_ASSERT_EXIT(bRetCode);

        m_Configuration = Configuration;
        m_HeapType = HeapType;

        bResult = true;
    Exit0:
        return bResult;
    }

    int D3D12BindlessDescriptorHeap::Allocate(uint32_t Num)
    {
        return m_BindlessGPHeap.Allocate(Num);
    }

    void D3D12BindlessDescriptorHeap::Free(int Index, uint32_t Num)
    {
        m_BindlessGPHeap.Free(Index, Num);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12BindlessDescriptorHeap::GetCpuHandle(int Index) const
    {
        return m_BindlessGPHeap.GetCpuHandle(Index);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12BindlessDescriptorHeap::GetGpuHandle(int Index) const
    {
        return m_BindlessGPHeap.GetGpuHandle(Index);
    }

    D3D12BindlessDescriptorHeapManager::~D3D12BindlessDescriptorHeapManager()
    {
        auto Iter = m_vecBindlesDescriptorHeaps.begin();
        while (Iter != m_vecBindlesDescriptorHeaps.end())
        {
            delete *Iter;
            Iter = m_vecBindlesDescriptorHeaps.erase(Iter);
        }

        m_vecBindlesDescriptorHeaps.clear();
    };

    bool D3D12BindlessDescriptorHeapManager::Init()
    {
        bool bResult = false;
        bool bRetCode = false;

        BindlessConfiguration Configuration = GetBindlessConfigguration();
        m_vecBindlesDescriptorHeaps.emplace_back(new D3D12BindlessDescriptorHeap());
        bRetCode = m_vecBindlesDescriptorHeaps[0]->Init(Configuration, BindlessHeapType::Standard);
        KGLOG_ASSERT_EXIT(bRetCode);

        m_vecBindlesDescriptorHeaps.emplace_back(new D3D12BindlessDescriptorHeap());
        bRetCode = m_vecBindlesDescriptorHeaps[1]->Init(Configuration, BindlessHeapType::Sampler);
        KGLOG_ASSERT_EXIT(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    

    int D3D12BindlessDescriptorHeapManager::Allocate(BindlessHeapType Type, uint32_t Num) const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_vecBindlesDescriptorHeaps.size()); i++)
        {
            if (m_vecBindlesDescriptorHeaps[i]->HasType(Type))
            {
                return m_vecBindlesDescriptorHeaps[i]->Allocate(Num);
            }
        }

        return -1;
    }

    void D3D12BindlessDescriptorHeapManager::Free(BindlessHeapType Type, int Index, uint32_t Num) const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_vecBindlesDescriptorHeaps.size()); ++i)
        {
            if (m_vecBindlesDescriptorHeaps[i]->HasType(Type))
            {
                m_vecBindlesDescriptorHeaps[i]->Free(Index, Num);
                return;
            }
        }
    }

    void D3D12BindlessDescriptorHeapManager::CopyDescriptorToBindless(BindlessDescriptor Descriptor, D3D12_CPU_DESCRIPTOR_HANDLE SrcHandle) const
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        D3D12_CPU_DESCRIPTOR_HANDLE DstHandle{};
        uint32_t CopyDst = Descriptor.Index;

        for (uint32_t i = 0; i < m_vecBindlesDescriptorHeaps.size(); i++)
        {
            if (m_vecBindlesDescriptorHeaps[i]->HasType(Descriptor.Type))
            {
                DstHandle = m_vecBindlesDescriptorHeaps[i]->GetCpuHandle(CopyDst);
                if (SrcHandle.ptr != 0)
                {
                    pD3dDevice->CopyDescriptorsSimple(1, DstHandle, SrcHandle, TransitionHeapType(Descriptor.Type));
                    return;
                }
            }
        }
    }

    bool D3D12BindlessDescriptorHeapManager::HasHeap(BindlessHeapType Type, BindlessConfiguration Configuration) const
    {
        for (uint32_t i = 0; i < m_vecBindlesDescriptorHeaps.size(); i++)
        {
            if (m_vecBindlesDescriptorHeaps[i]->HandleAllocate(Type, Configuration))
            {
                return true;
            }
        }

        return false;
    }

    D3D12BindlessDescriptorHeap* D3D12BindlessDescriptorHeapManager::GetHeap(BindlessHeapType Type, BindlessConfiguration Configuration) const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_vecBindlesDescriptorHeaps.size()); ++i)
        {
            if (m_vecBindlesDescriptorHeaps[i]->HandleAllocate(Type, Configuration))
            {
                return m_vecBindlesDescriptorHeaps[i];
            }
        }

        return nullptr;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12BindlessDescriptorHeapManager::GetGPUHandle(BindlessHeapType Type, int Index) const
    {
        D3D12BindlessDescriptorHeap* pHeap = GetHeap(Type, BindlessConfiguration::RayTracingShader);
        assert(pHeap != nullptr);

        return pHeap->GetGpuHandle(Index);
    }
}

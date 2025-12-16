#include "KGFX_SamplerDX12.h"
#include "KGFX_GraphiceDeviceDx12.h"

namespace gfx
{

    std::unordered_map<const_pool_str, KGFX_SamplerDX12*> KGFX_SamplerDX12::g_AllSamplerPool = {};

    KGFX_SamplerDX12::~KGFX_SamplerDX12()
    {
        SAFE_DELETE(m_pSamplerBindlesView);
    }

    const KSamplerState& KGFX_SamplerDX12::GetSamplerState()
    {
        return m_SamplerState;
    }


    void KGFX_SamplerDX12::Init(D3D12Descriptor handle, const KSamplerState& samplerState)
    {
        m_D3d12Descriptor = handle;
        m_SamplerState = samplerState;
    }

    std::unordered_map<const_pool_str, KGFX_SamplerDX12*>& KGFX_SamplerDX12::GetSamplerPool()
    {
        return  g_AllSamplerPool;
    }

    uintptr_t KGFX_SamplerDX12::GetNativeHandle()
    {
        return m_D3d12Descriptor.cpuHandle.ptr;
    }

    void KGFX_SamplerDX12::ClearSamplerPool()
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
       
        for (auto& sampler : g_AllSamplerPool)
        {
            pGraphicDevice->GetDX12SamplerHeap()->Free(sampler.second->m_D3d12Descriptor);
            SAFE_DELETE(sampler.second);
        }
    }
    IKGFX_SamplerBindlessView* KGFX_SamplerDX12::GetBindlessView()
    {
        if(m_pSamplerBindlesView == nullptr)
            m_pSamplerBindlesView = new KGFX_SamplerBindlessViewDX12(this);
        return m_pSamplerBindlesView;
    }

    KGFX_SamplerBindlessViewDX12::KGFX_SamplerBindlessViewDX12(KGFX_SamplerDX12* pSampler)
    {
        m_Sampler = pSampler;
    }

    KGFX_SamplerBindlessViewDX12::~KGFX_SamplerBindlessViewDX12()
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12BindlessDescriptorHeapManager* pBindlessManager = pGraphicDevice->GetDX12BindlessHeapManager();

        if (pBindlessManager)
        {
            if (m_BindlessDescriptor.IsValid())
            {
                pBindlessManager->Free(m_BindlessDescriptor.Type, m_BindlessDescriptor.Index);
            }
            m_BindlessDescriptor = {};
        }
    }

    uint32_t KGFX_SamplerBindlessViewDX12::GetBindlessHandle()
    {
        if (m_BindlessDescriptor.IsValid())
        {
            return m_BindlessDescriptor.Index;
        }

        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12BindlessDescriptorHeapManager* pBindlessManager = pGraphicDevice->GetDX12BindlessHeapManager();
        assert(pBindlessManager != nullptr);

        uint32_t BindlessIndex = pBindlessManager->Allocate(BindlessHeapType::Sampler);
        m_BindlessDescriptor.Index = BindlessIndex;
        m_BindlessDescriptor.Type = BindlessHeapType::Sampler;

        return BindlessIndex;
    }
    const KSamplerState& KGFX_SamplerBindlessViewDX12::GetSamplerState()
    {
        return GetResource()->GetSamplerState();
    }
    IKGFX_Sampler* KGFX_SamplerBindlessViewDX12::GetResource()
    {
        return m_Sampler;
    }
}

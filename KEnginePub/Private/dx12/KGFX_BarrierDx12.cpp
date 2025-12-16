#ifdef _WIN32
#include "KGFX_BarrierDx12.h"
#include "KGFX_TextureImplDx12.h"
#include "KGFX_BufferImplDX12.h"

namespace gfx
{
    KGFX_BarrierTrackerDx12::~KGFX_BarrierTrackerDx12()
    {
        Reset();
    }

    void KGFX_BarrierTrackerDx12::Reset()
    {
        for (auto& subBarrier : m_vecResourceBarriers)
        {
            KGFX_BufferImplDX12* transBuf = nullptr;
            IKGFX_TextureResource* transTex = nullptr;

            uint32_t subResIndex = subBarrier.barrier.Transition.Subresource;
            subBarrier.res.VisitBindedResource(transBuf, transTex);

            if (transBuf)
            {
                KGFX_BufferImplDX12* transBufImpl = (transBuf);
                transBufImpl->GetLayoutTracker().SetSubResBitSet();
            }

            if (transTex)
            {
                KGFX_TextureImplDx12* transTexImpl = static_cast<KGFX_TextureImplDx12*>(transTex);
                transTexImpl->GetLayoutTracker().SetSubResBitSet(subResIndex);
            }
        }
        m_vecSubmitResourceBarriers.clear();
        m_vecResourceBarriers.clear();
    }

    void KGFX_BarrierTrackerDx12::CommitAllBarrier(ID3D12GraphicsCommandList* pCmdList)
    {
        m_vecSubmitResourceBarriers.clear();

        for (const auto& trans : m_vecResourceBarriers)
        {
            m_vecSubmitResourceBarriers.emplace_back(trans.barrier);
        }

        if (!m_vecSubmitResourceBarriers.empty())
        {
            pCmdList->ResourceBarrier(static_cast<uint32_t>(m_vecSubmitResourceBarriers.size()), m_vecSubmitResourceBarriers.data());
            ClearAndWriteStateToRes();
        }

    }

    void KGFX_BarrierTrackerDx12::ClearAndWriteStateToRes()
    {
        for (const auto& trans : m_vecResourceBarriers)
        {

            TranslateResource transRes = trans.res;
            KGFX_BufferImplDX12* transBuf = nullptr;
            IKGFX_TextureResource* transTex = nullptr;

            auto aftState = trans.barrier.Transition.StateAfter;
            uint32_t subResIndex = trans.barrier.Transition.Subresource;
            transRes.VisitBindedResource(transBuf, transTex);

            if (transBuf)
            {
                KGFX_BufferImplDX12* transBufImpl = (transBuf);
                if (trans.barrier.Type == D3D12_RESOURCE_BARRIER_TYPE::D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                {
                    transBufImpl->GetLayoutTracker().SetSubresourceState(aftState, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
                }
                transBufImpl->GetLayoutTracker().SetSubResBitSet();
            }

            if (transTex)
            {
                KGFX_TextureImplDx12* transTexImpl = static_cast<KGFX_TextureImplDx12*>(transTex);
                if (trans.barrier.Type == D3D12_RESOURCE_BARRIER_TYPE::D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                {
                    transTexImpl->GetLayoutTracker().SetSubresourceState(aftState, subResIndex);
                }
                transTexImpl->GetLayoutTracker().SetSubResBitSet(subResIndex);
            }
        }
        m_vecResourceBarriers.clear();
    }


    void KGFX_BarrierTrackerDx12::ResourceBarrier(const KD3DX12_RESOURCE_BARRIER& barrier, IKGFX_TextureResource* texImpl)
    {
        auto& layoutTracker = ((KGFX_TextureImplDx12*)texImpl)->GetLayoutTracker();

        if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
        {
            const D3D12_RESOURCE_TRANSITION_BARRIER& transitionBarrier = barrier.Transition;
            if (layoutTracker.GetSubResBitSet(transitionBarrier.Subresource))
            {
                return;
            }
            layoutTracker.SetSubResBitSet(transitionBarrier.Subresource, true);
            m_vecResourceBarriers.emplace_back(TranresSubBarrier{ texImpl ,barrier });
        }
        else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
        {
            if (layoutTracker.GetSubResBitSet(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES))
            {
                return;
            }
            layoutTracker.SetSubResBitSet(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true);
            m_vecResourceBarriers.emplace_back(TranresSubBarrier{ texImpl ,barrier });
        }
    }

    void KGFX_BarrierTrackerDx12::ResourceBarrier(const KD3DX12_RESOURCE_BARRIER& barrier, KGFX_BufferImplDX12* bufImpl)
    {
        auto& layoutTracker = bufImpl->GetLayoutTracker();

        if (layoutTracker.GetSubResBitSet(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES))
        {
            //assert(false);
            return;
        }

        layoutTracker.SetSubResBitSet(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true);
        m_vecResourceBarriers.emplace_back(TranresSubBarrier{ bufImpl ,barrier });
    }
}

#endif

#ifdef _WIN32
#include "KGFX_RenderFrameBufferDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KBase/Public/KMemLeak.h"
#include "KGFX_RenderTargetDx12.h"

namespace gfx
{
    KGFX_RenderFrameBufferDx12::KGFX_RenderFrameBufferDx12(const KGfxFrameBufferDesc& fbDesc)
    {
        m_FBDesc = fbDesc;
        uint32_t rtWidth = 0;
        uint32_t rtHeight = 0;

        for (auto& eachRTV : fbDesc.vecFramebufferRTVDesc)
        {
            eachRTV.pTargetView->AddRef();

            rtWidth = std::max(eachRTV.pTargetView->GetResource()->GetDesc()->uWidth, rtWidth);
            rtHeight = std::max(eachRTV.pTargetView->GetResource()->GetDesc()->uHeight, rtHeight);
        }

        if (fbDesc.DSVDesc.pTargetView)
        {
            fbDesc.DSVDesc.pTargetView->AddRef();
            rtWidth = std::max(fbDesc.DSVDesc.pTargetView->GetResource()->GetDesc()->uWidth, rtWidth);
            rtHeight = std::max(fbDesc.DSVDesc.pTargetView->GetResource()->GetDesc()->uHeight, rtHeight);
        }

        assert(rtWidth < 10000);
        assert(rtHeight < 10000);

        m_uWidth = rtWidth;
        ;
        m_uHeight = rtHeight;
    }

    KGFX_RenderFrameBufferDx12::~KGFX_RenderFrameBufferDx12()
    {
        for (auto& eachRTV : m_FBDesc.vecFramebufferRTVDesc)
        {
            SAFE_RELEASE(eachRTV.pTargetView);
        }
        SAFE_RELEASE(m_FBDesc.DSVDesc.pTargetView);

        m_FBDesc = {};
    }

    int KGFX_RenderFrameBufferDx12::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            delete this;
        }
        return nRef;
    }

    uint32_t KGFX_RenderFrameBufferDx12::GetWidth() const
    {
        return m_uWidth;
    }

    uint32_t KGFX_RenderFrameBufferDx12::GetHeight() const
    {
        return m_uHeight;
    }

    void KGFX_RenderFrameBufferDx12::SetObjectName(const char* szName)
    {
        m_FBName = szName;
    }

    uint32_t KGFX_RenderFrameBufferDx12::GetRenderTargetCount() const
    {
        return static_cast<uint32_t>(m_FBDesc.vecFramebufferRTVDesc.size());
    }


    const KGfxFrameBufferDesc& KGFX_RenderFrameBufferDx12::GetFBDesc() const
    {
        return m_FBDesc;
    }

    BOOL KGFX_RenderFrameBufferDx12::BeginPass(IKGFX_RenderContext* pRenderCtx, bool bUseBarrier, bool bImmediateMode)
    {
        KGFX_CommandBufferDX12Impl* pctxDX12 = dynamic_cast<KGFX_CommandBufferDX12Impl*>(pRenderCtx);

        uint32_t                                 uColorRTCount = (uint32_t)m_FBDesc.vecFramebufferRTVDesc.size();
        D3D12_CPU_DESCRIPTOR_HANDLE              pDepthView = {};
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> RtViews = {};
        RtViews.resize(uColorRTCount);

        pctxDX12->SetCurrentFrameBuffer(this);

        for (int i = 0; i < (int)uColorRTCount; i++)
        {
            RtViews.at(i).ptr = m_FBDesc.vecFramebufferRTVDesc.at(i).pTargetView->GetNativeHandle();
            auto transSubRes = m_FBDesc.vecFramebufferRTVDesc.at(i).pTargetView->GetViewDesc().sSubresourceRange;
            pctxDX12->Transition({ m_FBDesc.vecFramebufferRTVDesc.at(i).pTargetView->GetResource(), KGfxAccess::Unknown, KGfxAccess::RTV, transSubRes });
        }

        if (m_FBDesc.DSVDesc.pTargetView)
        {
            pDepthView.ptr = m_FBDesc.DSVDesc.pTargetView->GetNativeHandle();
            auto transSubRes = m_FBDesc.DSVDesc.pTargetView->GetViewDesc().sSubresourceRange;
            pctxDX12->Transition({ m_FBDesc.DSVDesc.pTargetView->GetResource(), KGfxAccess::Unknown, KGfxAccess::DSVWrite, transSubRes });
        }



        for (uint32_t i = 0; i < uColorRTCount; ++i)
        {
            enumLoadActionType& type = m_FBDesc.vecFramebufferRTVDesc.at(i).eLoadActions;
            if (type == LOAD_ACTION_CLEAR)
            {
                KClearValue clearValue = m_FBDesc.vecFramebufferRTVDesc.at(i).ClearValue;
                pctxDX12->CmdClearTextureView(m_FBDesc.vecFramebufferRTVDesc.at(i).pTargetView, clearValue, KGFX_ClearResourceViewFlags::None);
            }
        }

        if (m_FBDesc.DSVDesc.pTargetView && m_FBDesc.DSVDesc.eLoadActions != LOAD_ACTION_LOAD)
        {
            KClearValue clearValue = m_FBDesc.DSVDesc.ClearValue;
            KGFX_ClearResourceViewFlags clearFlags = KGFX_ClearResourceViewFlags::None;
            if (m_FBDesc.DSVDesc.eLoadActions != LOAD_ACTION_LOAD)
            {
                clearFlags |= KGFX_ClearResourceViewFlags::ClearDepth;
            }

            if (m_FBDesc.DSVDesc.eStencilLoadActions != LOAD_ACTION_LOAD)
            {
                clearFlags |= KGFX_ClearResourceViewFlags::ClearStencil;
            }

            pctxDX12->CmdClearTextureView(m_FBDesc.DSVDesc.pTargetView, clearValue, clearFlags);
        }


        if (pDepthView.ptr != 0)
        {
            pctxDX12->GetD3D12CommandList()->OMSetRenderTargets(uColorRTCount, RtViews.data(), false, &pDepthView);
        }
        else
        {
            pctxDX12->GetD3D12CommandList()->OMSetRenderTargets(uColorRTCount, RtViews.data(), false, nullptr);
        }


        pctxDX12->CmdSetScissor(this);
        pctxDX12->CmdSetViewport(this);
        pctxDX12->BeginRenderPass();
        return true;
    }

    BOOL KGFX_RenderFrameBufferDx12::EndPass(IKGFX_RenderContext* pRenderCtx)
    {
        KGFX_CommandBufferDX12Impl* pctxDX12 = dynamic_cast<KGFX_CommandBufferDX12Impl*>(pRenderCtx);
        pctxDX12->FlushResourceBarriers();
        ASSERT(pctxDX12->GetCurrentFrameBuffer());
        pctxDX12->SetCurrentFrameBuffer(nullptr);
        pctxDX12->EndRenderPass();
        return true;
    }
} // namespace gfx

#endif

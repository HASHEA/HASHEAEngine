#include "KVulkanRenderFrameBuffer.h"
#include "KVulkanDevice.h"
#include "KVulkanInitializers.h"
#include "KVulkanRenderContext.h"
#include "KGFX_GraphicDeviceVK.h"

#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    std::atomic<uint32_t> g_FrameBufferId{ 0 };

    KVulkanRenderFrameBuffer::KVulkanRenderFrameBuffer()
    {
        PROF_CPU();
        m_uWidth = 0;
        m_uHeight = 0;
        m_pFramebuffer = VK_NULL_HANDLE;
        m_uArraySize = 0;
        m_bUseCustomViewport = FALSE;
        memset(&m_cmdBufInfo, 0, sizeof(m_cmdBufInfo));
        memset(&m_renderPassBeginInfo, 0, sizeof(m_renderPassBeginInfo));
        memset(&m_viewport, 0, sizeof(m_viewport));
        memset(&m_scissor, 0, sizeof(m_scissor));
        memset(&m_CustomViewport, 0, sizeof(m_CustomViewport));
        memset(&m_CustomScissor, 0, sizeof(m_CustomScissor));

#if memlect_RenderFrameBuffer
        static uint32_t uCounter = 0;
        uCounter++;
        m_pMemLectBuffer = new uint8_t[uCounter];
        if (uCounter == 178)
        {
            int x = 0;
        }
#endif

        m_uid = ++g_FrameBufferId;
    }

    KVulkanRenderFrameBuffer::~KVulkanRenderFrameBuffer()
    {
        PROF_CPU();

        {
            VkDevice pVkDevice = GetVkDevice();
            ASSERT(pVkDevice);
            if (m_pFramebuffer)
            {
                vks::vkDestroyFramebuffer(pVkDevice, m_pFramebuffer, nullptr);
                m_pFramebuffer = VK_NULL_HANDLE;
            }
        }
#if memlect_RenderFrameBuffer
        SAFE_DELETE_ARRAY(m_pMemLectBuffer);
#endif
    }

    int KVulkanRenderFrameBuffer::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            auto piDevice = KGFX_GetGraphicDeviceVKInternal();
            CHECK_ASSERT(piDevice);

            piDevice->GC_DelayReleaseObject(this);
        }
        return nRef;
    }

    BOOL KVulkanRenderFrameBuffer::Create(const KGfxFrameBufferDesc* pDesc)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;
        KVulkanRenderPass* pRetRenderPass = nullptr;
        KRenderPassDesc renderPassDesc = {};
        std::vector<VkImageView> views;
        VkFramebufferCreateInfo  fbufCreateInfo;
        uint32_t uAttachmentCount = 0;

        m_BeginPassLayoutTrans.clear();
        m_clearValues.clear();

        if (pDesc->vecFramebufferRTVDesc.empty() && pDesc->DSVDesc.pTargetView == nullptr)
        {
            m_uWidth = pDesc->uWidth;
            m_uHeight = pDesc->uHeight;
        }
        else if (pDesc->uWidth == 0 && pDesc->uHeight == 0)
        {
            const KGFX_TextureDesc* texDesc = nullptr;
            if (pDesc->DSVDesc.pTargetView)
            {
                auto pDSV = pDesc->DSVDesc.pTargetView;
                CHECK_ASSERT(pDSV);

                auto pResource = pDSV->GetResource();
                CHECK_ASSERT(pResource);

                texDesc = pResource->GetDesc();
            }
            else if (!pDesc->vecFramebufferRTVDesc.empty())
            {
                auto pRTV = pDesc->vecFramebufferRTVDesc[0].pTargetView;
                CHECK_ASSERT(pRTV);

                auto pResource = pRTV->GetResource();
                CHECK_ASSERT(pResource);

                texDesc = pResource->GetDesc();
            }

            CHECK_ASSERT(texDesc);

            m_uWidth = texDesc->uWidth;
            m_uHeight = texDesc->uHeight;
        }
        else
        {
            m_uWidth = pDesc->uWidth;
            m_uHeight = pDesc->uHeight;
        }

        CHECK_ASSERT(m_uWidth != 0 && m_uHeight != 0);

        m_uRenderTargetNum = (uint16_t)pDesc->vecFramebufferRTVDesc.size();

        uAttachmentCount = m_uRenderTargetNum + (pDesc->DSVDesc.pTargetView != nullptr ? 1 : 0);
        views.reserve(uAttachmentCount);

        if (pDesc)
        {
            for (const auto& iter : pDesc->vecFramebufferRTVDesc)
            {
                auto pRTV = iter.pTargetView;
                CHECK_ASSERT(pRTV && pRTV->GetViewDesc().eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV);

                auto pResource = pRTV->GetResource();
                CHECK_ASSERT(pResource);

                auto texDesc = pResource->GetDesc();

                renderPassDesc.vecColorFormats.push_back(texDesc->eFormat);
                renderPassDesc.vecColorInitImageLayout.push_back(IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                renderPassDesc.vecColorFinalImageLayout.push_back(IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                renderPassDesc.vecLoadActionsColor.push_back(iter.eLoadActions);
                renderPassDesc.vecSampleCount.push_back(texDesc->eSampleCount);

                m_BeginPassLayoutTrans.emplace_back(KGfxBarrier(pRTV, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::RTV));

                VkClearValue value = {};
                const KClearValue& v = iter.ClearValue;
                value.color = { v.r, v.g, v.b, v.a };
                m_clearValues.emplace_back(value);

                views.push_back((VkImageView)pRTV->GetNativeHandle());
            }

            renderPassDesc.uRenderTargetCount = (uint32_t)pDesc->vecFramebufferRTVDesc.size();

            if (pDesc->DSVDesc.pTargetView != nullptr)
            {
                auto pDSV = pDesc->DSVDesc.pTargetView;
                CHECK_ASSERT(pDSV && pDSV->GetViewDesc().eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV);

                auto pResource = pDSV->GetResource();
                CHECK_ASSERT(pResource);

                auto texDesc = pResource->GetDesc();

                renderPassDesc.depthInitLayout = pDesc->bDSVReadOnly ? IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                renderPassDesc.depthfinalLayout = pDesc->bDSVReadOnly ? IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                renderPassDesc.depthSampleCount = texDesc->eSampleCount;
                renderPassDesc.enuDepthStencilFormat = texDesc->eFormat;
                renderPassDesc.enuLoadActionDepth = pDesc->DSVDesc.eLoadActions;
                renderPassDesc.enuLoadActionStencil = pDesc->DSVDesc.eStencilLoadActions;
                renderPassDesc.enuStoreActionDepth = STORE_ACTION_STORE;
                renderPassDesc.enuStoreActionStencil = STORE_ACTION_STORE;
                renderPassDesc.bDepthReadOnly = pDesc->bDSVReadOnly;

                m_BeginPassLayoutTrans.emplace_back(KGfxBarrier(pDSV, gfx::KGfxAccess::Unknown, pDesc->bDSVReadOnly ? KGfxAccess::DSVRead : gfx::KGfxAccess::DSVWrite));

                VkClearValue value = {};
                const KClearValue& v = pDesc->DSVDesc.ClearValue;
                value.depthStencil = { v.depth, v.stencil };
                m_clearValues.emplace_back(value);

                views.push_back((VkImageView)pDSV->GetNativeHandle());
            }
            else
            {
                renderPassDesc.enuDepthStencilFormat = enumTextureFormat::TEX_FORMAT_NONE;
            }
        }

        {
            KGFX_GraphicDeviceVK* pKGFXGraphicDevice = KGFX_GetGraphicDeviceVKInternal();
            VkDevice pDevice = GetVkDevice();
            VkResult hRetCode;

            bRetCode = pKGFXGraphicDevice->CreateRenderPass(&pRetRenderPass, &renderPassDesc);
            KGLOG_PROCESS_ERROR(bRetCode);

            fbufCreateInfo = vks::initializers::FramebufferCreateInfo();
            fbufCreateInfo.renderPass = pRetRenderPass->GetPass();
            fbufCreateInfo.attachmentCount = (uint32_t)views.size();
            fbufCreateInfo.pAttachments = fbufCreateInfo.attachmentCount > 0 ? views.data() : nullptr;
            fbufCreateInfo.width = m_uWidth;
            fbufCreateInfo.height = m_uHeight;
            fbufCreateInfo.layers = 1;

            hRetCode = vks::vkCreateFramebuffer(pDevice, &fbufCreateInfo, nullptr, &m_pFramebuffer);
            KGLOG_COM_PROCESS_ERROR(hRetCode);

            m_cmdBufInfo = vks::initializers::CommandBufferBeginInfo();

            // clear color
            m_renderPassBeginInfo = vks::initializers::RenderPassBeginInfo();
            m_renderPassBeginInfo.renderPass = pRetRenderPass->GetPass();
            m_renderPassBeginInfo.framebuffer = m_pFramebuffer;
            m_renderPassBeginInfo.renderArea.extent.width = m_uWidth;
            m_renderPassBeginInfo.renderArea.extent.height = m_uHeight;
            m_renderPassBeginInfo.clearValueCount = (uint32_t)m_clearValues.size();
            m_renderPassBeginInfo.pClearValues = m_clearValues.data();

            m_viewport = vks::initializers::Viewport((float)m_uWidth, (float)m_uHeight, 0.0f, 1.0f);
            m_scissor = vks::initializers::Rect2D(m_uWidth, m_uHeight, 0, 0);

            m_pRenderPass = pRetRenderPass;
            pRetRenderPass = nullptr;
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    VkFramebuffer KVulkanRenderFrameBuffer::GetFrameBuffer()
    {
        return m_pFramebuffer;
    }

    uint32_t KVulkanRenderFrameBuffer::GetWidth() const
    {
        return m_uWidth;
    }
    uint32_t KVulkanRenderFrameBuffer::GetHeight() const
    {
        return m_uHeight;
    }

    KVulkanRenderPass* KVulkanRenderFrameBuffer::GetRenderPassPtr()
    {
        return m_pRenderPass;
    }

    BOOL KVulkanRenderFrameBuffer::_BeginPass(IKGFX_RenderContext* pRenderCtx, BOOL bSecondary, BOOL bUseBarrier, bool bImmediateMode)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;

        KVulkanRenderContext* pGraphicsCmdVk = nullptr;
        VkCommandBuffer pCommandBufferVk = nullptr;

        KGLOG_ASSERT_EXIT(pRenderCtx);
        pGraphicsCmdVk = (KVulkanRenderContext*)pRenderCtx;
        pCommandBufferVk = pGraphicsCmdVk->GetCommandBufferVk();
        KGLOG_ASSERT_EXIT(pCommandBufferVk);

        {
            pRenderCtx->Transition(m_BeginPassLayoutTrans.data(), (uint32_t)m_BeginPassLayoutTrans.size());
        }

        if (bSecondary)
        {
            ASSERT(FALSE);
            pGraphicsCmdVk->BeginRenderPass(m_renderPassBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS, bImmediateMode);
        }
        else
        {
            pGraphicsCmdVk->BeginRenderPass(m_renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE, bImmediateMode);
        }

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KVulkanRenderFrameBuffer::BeginPass(IKGFX_RenderContext* pRenderCtx, bool bUseBarrier, bool bImmediateMode)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        KVulkanRenderContext* pRenderCtxVk = (KVulkanRenderContext*)pRenderCtx;
        CHECK_ASSERT(pRenderCtxVk);
        //CHECK_ASSERT(pRenderCtxVk->GetCurrentFrameBuffer() == nullptr && "Previous render pass has not EndPass()!");

        pRenderCtxVk->SetCurrentFrameBuffer(this);

        bRetCode = _BeginPass(pRenderCtxVk, false, bUseBarrier, bImmediateMode);
        KGLOG_PROCESS_ERROR(bRetCode);

        pRenderCtxVk->CmdSetScissor(this);
        pRenderCtxVk->CmdSetViewport(this);


        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanRenderFrameBuffer::EndPass(IKGFX_RenderContext* pRenderCtx)
    {
        PROF_CPU_DETAIL();

        BOOL bRet = FALSE;
        BOOL bRetCode = FALSE;

        KVulkanRenderContext* pGraphicsCmdVk = nullptr;
        VkCommandBuffer pCommandBufferVk = nullptr;

        KGLOG_ASSERT_EXIT(pRenderCtx);
        pGraphicsCmdVk = (KVulkanRenderContext*)pRenderCtx;
        pCommandBufferVk = pGraphicsCmdVk->GetCommandBufferVk();
        KGLOG_ASSERT_EXIT(pCommandBufferVk);

        pGraphicsCmdVk->EndRenderPass();

        m_bUseCustomViewport = FALSE;

        bRet = TRUE;
    Exit0:
        if (pGraphicsCmdVk)
            ASSERT(pGraphicsCmdVk->GetCurrentFrameBuffer());
        // pRenderCtx->SetCurrentFrameBuffer(nullptr);
        return bRet;
    }

    void KVulkanRenderFrameBuffer::SetObjectName(const char* szName)
    {
        GetVulkanDevice()->SetObjectLabel(m_pFramebuffer, szName);
    }

    uint64_t KVulkanRenderFrameBuffer::GetCode()
    {
        uint64_t uCode = 0;

        if (m_pRenderPass)
        {
            uCode += (uint64_t)m_pRenderPass->GetRenderPassPtr();
        }

        uCode += (uint64_t)m_pFramebuffer;

        return uCode;
    }
}

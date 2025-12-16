#include "KVulkanRenderContext.h"
#include "KVulkanGraphicDevice.h"
#include "KVulkanRayTracing.h"
#include "kVulkanBuffer.h"
#include "KVulkanRenderFrameBuffer.h"
#include "KVulkanCommandBuffer.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include <memory_resource>

#include "KBase/Public/KMemLeak.h"

#define BIND_OPTIMIZE_ON 1

namespace gfx
{
    static bool s_bEnableGraphicsPipelineRecording = true;

    const KGfxTextureFormatInfo& GetTextureFormatInfoVk(enumTextureFormat eFormat);
    VkImageAspectFlags GetAspectFlagsFromFormat(enumTextureFormat eGfxFormat);

    VkIndexType GetIndexType(enumIndexType indexType)
    {
        VkIndexType ret = VK_INDEX_TYPE_UINT16;
        switch (indexType)
        {
        case INDEX_TYPE_UINT16:
            ret = VK_INDEX_TYPE_UINT16;
            break;
        case INDEX_TYPE_UINT32:
            ret = VK_INDEX_TYPE_UINT32;
            break;
        default:
            break;
        }
        return ret;
    }

    VkPipelineBindPoint GetVkPipelineBindPoint(enumPipelineBindPoint point)
    {
        VkPipelineBindPoint ret = VK_PIPELINE_BIND_POINT_GRAPHICS;
        switch (point)
        {
        case gfx::PIPELINE_BIND_POINT_GRAPHICS:
            ret = VK_PIPELINE_BIND_POINT_GRAPHICS;
            break;
        case gfx::PIPELINE_BIND_POINT_COMPUTE:
            ret = VK_PIPELINE_BIND_POINT_COMPUTE;
            break;
        case gfx::PIPELINE_BIND_POINT_RAY_TRACNG:
            ret = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
            break;
        default:
            break;
        }
        return ret;
    }

    KVulkanRenderContext::KVulkanRenderContext()
    {
    }

    KVulkanRenderContext::~KVulkanRenderContext()
    {
        UnInit();
    }

    BOOL KVulkanRenderContext::Init()
    {
        BOOL                 bRetCode = FALSE;
        BOOL                 bResult = FALSE;
        gfx::KGraphicDevice* pGraphicDevice = GetGraphicDevice();
        for (int i = 0; i < countof(m_vCommandBuffers); i++)
        {
            KVulkanCommandBuffer* pCmdBuffer = nullptr;

            bRetCode = pGraphicDevice->CreateCommandBuffer(&pCmdBuffer, COMMAND_BUFFER_LEVEL_PRIMARY, gfx::FOR_GRPAHIC);
            KGLOG_PROCESS_ERROR(bRetCode);

            // pCmdBuffer->SetId(i % m_pSwapChain->m_nImageCount);
            pCmdBuffer->SetId(i);

            std::string strName = "MainCommandBuffer" + std::to_string(i) + "_";
            pCmdBuffer->SetObjectName(strName.c_str());
            m_vCommandBuffers[i] = dynamic_cast<KVulkanCommandBuffer*>(pCmdBuffer);
        }

        m_bBegun = FALSE;
        m_nCurCommandIndex = -1;
        m_nPreCommandIndex = -1;

        m_GraphicsCmdRecorder = new KVulkanGraphicsCommandRecorder(this);
        CHECK_ASSERT(m_GraphicsCmdRecorder);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    void KVulkanRenderContext::UnInit()
    {
        SAFE_DELETE(m_GraphicsCmdRecorder);

        gfx::KGraphicDevice* pGraphicDevice = GetGraphicDevice();
        for (auto& pCommandBuffer : m_vCommandBuffers)
        {
            KVulkanCommandBuffer* pCmdBuffer = pCommandBuffer;
            if (pCommandBuffer)
            {
                pGraphicDevice->DestroyCommandBuffer(pCmdBuffer);
            }
            pCommandBuffer = nullptr;
        }
    }

    bool KVulkanRenderContext::IsValid() const
    {
        return m_pVulkanCmdBuffer != nullptr;
    }

    /**
     * @brief 开始Vulkan渲染通道（RenderPass）
     * 
     * 此函数用于在Vulkan渲染流程中开启一个新的RenderPass。它会根据传入的VkRenderPassBeginInfo结构体、子通道内容类型（VkSubpassContents）以及是否立即模式（bImmediateMode）
     * 调用命令记录器（KVulkanGraphicsCommandRecorder）的Begin方法，设置渲染通道的相关参数。
     * 
     * @param InBeginInfo     Vulkan渲染通道开始信息（VkRenderPassBeginInfo），包含RenderPass、FrameBuffer、清除值等
     * @param ePassContents   指定子通道内容类型（VkSubpassContents），如INLINE或SECONDARY_COMMAND_BUFFERS
     * @param bImmediateMode  是否立即模式（true为立即执行，false为延迟记录），立即执行仅在使用第三方渲染库渲染时使用
     */
    void KVulkanRenderContext::BeginRenderPass(const VkRenderPassBeginInfo& InBeginInfo, VkSubpassContents ePassContents, bool bImmediateMode)
    {
        m_GraphicsCmdRecorder->Begin(InBeginInfo, ePassContents, bImmediateMode);
        m_nCurRenderPassCount++;
    }

    void KVulkanRenderContext::EndRenderPass()
    {
        m_GraphicsCmdRecorder->End();
    }

    bool KVulkanRenderContext::IsInRenderPass() const
    {
        return m_GraphicsCmdRecorder->IsInGraphicsPipelineScope();
    }

    BOOL KVulkanRenderContext::BeginCommandBuffer()
    {
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;

        KVulkanCommandBuffer* pCommandBuffer = nullptr;
        KGraphicDevice* pGraphicDevice = GetGraphicDevice();

        KG_PROCESS_SUCCESS(m_bBegun);

        KGLOG_ASSERT_EXIT(m_pVulkanCmdBuffer == nullptr);

        m_nPreCommandIndex = m_nCurCommandIndex;
        m_nCurCommandIndex = (m_nCurCommandIndex + 1) % countof(m_vCommandBuffers);

        pCommandBuffer = m_vCommandBuffers[m_nCurCommandIndex];
        if (!pCommandBuffer)
        {
            KVulkanCommandBuffer* pCmdBuffer = nullptr;
            bRetCode = pGraphicDevice->CreateCommandBuffer(&pCmdBuffer, COMMAND_BUFFER_LEVEL_PRIMARY, gfx::FOR_GRPAHIC);
            KGLOG_PROCESS_ERROR(bRetCode);

            pCommandBuffer = static_cast<KVulkanCommandBuffer*>(pCmdBuffer);
            m_vCommandBuffers[m_nCurCommandIndex] = pCommandBuffer;
        }
        else
        {
            pCommandBuffer->WaitForFence();
        }

        pCommandBuffer->m_uLastBegunCmdBufferFrame = NSEngine::GetRenderFrameMoveLoopCount();

#if MICROPROFILE_ENABLED
        if (DrvOption::GetRenderApi() == GFX_VULKAN_API)
        {
            MICROPROFILE_BEGIN_GPU(pCommandBuffer);
        }
#endif

        bRetCode = pGraphicDevice->BeginCommandBuffer(pCommandBuffer, nullptr);
        KGLOG_ASSERT_EXIT(bRetCode);

        m_pVkCmdBuffer = pCommandBuffer->GetCommandBuffer();
        m_pVulkanCmdBuffer = pCommandBuffer;
        m_bBegun = TRUE;

    Exit1:
        bResult = TRUE;
    Exit0:
        return bResult;
    }

    void KVulkanRenderContext::SubmitCommandBuffer(BOOL bWait /* = FALSE*/, void* pGpuCompletedSignal /* = nullptr*/)
    {
        BOOL            bRetCode = FALSE;
        KGraphicDevice* pGraphicDevice = GetGraphicDevice();
        KVulkanSemaphore* pSemaphore = (KVulkanSemaphore*)pGpuCompletedSignal;
        ASSERT(pGraphicDevice);

        // 检查RenderPass是否结束
        CHECK_ASSERT(!m_GraphicsCmdRecorder->IsInCmdRecording());

        // 检查是否调用过BeginCommandBuffer
        KG_PROCESS_SUCCESS(m_bBegun == FALSE);
		
        ASSERT(m_pVulkanCmdBuffer);
        KGLOG_ASSERT_EXIT(m_nCurCommandIndex >= 0 && m_nCurCommandIndex < countof(m_vCommandBuffers));

        // if (m_nPreCommandIndex >= 0 && m_nCurrentCommandIndex < countof(m_vCommandBuffers))
        //{
        //     if (DrvOption::bIsMaliGPU && DrvOption::nDeviceNumber >= 600)
        //     {
        //         // m_pPrevCommander->WaitForFence(3000); //天机不等就会卡死
        //         m_vCommandBuffers[m_nPreCommandIndex]->WaitForFence();
        //     }
        // }

        bRetCode = pGraphicDevice->EndCommandBuffer(m_pVulkanCmdBuffer);
        KGLOG_ASSERT_EXIT(bRetCode);

        {
            KVulkanGfxQueue* pGraphicQueue = pGraphicDevice->GetGraphicQueue();
            ASSERT(pGraphicQueue);


            pGraphicDevice->QueueSubmit(pGraphicQueue, m_pVulkanCmdBuffer, bWait, pSemaphore);

            m_pVulkanCmdBuffer = nullptr;
            m_bBegun = FALSE;
        }
    Exit1:
    Exit0:
        return;
    }

    uint32_t KVulkanRenderContext::GetCommandBufferId() const
    {
        CHECK_ASSERT(m_pVulkanCmdBuffer);
        return m_pVulkanCmdBuffer->GetId();
    }

    void* KVulkanRenderContext::GetCommandBufferNativeHandle() const
    {
        return GetCommandBufferVk();
    }

    void KVulkanRenderContext::CmdClearAttachment(const KClearAttchment* pAttachment, int nCount, const NSKMath::KVectorInt2& v2Offset, const NSKMath::KVectorUint2& v2Size, uint32_t uBaseArrayLayer /* = 0*/, uint32_t uLayerCount /* = 1*/)
    {
        PROF_CPU_DETAIL();

        std::vector<VkClearAttachment> vecClearAttach;
        VkClearRect                    vkClearRect;

        KGLOG_ASSERT_EXIT(pAttachment && nCount > 0);
        CHECK_ASSERT(IsInRenderPass());

        vecClearAttach.resize(nCount);
        for (int i = 0; i < nCount; ++i)
        {
            const KClearAttchment& sClearAttach = pAttachment[i];
            VkClearAttachment& vkClearAttach = vecClearAttach[i];

            if (sClearAttach.eAspectMask == ImageAspectFlagBits::IMAGE_ASPECT_COLOR_BIT)
            {
                vkClearAttach.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                vkClearAttach.colorAttachment = sClearAttach.uAttachmentIndex;
                vkClearAttach.clearValue.color = {
                    {sClearAttach.sClearValue.r, sClearAttach.sClearValue.g, sClearAttach.sClearValue.b, sClearAttach.sClearValue.a}
                };
            }
            else if (sClearAttach.eAspectMask == ImageAspectFlagBits::IMAGE_ASPECT_DEPTH_BIT)
            {
                vkClearAttach.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                vkClearAttach.clearValue.depthStencil.depth = sClearAttach.sClearValue.depth;
            }
            else if (sClearAttach.eAspectMask == ImageAspectFlagBits::IMAGE_ASPECT_STENCIL_BIT)
            {
                vkClearAttach.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
                vkClearAttach.clearValue.depthStencil.stencil = sClearAttach.sClearValue.stencil;
            }
            else
            {
                KGLOG_ASSERT_EXIT(FALSE);
            }
        }

        vkClearRect.baseArrayLayer = uBaseArrayLayer;
        vkClearRect.layerCount = uLayerCount;
        vkClearRect.rect.offset = { v2Offset.x, v2Offset.y };
        vkClearRect.rect.extent = { v2Size.x, v2Size.y };

        m_GraphicsCmdRecorder->CmdClearAttachments(m_pVkCmdBuffer, vecClearAttach, vkClearRect);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdClearTextureView(IKGFX_TextureView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(view);
        CHECK_ASSERT(!IsInRenderPass());

        auto pResource = view->GetResource();
        CHECK_ASSERT(pResource);

        switch (view->GetViewDesc().eViewType)
        {
        case gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV:
        {
            VkClearDepthStencilValue ClearDepthStencil;
            ClearDepthStencil.depth = clearValue.depth;
            ClearDepthStencil.stencil = clearValue.stencil;

            VkImageSubresourceRange Range;
            Range.aspectMask = 0;
            Range.baseArrayLayer = 0;
            Range.baseMipLevel = 0;
            Range.layerCount = VK_REMAINING_ARRAY_LAYERS;
            Range.levelCount = VK_REMAINING_MIP_LEVELS;

            if (NSKMath::HasAnyFlags((uint32_t)flags, (uint32_t)KGFX_ClearResourceViewFlags::ClearDepth))
            {
                Range.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            if (NSKMath::HasAnyFlags((uint32_t)flags, (uint32_t)KGFX_ClearResourceViewFlags::ClearStencil))
            {
                Range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            ASSERT(Range.aspectMask > 0 && "flags需要指定包含KGFX_ClearResourceViewFlags中ClearDepth或者ClearStencil");

            Transition({ pResource, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst });

            VkImage vkImageHandle = (VkImage)pResource->GetNativeResourceHandle();
            vks::vkCmdClearDepthStencilImage(m_pVkCmdBuffer, vkImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearDepthStencil, 1, &Range);
        }
        break;
        case gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV:
        case gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV:
        {
            VkClearColorValue ClearColor;
            ClearColor.float32[0] = clearValue.r;
            ClearColor.float32[1] = clearValue.g;
            ClearColor.float32[2] = clearValue.b;
            ClearColor.float32[3] = clearValue.a;

            VkImageSubresourceRange Range;
            Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            Range.baseArrayLayer = 0;
            Range.baseMipLevel = 0;
            Range.layerCount = VK_REMAINING_ARRAY_LAYERS;
            Range.levelCount = VK_REMAINING_MIP_LEVELS;

            Transition({ pResource, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst });

            VkImage vkImageHandle = (VkImage)pResource->GetNativeResourceHandle();
            vks::vkCmdClearColorImage(m_pVkCmdBuffer, vkImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearColor, 1, &Range);
        }
        break;
        default:
        {
            DEBUG_BREAK();
        }
        }
    }

    void KVulkanRenderContext::CmdClearBufferView(IKGFX_BufferView* view, KClearValue clearValue, KGFX_ClearResourceViewFlags flags)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(view);
        CHECK_ASSERT(!IsInRenderPass());

        auto pBuffer = view->GetResource();
        CHECK_ASSERT(pBuffer);

        KVulkanBuffer* pkvkBuffer = (KVulkanBuffer*)pBuffer;
        CHECK_ASSERT(pkvkBuffer);

        auto viewDesc = view->GetViewDesc();
        CHECK_ASSERT(viewDesc->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV);

        auto bufDesc = pkvkBuffer->GetDesc();
        CHECK_ASSERT(bufDesc->eResAccessFlags == KGfxResourceAccessType::KGfxResourceAccess_GPUOnly);

        uint32_t uDstByteWidth = bufDesc->uByteWidth;

        Transition({ pBuffer, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst });

        vks::vkCmdFillBuffer(m_pVkCmdBuffer, pkvkBuffer->GetVkBuffer(), 0, uDstByteWidth, clearValue.ux);
    }

    void KVulkanRenderContext::CmdSetScissor(int nX, int nY, int nWidth, int nHeight)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(nX >= 0 && nY >= 0 && nWidth > 0 && nHeight > 0);
        KGLOG_ASSERT_EXIT(nX < nWidth && nY < nHeight);

        VkRect2D rect;
        rect.offset.x = nX;
        rect.offset.y = nY;
        rect.extent.width = nWidth;
        rect.extent.height = nHeight;

        m_GraphicsCmdRecorder->CmdSetScissor(m_pVkCmdBuffer, rect);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdSetScissor(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer)
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(piRenderFrameBuffer);
        CHECK_ASSERT(m_pVkCmdBuffer);

        KVulkanRenderFrameBuffer* pVkFrameBuffer = (KVulkanRenderFrameBuffer*)piRenderFrameBuffer;
        KGLOG_ASSERT_EXIT(pVkFrameBuffer);

        m_GraphicsCmdRecorder->CmdSetScissor(m_pVkCmdBuffer, pVkFrameBuffer->m_scissor);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdSetViewport(const ViewportDescription& Viewport)
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(m_pVkCmdBuffer);

        VkViewport vkViewport;
        KGLOG_ASSERT_EXIT(Viewport.TopLeftX >= 0.f && Viewport.TopLeftY >= 0.f && Viewport.Width > 0.f && Viewport.Height > 0.f);

        vkViewport.x = Viewport.TopLeftX;
        vkViewport.y = Viewport.TopLeftY;
        vkViewport.width = Viewport.Width;
        vkViewport.height = Viewport.Height;
        vkViewport.minDepth = Viewport.MinDepth;
        vkViewport.maxDepth = Viewport.MaxDepth;

        m_GraphicsCmdRecorder->CmdSetViewport(m_pVkCmdBuffer, vkViewport);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdSetViewport(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer)
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(piRenderFrameBuffer);
        CHECK_ASSERT(m_pVkCmdBuffer);

        KVulkanRenderFrameBuffer* pVkFrameBuffer = (KVulkanRenderFrameBuffer*)piRenderFrameBuffer;
        KGLOG_ASSERT_EXIT(pVkFrameBuffer);

        if (pVkFrameBuffer->m_bUseCustomViewport)
            m_GraphicsCmdRecorder->CmdSetViewport(m_pVkCmdBuffer, pVkFrameBuffer->m_CustomViewport);
        else
            m_GraphicsCmdRecorder->CmdSetViewport(m_pVkCmdBuffer, pVkFrameBuffer->m_viewport);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdBindVertexBuffers(int nFirstBinding, int nBindingCount, IKGFX_Buffer* apBuffer[], int anOffsets[], uint32_t* stride)
    {
        PROF_CPU_DETAIL();
        KG3D_Vector<KGfxBarrier, 4> vecBarriers;

        CHECK_ASSERT(m_pVkCmdBuffer);
        CHECK_ASSERT(m_pVulkanCmdBuffer);

        uint32_t cmdId = m_pVulkanCmdBuffer->GetId();

        KGLOG_ASSERT_EXIT(nFirstBinding >= 0);
        KGLOG_ASSERT_EXIT(nBindingCount > 0 && nBindingCount <= KMAX_BIND_VERT_STREAM);

        vecBarriers.reserve(nBindingCount);
        for (int i = 0; i < nBindingCount; ++i)
        {
            CHECK_ASSERT(apBuffer[i]);
            vecBarriers.push_back(KGfxBarrier(apBuffer[i], gfx::KGfxAccess::Unknown, gfx::KGfxAccess::VertexBuffer));
        }
        Transition(vecBarriers.data(), (uint32_t)vecBarriers.size());

        if (nBindingCount == 1)
        {
            KVulkanBuffer* pVulkanVertBuffer = (KVulkanBuffer*)apBuffer[0];
            VkBuffer          pBuffer = pVulkanVertBuffer->GetVkBuffer();
            VkDeviceSize      _offsets[1] = { static_cast<VkDeviceSize>(anOffsets[0]) };
            uint32_t          bufferId = pVulkanVertBuffer->GetId();
#if BIND_OPTIMIZE_ON
            uint32_t frmId = NSEngine::GetRenderFrameMoveLoopCount();
            struct _last_vert
            {
                int32_t      cmdId = 0;
                uint32_t     bufferId = 0;
                VkDeviceSize offset = 0;
                uint32_t     firstBinding = 0;
                uint32_t     frmId = 0;
            };

            static _last_vert last_vert;
            _last_vert& last = last_vert;

            if (last.frmId == frmId && last.cmdId == cmdId && last.bufferId == bufferId && last.offset == _offsets[0] && last.firstBinding == nFirstBinding)
            {
                // 优化
                return;
            }
            last.cmdId = cmdId;
            last.bufferId = bufferId;
            last.offset = _offsets[0];
            last.firstBinding = nFirstBinding;
            last.frmId = frmId;
#endif
            m_GraphicsCmdRecorder->CmdBindVertexBuffers(m_pVkCmdBuffer, nFirstBinding, 1, &pBuffer, _offsets);
        }
        else
        {
            VkDeviceSize vecOffset[KMAX_BIND_VERT_STREAM];
            VkBuffer     vecBuffer[KMAX_BIND_VERT_STREAM];
            ASSERT(nBindingCount <= KMAX_BIND_VERT_STREAM);
            for (int i = 0; i < nBindingCount; ++i)
            {
                KVulkanBuffer* pVulkanVertBuffer = (KVulkanBuffer*)apBuffer[i];
                vecBuffer[i] = pVulkanVertBuffer->GetVkBuffer();
                vecOffset[i] = (VkDeviceSize)anOffsets[i];
            }
            m_GraphicsCmdRecorder->CmdBindVertexBuffers(m_pVkCmdBuffer, nFirstBinding, nBindingCount, vecBuffer, vecOffset);
        }
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdBindIndexBuffer(IKGFX_Buffer* pBuffer, int nOffset, enumIndexType indexType)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(pBuffer && indexType < enumIndexType::INDEX_TYPE_MAX_NUM);

        Transition({ pBuffer, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::IndexBuffer });

        {
            uint32_t cmdId = m_pVulkanCmdBuffer->GetId();
            KVulkanBuffer* pVKIB = (KVulkanBuffer*)pBuffer;
            uint32_t bufferId = pVKIB->GetId();
            VkBuffer pIB = pVKIB->GetVkBuffer();
            VkIndexType inType = GetIndexType(indexType);

#if BIND_OPTIMIZE_ON
            uint32_t frmId = NSEngine::GetRenderFrameMoveLoopCount();
            struct _last_index
            {
                int32_t       cmdId = 0;
                uint32_t      bufferId = 0;
                uint32_t      offset = 0;
                enumIndexType indexType = INDEX_TYPE_UINT16;
                uint32_t      frmId = 0;
            };
            static _last_index last_index;
            _last_index& last = last_index;

            if (last.frmId == frmId && last.cmdId == cmdId && last.bufferId == bufferId && last.offset == nOffset && last.indexType == indexType)
            {
                // 优化
                return;
            }
            last.cmdId = cmdId;
            last.bufferId = bufferId;
            last.offset = nOffset;
            last.indexType = indexType;
            last.frmId = frmId;
#endif

            m_GraphicsCmdRecorder->CmdBindIndexBuffer(m_pVkCmdBuffer, pIB, nOffset, inType);
        }
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdDraw(int nVertexCount, int nFirstVertex, bool bPoint /*= false*/)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVulkanCmdBuffer);
        CHECK_ASSERT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(nVertexCount > 0 && nFirstVertex >= 0);

        m_pVulkanCmdBuffer->m_uDrawCallCount++;
        if (bPoint)
            m_pVulkanCmdBuffer->m_uDrawPointCount += nVertexCount;
        else
            m_pVulkanCmdBuffer->m_uDrawIndexCount += nVertexCount;

        m_GraphicsCmdRecorder->CmdDraw(m_pVkCmdBuffer, nVertexCount, nFirstVertex);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdDrawInstanced(int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVulkanCmdBuffer);
        CHECK_ASSERT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(nVertexCount > 0 && nFirstVertex >= 0 && nInstanceCount > 0 && nFirstInstance >= 0);

        m_pVulkanCmdBuffer->m_uDrawCallCount++;
        m_pVulkanCmdBuffer->m_uDrawIndexCount += nVertexCount * nInstanceCount;

        m_GraphicsCmdRecorder->CmdDrawInstanced(m_pVkCmdBuffer, nVertexCount, nInstanceCount, nFirstVertex, nFirstInstance);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdDrawIndexed(int nIndexCount, int nInstanceCount, int nFirstIndex, int nVertexOffset, int nFirstInstance)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVulkanCmdBuffer);
        CHECK_ASSERT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(nIndexCount > 0 && nInstanceCount > 0 && nFirstIndex >= 0 && nVertexOffset >= 0 && nFirstInstance >= 0);

        m_pVulkanCmdBuffer->m_uDrawCallCount++;
        m_pVulkanCmdBuffer->m_uDrawIndexCount += nIndexCount * nInstanceCount;

        m_GraphicsCmdRecorder->CmdDrawIndexed(m_pVkCmdBuffer, nIndexCount, nInstanceCount, nFirstIndex, nVertexOffset, nFirstInstance);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdDrawIndexedInstanced(int nIndexCount, int nFirstIndex, int nInstanceCount, int nFirstVertex, int nFirstInstance)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVulkanCmdBuffer);
        CHECK_ASSERT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(nIndexCount > 0 && nInstanceCount > 0 && nFirstIndex >= 0 && nFirstVertex >= 0 && nFirstInstance >= 0);

        m_pVulkanCmdBuffer->m_uDrawCallCount++;
        m_pVulkanCmdBuffer->m_uDrawIndexCount += nIndexCount * nInstanceCount;

        m_GraphicsCmdRecorder->CmdDrawIndexed(m_pVkCmdBuffer, nIndexCount, nInstanceCount, nFirstIndex, nFirstVertex, nFirstInstance);
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdDrawIndexedIndirect(IKGFX_Buffer* pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride, bool bRecordDrawCall /*= true*/)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVkCmdBuffer);
        CHECK_ASSERT(nDrawCount > 0);

        KVulkanBuffer* pVulkanIndirectCmdBuffer = (KVulkanBuffer*)pIndirectCmdBuffer;
        VkBuffer pInDirectBuffer = pVulkanIndirectCmdBuffer->GetVkBuffer();
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();

#if defined(_WIN32) && defined(_DEBUG)
        const KGfxBufferDesc* pBufferDesc = pVulkanIndirectCmdBuffer->GetDesc();
        ASSERT(nOffset < (int)pBufferDesc->uByteWidth);
#endif

        Transition({ pIndirectCmdBuffer, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::IndirectArgs });

        if (pVulkanDevice->m_Features.multiDrawIndirect)
        {
            if (bRecordDrawCall)
            {
                m_pVulkanCmdBuffer->m_uDrawCallCount += nDrawCount;
            }
            m_GraphicsCmdRecorder->CmdDrawIndexedIndirect(m_pVkCmdBuffer, pInDirectBuffer, nOffset, nDrawCount, nStride);
        }
        else
        {
            if (bRecordDrawCall)
            {
                m_pVulkanCmdBuffer->m_uDrawCallCount += nDrawCount;
            }

            for (int i = 0; i < nDrawCount; ++i)
            {
                m_GraphicsCmdRecorder->CmdDrawIndexedIndirect(m_pVkCmdBuffer, pInDirectBuffer, nOffset + i * nStride, 1, nStride);
            }
        }
    }

    void KVulkanRenderContext::CmdDrawIndirect(IKGFX_Buffer* pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(nDrawCount > 0);
        CHECK_ASSERT(m_pVkCmdBuffer);

        KVulkanBuffer* pVulkanIndirectCmdBuffer = (KVulkanBuffer*)pIndirectCmdBuffer;
        VkBuffer pInDirectBuffer = pVulkanIndirectCmdBuffer->GetVkBuffer();
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();

#if defined(_WIN32) && defined(_DEBUG)
        const KGfxBufferDesc* pBufferDesc = pVulkanIndirectCmdBuffer->GetDesc();
        ASSERT(nOffset < (int)pBufferDesc->uByteWidth);
#endif

        Transition({ pIndirectCmdBuffer, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::IndirectArgs });

        if (pVulkanDevice->m_Features.multiDrawIndirect)
        {
            m_pVulkanCmdBuffer->m_uDrawCallCount += nDrawCount;
            m_GraphicsCmdRecorder->CmdDrawIndirect(m_pVkCmdBuffer, pInDirectBuffer, nOffset, nDrawCount, nStride);
        }
        else
        {
            m_pVulkanCmdBuffer->m_uDrawCallCount += nDrawCount;

            for (int i = 0; i < nDrawCount; ++i)
            {
                m_GraphicsCmdRecorder->CmdDrawIndirect(m_pVkCmdBuffer, pInDirectBuffer, nOffset + i * nStride, 1, nStride);
            }
        }
    }

    void KVulkanRenderContext::CmdSetLineWidth(float linewidth)
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(m_pVkCmdBuffer);
        m_GraphicsCmdRecorder->CmdSetLineWidth(m_pVkCmdBuffer, linewidth);
    }

    void KVulkanRenderContext::CmdBindPipeline(enumPipelineBindPoint eBindPoint, KPipeline* pPipeline)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(pPipeline);
        CHECK_ASSERT(m_pVkCmdBuffer);

        uint32_t cmdId = m_pVulkanCmdBuffer->GetId();
        VkPipelineBindPoint point = GetVkPipelineBindPoint(eBindPoint);
        enumForProcessType type = pPipeline->GetType();
        VkPipeline pl = nullptr;

        if (type == FOR_GRPAHIC)
        {
            KVulkanGraphicsPipeline* pVulkanPipeline = (KVulkanGraphicsPipeline*)pPipeline;
            pl = pVulkanPipeline->GetPipeline();
        }
        else if (type == FOR_COMPUTE)
        {
            KVulkanComputePipeline* pVulkanPipeline = (KVulkanComputePipeline*)pPipeline;
            pl = pVulkanPipeline->GetPipeline();
        }
        else if (type == FOR_RAYTRACING)
        {
            KVulkanRayTracingPipeline* pVulkanPipeline = (KVulkanRayTracingPipeline*)pPipeline;
            pl = pVulkanPipeline->GetPipeline();
        }

#if BIND_OPTIMIZE_ON
        uint32_t frmId = NSEngine::GetRenderFrameMoveLoopCount();

        struct _last_pipeline
        {
            uint32_t cmdId = 0;
            VkPipelineBindPoint point = VK_PIPELINE_BIND_POINT_GRAPHICS;
            uint32_t uPipelineId = 0xFFFF;
            uint32_t frmId = 0;
        };
        static _last_pipeline last_pipeline;

        _last_pipeline& last = last_pipeline;
        if (last.frmId == frmId && last.cmdId == cmdId && last.point == point && last.uPipelineId == pPipeline->GetCreateId())
        {
            return;
        }
        last.frmId = frmId;
        last.cmdId = cmdId;
        last.point = point;
        last.uPipelineId = pPipeline->GetCreateId();
#endif
        KGLOG_ASSERT_EXIT(pl);

        m_GraphicsCmdRecorder->CmdBindPipeline(m_pVkCmdBuffer, point, pl);

    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdBindDescriptorSets(
        enumPipelineBindPoint eBindPoint,
        const KVulkanLayout* pLayout,
        uint32_t uSet,
        KVulkanDescriptorSet* pDescriptorSet,
        uint32_t uDynamicOffsetCount,
        const uint32_t auDynamicOffsets[]
    )
    {
        PROF_CPU_DETAIL();

        KGLOG_ASSERT_EXIT(pLayout);
        KGLOG_ASSERT_EXIT(pDescriptorSet);
        CHECK_ASSERT(uDynamicOffsetCount == 0 || auDynamicOffsets != nullptr);
        CHECK_ASSERT(m_pVkCmdBuffer);

        {
            uint32_t              cmdId = m_pVulkanCmdBuffer->GetId();
            VkPipelineBindPoint   point = GetVkPipelineBindPoint(eBindPoint);
            VkPipelineLayout      pPipelineLayout = pLayout->GetPipelineLayout();

            KVulkanDescriptorSet* pVulkanDescriptorSet = (KVulkanDescriptorSet*)pDescriptorSet;
            VkDescriptorSet       pSet = pVulkanDescriptorSet->GetDescriptorSet(uSet);

            if (DrvOption::bSupportDynamicUBO)
            {
#if BIND_OPTIMIZE_ON
                if (uDynamicOffsetCount < 32)
                {
                    uint32_t frmId = NSEngine::GetRenderFrameMoveLoopCount();
                    struct _last_descriptor
                    {
                        uint32_t            cmdId = 0;
                        VkPipelineBindPoint point = VK_PIPELINE_BIND_POINT_GRAPHICS;
                        uint64_t            pPipelineLayout = 0;
                        uint32_t            uSet = 0;
                        uint64_t            pSet = 0;
                        uint32_t            dynamicOffsetCount = 0;
                        uint32_t            dynamicOffsets[32] = { 0 };
                        uint32_t            frmId = 0;
                    };

                    static _last_descriptor last_descriptor;
                    _last_descriptor& last = last_descriptor;

                    BOOL allEq = true;
                    do
                    {
                        if (last.frmId != frmId || last.dynamicOffsetCount != uDynamicOffsetCount)
                        {
                            allEq = false;
                            break;
                        }

                        for (uint32_t i = 0; i < uDynamicOffsetCount; ++i)
                        {
                            if (last.dynamicOffsets[i] != auDynamicOffsets[i])
                            {
                                allEq = false;
                                break;
                            }
                        }

                        if (!allEq || last.cmdId != cmdId || last.point != point || last.pPipelineLayout != (uint64_t)pPipelineLayout + (uint64_t)pLayout || last.uSet != uSet || last.pSet != (uint64_t)pSet + (uint64_t)pDescriptorSet)
                        {
                            allEq = false;
                            break;
                        }
                    } while (0);

                    if (allEq)
                    {
                        // 优化
                        return;
                    }


                    last.frmId = frmId;
                    last.cmdId = cmdId;
                    last.point = point;
                    last.pPipelineLayout = (uint64_t)pPipelineLayout + (uint64_t)pLayout;
                    last.uSet = uSet;
                    last.pSet = (uint64_t)pSet + (uint64_t)pDescriptorSet;
                    last.dynamicOffsetCount = uDynamicOffsetCount;
                    for (uint32_t i = 0; i < uDynamicOffsetCount; ++i)
                    {
                        last.dynamicOffsets[i] = auDynamicOffsets[i];
                    }
                    if (eBindPoint == PIPELINE_BIND_POINT_GRAPHICS)
                    {
                        KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
                        pEnginePerformace->dwDrawBatchCount++;
                    }
                }
#endif

                m_GraphicsCmdRecorder->CmdBindDescriptorSets(m_pVkCmdBuffer, point, pPipelineLayout, uSet, pSet, uDynamicOffsetCount, auDynamicOffsets);
            }
            else
            {
#if BIND_OPTIMIZE_ON

                uint32_t frmId = NSEngine::GetRenderFrameMoveLoopCount();
                struct _last_descriptor
                {
                    uint32_t            cmdId = 0;
                    VkPipelineBindPoint point = VK_PIPELINE_BIND_POINT_GRAPHICS;
                    uint64_t            pPipelineLayout = 0;
                    uint32_t            uSet = 0;
                    uint64_t            pSet = 0;
                    uint32_t            frmId = 0;
                };
                static _last_descriptor last_descriptor;
                _last_descriptor& last = last_descriptor;

                if (last.frmId == frmId && last.cmdId == cmdId && last.point == point && last.pPipelineLayout == (uint64_t)pPipelineLayout + (uint64_t)pLayout && last.uSet == uSet && last.pSet == (uint64_t)pSet + (uint64_t)pDescriptorSet)
                {
                    // 优化
                    return;
                }
                last.frmId = frmId;
                last.cmdId = cmdId;
                last.point = point;
                last.pPipelineLayout = (uint64_t)pPipelineLayout + (uint64_t)pLayout;
                last.uSet = uSet;
                last.pSet = (uint64_t)pSet + (uint64_t)pDescriptorSet;

#endif

                m_GraphicsCmdRecorder->CmdBindDescriptorSets(m_pVkCmdBuffer, point, pPipelineLayout, uSet, pSet, 0, nullptr);
            }
        }
    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdPushConstants(const KVulkanLayout* pLayout, gfx::ShaderStageType eShaderStage, int nOffset, int nSize, const void* pValues)
    {
        PROF_CPU_DETAIL();

        KGLOG_ASSERT_EXIT(pLayout);
        KGLOG_ASSERT_EXIT(nOffset >= 0 && nSize > 0 && pValues);
        CHECK_ASSERT(m_pVkCmdBuffer);

        {
            VkPipelineLayout pPipelineLayout = pLayout->GetPipelineLayout();
            VkShaderStageFlags shaderStageFlag = GetShaderStageFlag(eShaderStage);

            m_GraphicsCmdRecorder->CmdPushConstants(m_pVkCmdBuffer, pPipelineLayout, shaderStageFlag, nOffset, nSize, pValues);
        }

    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdSetDepthBias(float fConstant, float fClamp, float fSlop, bool bAutoReverse /*= true*/)
    {
        PROF_CPU_DETAIL();

        CHECK_ASSERT(m_pVkCmdBuffer);
        if (DrvOption::bReversePerspectiveDepthZ && bAutoReverse)
        {
            m_GraphicsCmdRecorder->CmdSetDepthBias(m_pVkCmdBuffer, -fConstant, -fClamp, -fSlop);
        }
        else
        {
            m_GraphicsCmdRecorder->CmdSetDepthBias(m_pVkCmdBuffer, fConstant, fClamp, fSlop);
        }
    }

    BOOL KVulkanRenderContext::CmdDispatch(int nGroupCountX, int nGroupCountY, int nGroupCountZ)
    {
        PROF_CPU_DETAIL();

        BOOL bResult = FALSE;

        CHECK_ASSERT(m_pVkCmdBuffer);

        KGLOG_ASSERT_EXIT(nGroupCountX > 0 && nGroupCountY > 0 && nGroupCountZ > 0);
        vks::vkCmdDispatch(m_pVkCmdBuffer, nGroupCountX, nGroupCountY, nGroupCountZ);

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanRenderContext::CmdDispatchIndirect(gfx::IKGFX_Buffer* pIndirectBuffer, int nOffset)
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(m_pVulkanCmdBuffer);

        CHECK_ASSERT(m_pVkCmdBuffer);

        KVulkanBuffer* pVulkanIderiectCommandBuffer = (KVulkanBuffer*)pIndirectBuffer;
        CHECK_ASSERT(pVulkanIderiectCommandBuffer);

        VkBuffer pInDirectBuffer = pVulkanIderiectCommandBuffer->GetVkBuffer();
        CHECK_ASSERT(pInDirectBuffer);

        Transition({ pIndirectBuffer, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::IndirectArgs });

        vks::vkCmdDispatchIndirect(m_pVkCmdBuffer, pInDirectBuffer, nOffset);
        return true;
    }

    void KVulkanRenderContext::BeginDebugLabel(const char* szDebugLabel)
    {
        CHECK_ASSERT(m_pVkCmdBuffer);
        m_GraphicsCmdRecorder->BeginDebugLabel(m_pVkCmdBuffer, szDebugLabel);
        return;
    }

    void KVulkanRenderContext::EndDebugLabel()
    {
        CHECK_ASSERT(m_pVkCmdBuffer);
        m_GraphicsCmdRecorder->EndDebugLabel(m_pVkCmdBuffer);
        return;
    }

    void KVulkanRenderContext::BeginOptickProfile()
    {
        CHECK_ASSERT(m_bBegun);
        m_GraphicsCmdRecorder->BeginOptickProfile();
        return;
    }

    void KVulkanRenderContext::EndOptickProfile()
    {
        CHECK_ASSERT(m_bBegun);
        m_GraphicsCmdRecorder->EndOptickProfile();
        return;
    }

    BOOL KVulkanRenderContext::CmdInsertSignalFence(KSignalFence* pSignalFence)
    {
        PROF_CPU_DETAIL();

        KVulkanSignalFence* pKSignalFence = (KVulkanSignalFence*)pSignalFence;
        return pKSignalFence->Submit(m_pVulkanCmdBuffer->m_fence);
    }

    void KVulkanRenderContext::CmdClose()
    {
        m_pVulkanCmdBuffer->End();
    }

    void KVulkanRenderContext::CmdBeginUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count)
    {
        KG3D_Vector<KGfxBarrier, 4> vecBarriers;
        KG3D_Vector<KVulkanTexture*, 4> vecTextures;
        KG3D_Vector<KVulkanBuffer*, 4> vecBuffers;

        CHECK_ASSERT(ppResourceUAV && count > 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (ppResourceUAV[i] == nullptr)
                continue;

            if (auto pTexture = dynamic_cast<KVulkanTexture*>(ppResourceUAV[i]))
            {
                vecBarriers.push_back(KGfxBarrier(pTexture, KGfxAccess::Unknown, KGfxAccess::UAVMask));
                vecTextures.push_back(pTexture);
            }
            else if (auto pBuffer = dynamic_cast<KVulkanBuffer*>(ppResourceUAV[i]))
            {
                vecBarriers.push_back(KGfxBarrier(pBuffer, KGfxAccess::Unknown, KGfxAccess::UAVMask));
                vecBuffers.push_back(pBuffer);
            }
        }

        if (vecBarriers.empty())
            return;

        m_UAVOverlapCounter += vecBarriers.size();

        // 由于BeginEndUAVOverlap作用域中不再关心UAV覆写的问题，所以进入作用域之前需要保证UAV的数据同步
        Transition(vecBarriers.data(), vecBarriers.size());

        for (auto& iter : vecTextures)
        {
            iter->EnableUAVOverlap(true);
        }

        for (auto& iter : vecBuffers)
        {
            iter->EnableUAVOverlap(true);
        }
    }

    void KVulkanRenderContext::CmdEndUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count)
    {
        CHECK_ASSERT(ppResourceUAV && count > 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (ppResourceUAV[i] == nullptr)
                continue;

            if (auto pTexture = dynamic_cast<KVulkanTexture*>(ppResourceUAV[i]))
            {
                pTexture->EnableUAVOverlap(false);
                --m_UAVOverlapCounter;
            }
            else if (auto pBuffer = dynamic_cast<KVulkanBuffer*>(ppResourceUAV[i]))
            {
                pBuffer->EnableUAVOverlap(false);
                --m_UAVOverlapCounter;
            }
        }
    }

    uint64_t KVulkanRenderContext::GetCurRenderPassCount() const
    {
        return IsInRenderPass() ? m_nCurRenderPassCount : (uint64_t)-1;
    }

    BOOL KVulkanTransitionImp(KVulkanCommandBuffer* pCmdBuffer, const KGfxBarrier* pBarrierInfos, uint32_t uBarrierCount);

    BOOL KVulkanRenderContext::Transition(const KGfxBarrier& sBarrierInfo)
    {
        PROF_CPU_DETAIL();
        return KVulkanTransitionImp(m_pVulkanCmdBuffer , &sBarrierInfo, 1);
    }

    BOOL KVulkanRenderContext::Transition(const std::initializer_list<KGfxBarrier>& BarrierInfosArray)
    {
        PROF_CPU_DETAIL();
        return KVulkanTransitionImp(m_pVulkanCmdBuffer, BarrierInfosArray.begin(), (uint32_t)BarrierInfosArray.size());
    }

    BOOL KVulkanRenderContext::Transition(const KGfxBarrier* pBarrierInfos, uint32_t uBarrierCount)
    {
        PROF_CPU_DETAIL();
        return KVulkanTransitionImp(m_pVulkanCmdBuffer, pBarrierInfos, uBarrierCount);
    }

    void KVulkanRenderContext::CmdUpdateSubResource(
        IKGFX_Buffer* DstBuffer,
        uint32_t    DstOffset,
        uint32_t    SrcSize,
        const void* SrcData,
        uint32_t    option /*= 0*/
    )
    {
        PROF_CPU_DETAIL();

        if (DstBuffer->IsDynamic())
        {
            DstBuffer->Update(SrcData, SrcSize, DstOffset, false);
        }
        else
        {
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)gfx::GetGraphicDevice();
            pGraphicDevice->CmdUpdateSubResource(this, DstBuffer, DstOffset, SrcSize, SrcData);
        }
    }

    void KVulkanRenderContext::CmdUpdateSubResource(IKGFX_TextureResource* pGfxTexture, uint32_t uDstMipLevel, uint32_t uDstArraySlice, const KGfxCopyRegion* pDstRegion, const void* pSrcData, uint32_t uSrcRowPitch, uint32_t uSrcDepthPitch)
    {
        PROF_CPU_DETAIL();
        gfx::KVulkanGraphicDevice* pGraphicDevice = (gfx::KVulkanGraphicDevice*)GetGraphicDevice();
        pGraphicDevice->CmdUpdateSubResource(this, pGfxTexture, uDstMipLevel, uDstArraySlice, pDstRegion, pSrcData, uSrcRowPitch, uSrcDepthPitch);
    }

    void KVulkanRenderContext::CmdUpdateAllResource(IKGFX_TextureResource* pGfxTexture, std::vector<gfx::KGfxSubResourceData>& data)
    {
        PROF_CPU_DETAIL();
        gfx::KVulkanGraphicDevice* pGraphicDevice = (gfx::KVulkanGraphicDevice*)GetGraphicDevice();
        pGraphicDevice->CmdUpdateAllResource(this, pGfxTexture, data);
    }

    void KVulkanRenderContext::CmdCopyBuffer(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer)
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(m_pVkCmdBuffer);

        KVulkanBuffer* pSrcBuf = (KVulkanBuffer*)pSrcBuffer;
        KVulkanBuffer* pDstBuf = (KVulkanBuffer*)pDstBuffer;

        VkBuffer pVkSrcBuffer = pSrcBuf->GetVkBuffer();
        VkBuffer pVkDstBuffer = pDstBuf->GetVkBuffer();

        auto& dstBufDesc = *pDstBuf->GetDesc();
        auto& srcBufDesc = *pSrcBuf->GetDesc();

        VkBufferCopy copyRegion;

        KGLOG_ASSERT_EXIT(srcBufDesc.eResAccessFlags != KGfxResourceAccessType::KGfxResourceAccess_Read);
        KGLOG_ASSERT_EXIT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(pVkSrcBuffer);
        KGLOG_ASSERT_EXIT(pVkDstBuffer);
        KGLOG_ASSERT_EXIT(srcBufDesc.uByteWidth <= dstBufDesc.uByteWidth);

        copyRegion.dstOffset = 0;
        copyRegion.srcOffset = 0;
        copyRegion.size = std::min(srcBufDesc.uByteWidth, dstBufDesc.uByteWidth);

        Transition({
            { pDstBuf, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst },
            { pSrcBuf, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopySrc }
        });

        if (dstBufDesc.eResAccessFlags == KGfxResourceAccessType::KGfxResourceAccess_Read)
        {
            vks::vkCmdCopyBuffer(m_pVkCmdBuffer, pVkSrcBuffer, pVkDstBuffer, 1, &copyRegion);
        }
        else
        {
            vks::vkCmdCopyBuffer(m_pVkCmdBuffer, pVkSrcBuffer, pVkDstBuffer, 1, &copyRegion);
        }

    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdCopyBufferSubRegions(
        IKGFX_Buffer* pSrcBuffer,
        IKGFX_Buffer* pDstBuffer,
        uint32_t uCopyRegionCount,
        const KBufferCopyRegion* pCopyRegions
    )
    {
        PROF_CPU_DETAIL();

        KVulkanBuffer* pSrcBuf = (KVulkanBuffer*)pSrcBuffer;
        KVulkanBuffer* pDstBuf = (KVulkanBuffer*)pDstBuffer;

        VkBuffer pVkSrcBuffer = pSrcBuf->GetVkBuffer();
        VkBuffer pVkDstBuffer = pDstBuf->GetVkBuffer();

        auto& dstBufDesc = *pDstBuf->GetDesc();
        auto& srcBufDesc = *pSrcBuf->GetDesc();

        KGLOG_ASSERT_EXIT(srcBufDesc.eResAccessFlags != KGfxResourceAccessType::KGfxResourceAccess_Read);
        KGLOG_ASSERT_EXIT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(pVkSrcBuffer);
        KGLOG_ASSERT_EXIT(pVkDstBuffer);
        KGLOG_ASSERT_EXIT(srcBufDesc.uByteWidth <= dstBufDesc.uByteWidth);

        Transition({
            { pDstBuf, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst },
            { pSrcBuf, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopySrc }
        });

        if (uCopyRegionCount == 0)
        {
            VkBufferCopy copyRegion;

            copyRegion.dstOffset = 0;
            copyRegion.srcOffset = 0;
            copyRegion.size = std::min(srcBufDesc.uByteWidth, dstBufDesc.uByteWidth);

            vks::vkCmdCopyBuffer(m_pVkCmdBuffer, pVkSrcBuffer, pVkDstBuffer, 1, &copyRegion);
        }
        else if (uCopyRegionCount == 1)
        {
            VkBufferCopy cpy;
            cpy.dstOffset = pCopyRegions[0].uDstOffset;
            cpy.srcOffset = pCopyRegions[0].uSrcOffset;
            cpy.size = pCopyRegions[0].uSize;
            vks::vkCmdCopyBuffer(m_pVkCmdBuffer, pVkSrcBuffer, pVkDstBuffer, 1, &cpy);
        }
        else
        {
            KG3D_Vector<VkBufferCopy, 8> vecCpy;
            vecCpy.reserve(uCopyRegionCount);
            for (uint32_t i = 0; i < uCopyRegionCount; ++i)
            {
                VkBufferCopy cpy;
                cpy.dstOffset = pCopyRegions[i].uDstOffset;
                cpy.srcOffset = pCopyRegions[i].uSrcOffset;
                cpy.size = pCopyRegions[i].uSize;
                vecCpy.push_back(cpy);
            }
            vks::vkCmdCopyBuffer(m_pVkCmdBuffer, pVkSrcBuffer, pVkDstBuffer, uCopyRegionCount, vecCpy.data());
        }

    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdCopyTexture(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture)
    {
        PROF_CPU_DETAIL();

        KVulkanTexture* pGfxSrcTexture = (KVulkanTexture*)pSrcTexture;
        KVulkanTexture* pGfxDstTexture = (KVulkanTexture*)pDstTexture;

        VkImage pVkSrcImage = pGfxSrcTexture->GetVkHandle();
        VkImage pVkDstImage = pGfxDstTexture->GetVkHandle();

        auto& dstDesc = *pGfxSrcTexture->GetDesc();
        auto& srcDesc = *pGfxSrcTexture->GetDesc();

        VkImageAspectFlags srcAspectFlags = pGfxSrcTexture->GetAspectFlags();
        VkImageAspectFlags dstAspectFlags = pGfxDstTexture->GetAspectFlags();

        VkImageCopy copyRegions[16];

        KGLOG_ASSERT_EXIT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(pVkSrcImage);
        KGLOG_ASSERT_EXIT(pVkDstImage);
        KGLOG_ASSERT_EXIT(srcDesc.uWidth <= dstDesc.uWidth && srcDesc.uHeight <= dstDesc.uHeight && srcDesc.uDepth <= dstDesc.uDepth);
        KGLOG_ASSERT_EXIT(srcDesc.uMipLevels <= dstDesc.uMipLevels);
        KGLOG_ASSERT_EXIT(srcDesc.uArraySize <= dstDesc.uArraySize);
        KGLOG_ASSERT_EXIT(srcAspectFlags == dstAspectFlags);

        ZeroMemory(copyRegions, sizeof(copyRegions));

        copyRegions[0].srcSubresource.aspectMask = srcAspectFlags;
        copyRegions[0].srcSubresource.mipLevel = 0;
        copyRegions[0].srcSubresource.baseArrayLayer = 0;
        copyRegions[0].srcSubresource.layerCount = srcDesc.uArraySize;
        copyRegions[0].srcOffset.x = 0;
        copyRegions[0].srcOffset.y = 0;
        copyRegions[0].srcOffset.z = 0;
        copyRegions[0].dstSubresource.aspectMask = dstAspectFlags;
        copyRegions[0].dstSubresource.mipLevel = 0;
        copyRegions[0].dstSubresource.baseArrayLayer = 0;
        copyRegions[0].dstSubresource.layerCount = srcDesc.uArraySize;
        copyRegions[0].dstOffset.x = 0;
        copyRegions[0].dstOffset.y = 0;
        copyRegions[0].dstOffset.z = 0;
        copyRegions[0].extent.width = srcDesc.uWidth;
        copyRegions[0].extent.height = srcDesc.uHeight;
        copyRegions[0].extent.depth = srcDesc.uDepth;

        for (uint32_t i = 1; i < srcDesc.uMipLevels; i++)
        {
            auto& iter = copyRegions[i];
            iter.srcSubresource.aspectMask = srcAspectFlags;
            iter.srcSubresource.mipLevel = i;
            iter.srcSubresource.baseArrayLayer = 0;
            iter.srcSubresource.layerCount = srcDesc.uArraySize;
            iter.srcOffset.x = 0;
            iter.srcOffset.y = 0;
            iter.srcOffset.z = 0;
            iter.dstSubresource.aspectMask = dstAspectFlags;
            iter.dstSubresource.mipLevel = i;
            iter.dstSubresource.baseArrayLayer = 0;
            iter.dstSubresource.layerCount = srcDesc.uArraySize;
            iter.dstOffset.x = 0;
            iter.dstOffset.y = 0;
            iter.dstOffset.z = 0;
            iter.extent.width = srcDesc.uWidth >> i;
            iter.extent.height = srcDesc.uHeight >> i;
            iter.extent.depth = srcDesc.uDepth >> i;

            iter.extent.width = std::max(1u, iter.extent.width);
            iter.extent.height = std::max(1u, iter.extent.height);
            iter.extent.depth = std::max(1u, iter.extent.depth);
        }

        Transition({
            { pDstTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst },
            { pSrcTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopySrc }
        });

        // make sure the src texture in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL layout and dst in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
        vks::vkCmdCopyImage(
            m_pVkCmdBuffer,
            pVkSrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            pVkDstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            srcDesc.uMipLevels, copyRegions
        );

    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdCopyTextureSubRegions(
        IKGFX_TextureResource* pSrcTexture,
        IKGFX_TextureResource* pDstTexture,
        uint32_t uCopyRegionCount,
        const KTextureCopyRegion* pCopyRegions
    )
    {
        PROF_CPU_DETAIL();

        KVulkanTexture* pGfxSrcTexture = (KVulkanTexture*)pSrcTexture;
        KVulkanTexture* pGfxDstTexture = (KVulkanTexture*)pDstTexture;

        VkImage pVkSrcImage = pGfxSrcTexture->GetVkHandle();
        VkImage pVkDstImage = pGfxDstTexture->GetVkHandle();

        auto& dstDesc = *pGfxSrcTexture->GetDesc();
        auto& srcDesc = *pGfxSrcTexture->GetDesc();

        VkImageAspectFlags srcAspectFlags = pGfxSrcTexture->GetAspectFlags();
        VkImageAspectFlags dstAspectFlags = pGfxDstTexture->GetAspectFlags();

        KGLOG_ASSERT_EXIT(m_pVkCmdBuffer);
        KGLOG_ASSERT_EXIT(pVkSrcImage);
        KGLOG_ASSERT_EXIT(pVkDstImage);
        KGLOG_ASSERT_EXIT(srcAspectFlags == dstAspectFlags);

        {
            KG3D_Vector<VkImageCopy, 8> vecImageCpy;
            KG3D_Vector<KGfxBarrier, 16> vecBarrier;

            vecImageCpy.clear();
            vecImageCpy.reserve(uCopyRegionCount);
            vecBarrier.reserve(uCopyRegionCount * 2);

            for (uint32_t i = 0; i < uCopyRegionCount; ++i)
            {
                VkImageCopy cpy = {};
                auto dstRegion = pCopyRegions[i];

                cpy.srcSubresource.aspectMask = srcAspectFlags;
                cpy.srcSubresource.mipLevel = dstRegion.srcMipLevel;
                cpy.srcSubresource.baseArrayLayer = dstRegion.srcArraySlice;
                cpy.srcSubresource.layerCount = 1;
                cpy.srcOffset.x = dstRegion.srcLeft;
                cpy.srcOffset.y = dstRegion.srcTop;
                cpy.srcOffset.z = dstRegion.srcFront;
                cpy.dstSubresource.aspectMask = dstAspectFlags;
                cpy.dstSubresource.mipLevel = dstRegion.dstMipLevel;
                cpy.dstSubresource.baseArrayLayer = dstRegion.dstArraySlice;
                cpy.dstSubresource.layerCount = 1;
                cpy.dstOffset.x = dstRegion.dstLeft;
                cpy.dstOffset.y = dstRegion.dstTop;
                cpy.dstOffset.z = dstRegion.dstFront;
                cpy.extent.width = dstRegion.extentWidth;
                cpy.extent.height = dstRegion.extentHeight;
                cpy.extent.depth = dstRegion.extentDepth;

                vecImageCpy.push_back(cpy);

                vecBarrier.push_back({ pGfxSrcTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopySrc, dstRegion.srcMipLevel, dstRegion.srcArraySlice, 1, 1 });
                vecBarrier.push_back({ pGfxDstTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst, dstRegion.dstMipLevel, dstRegion.dstArraySlice, 1, 1 });
            }

            if (!vecBarrier.empty())
            {
                Transition(vecBarrier.data(), (uint32_t)vecBarrier.size());
            }

            // make sure the src texture in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL layout and dst in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
            vks::vkCmdCopyImage(
                m_pVkCmdBuffer,
                pVkSrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                pVkDstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                uCopyRegionCount, vecImageCpy.data()
            );
        }

    Exit0:
        return;
    }

    void KVulkanRenderContext::CmdCopyTextureToBuffer(IKGFX_TextureResource* pSrcTexture, IKGFX_Buffer* pDstBuffer, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy)
    {
        CHECK_ASSERT(pSrcTexture);
        CHECK_ASSERT(pDstBuffer);

        std::vector<VkBufferImageCopy> vksBufferTextureCopy;

        if (pBufferTextureCopy == nullptr && NumBufferTextureCopy == 0)
        {
            auto  texDesc = pSrcTexture->GetDesc();
            auto  bufferDesc = pDstBuffer->GetDesc();
            auto& texFormatInfo = GetTextureFormatInfoVk(texDesc->eFormat);

            VkImageAspectFlags aspectFlags = GetAspectFlagsFromFormat(texDesc->eFormat);

            // TODO: 这里要不要判断下 buffer 大小适不适配呢，
            /*const uint32_t uNumRequestedPixels = texDesc->uWidth * texDesc->uHeight;
            const uint32_t uPixelByteSize = GetNumBitsPerPixel(texDesc->eFormat) / 8;
            const uint32_t uBufferSize = uNumRequestedPixels * uPixelByteSize;
            CHECK_ASSERT(bufferDesc->uByteWidth >= uBufferSize);*/

            uint32_t bufferOffset = 0;

            for (uint32_t uArr = 0; uArr < texDesc->uArraySize; ++uArr)
            {
                for (uint32_t uMip = 0; uMip < texDesc->uMipLevels; ++uMip)
                {
                    VkBufferImageCopy vksCopy;
                    vksCopy.bufferOffset = bufferOffset;
                    vksCopy.bufferRowLength = 0;
                    vksCopy.bufferImageHeight = 0;
                    vksCopy.imageSubresource.aspectMask = aspectFlags;
                    vksCopy.imageSubresource.mipLevel = uMip;
                    vksCopy.imageSubresource.baseArrayLayer = uArr;
                    vksCopy.imageSubresource.layerCount = 1;
                    vksCopy.imageOffset = { 0, 0, 0 };
                    vksCopy.imageExtent.width = texDesc->uWidth >> uMip;
                    vksCopy.imageExtent.height = texDesc->uHeight >> uMip;
                    vksCopy.imageExtent.depth = 1;
                    vksBufferTextureCopy.push_back(vksCopy);

                    bufferOffset += vksCopy.imageExtent.width * vksCopy.imageExtent.height * texFormatInfo.uBytesPerBlock;
                }
            }
        }
        else
        {
            auto               texDesc = pSrcTexture->GetDesc();
            VkImageAspectFlags aspectFlags = GetAspectFlagsFromFormat(texDesc->eFormat);

            vksBufferTextureCopy.resize(NumBufferTextureCopy);
            for (uint32_t i = 0; i < NumBufferTextureCopy; ++i)
            {
                const KBufferTextureCopy& copy = pBufferTextureCopy[i];
                VkBufferImageCopy& vksCopy = vksBufferTextureCopy[i];
                vksCopy.bufferOffset = copy.bufferOffset;
                vksCopy.bufferRowLength = copy.bufferRowLength;
                vksCopy.bufferImageHeight = copy.bufferImageHeight;
                vksCopy.imageSubresource.aspectMask = aspectFlags;
                vksCopy.imageSubresource.mipLevel = copy.textureMipLevel;
                vksCopy.imageSubresource.baseArrayLayer = copy.textureArraySlice;
                vksCopy.imageSubresource.layerCount = 1;
                vksCopy.imageOffset.x = copy.textureCopyRegion.left;
                vksCopy.imageOffset.y = copy.textureCopyRegion.top;
                vksCopy.imageOffset.z = copy.textureCopyRegion.front;
                vksCopy.imageExtent.width = copy.textureCopyRegion.right - copy.textureCopyRegion.left;
                vksCopy.imageExtent.height = copy.textureCopyRegion.bottom - copy.textureCopyRegion.top;
                vksCopy.imageExtent.depth = copy.textureCopyRegion.back - copy.textureCopyRegion.front;
            }
        }

        Transition({
            { pSrcTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopySrc },
            { pDstBuffer, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst }
        });

        vks::vkCmdCopyImageToBuffer(
            m_pVulkanCmdBuffer->GetCommandBuffer(),
            (VkImage)pSrcTexture->GetNativeResourceHandle(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            (VkBuffer)pDstBuffer->GetNativeResourceHandle(),
            (uint32_t)vksBufferTextureCopy.size(),
            vksBufferTextureCopy.data()
        );
    }

    void KVulkanRenderContext::CmdCopyBufferToTexture(IKGFX_Buffer* pSrcBuffer, IKGFX_TextureResource* pDstTexture, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy)
    {
        CHECK_ASSERT(pSrcBuffer);
        CHECK_ASSERT(pDstTexture);

        std::vector<VkBufferImageCopy> vksBufferTextureCopy;
        if (pBufferTextureCopy == nullptr && NumBufferTextureCopy == 0)
        {
            auto  texDesc = pDstTexture->GetDesc();
            auto& texFormatInfo = GetTextureFormatInfoVk(texDesc->eFormat);

            uint32_t bufferOffset = 0;

            for (uint32_t uArr = 0; uArr < texDesc->uArraySize; ++uArr)
            {
                for (uint32_t uMip = 0; uMip < texDesc->uMipLevels; ++uMip)
                {
                    VkBufferImageCopy vksCopy;
                    vksCopy.bufferOffset = bufferOffset;
                    vksCopy.bufferRowLength = 0;
                    vksCopy.bufferImageHeight = 0;
                    vksCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    vksCopy.imageSubresource.mipLevel = uMip;
                    vksCopy.imageSubresource.baseArrayLayer = uArr;
                    vksCopy.imageSubresource.layerCount = 1;
                    vksCopy.imageOffset = { 0, 0, 0 };
                    vksCopy.imageExtent.width = texDesc->uWidth >> uMip;
                    vksCopy.imageExtent.height = texDesc->uHeight >> uMip;
                    vksCopy.imageExtent.depth = 1;
                    vksBufferTextureCopy.push_back(vksCopy);

                    bufferOffset += vksCopy.imageExtent.width * vksCopy.imageExtent.height * texFormatInfo.uBytesPerBlock;
                }
            }
        }
        else
        {
            vksBufferTextureCopy.resize(NumBufferTextureCopy);
            for (uint32_t i = 0; i < NumBufferTextureCopy; ++i)
            {
                const KBufferTextureCopy& copy = pBufferTextureCopy[i];
                VkBufferImageCopy& vksCopy = vksBufferTextureCopy[i];
                vksCopy.bufferOffset = copy.bufferOffset;
                vksCopy.bufferRowLength = copy.bufferRowLength;
                vksCopy.bufferImageHeight = copy.bufferImageHeight;
                vksCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                vksCopy.imageSubresource.mipLevel = copy.textureMipLevel;
                vksCopy.imageSubresource.baseArrayLayer = copy.textureArraySlice;
                vksCopy.imageSubresource.layerCount = 1;
                vksCopy.imageOffset.x = copy.textureCopyRegion.left;
                vksCopy.imageOffset.y = copy.textureCopyRegion.top;
                vksCopy.imageOffset.z = copy.textureCopyRegion.front;
                vksCopy.imageExtent.width = copy.textureCopyRegion.right - copy.textureCopyRegion.left;
                vksCopy.imageExtent.height = copy.textureCopyRegion.bottom - copy.textureCopyRegion.top;
                vksCopy.imageExtent.depth = copy.textureCopyRegion.back - copy.textureCopyRegion.front;
            }
        }

        Transition({
            { pSrcBuffer, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopySrc },
            { pDstTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst }
            });

        vks::vkCmdCopyBufferToImage(
            m_pVulkanCmdBuffer->GetCommandBuffer(),
            (VkBuffer)pSrcBuffer->GetNativeResourceHandle(),
            (VkImage)pDstTexture->GetNativeResourceHandle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            (uint32_t)vksBufferTextureCopy.size(),
            vksBufferTextureCopy.data()
        );
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    KVulkanGraphicsCommandRecorder::KVulkanGraphicsCommandRecorder(KVulkanRenderContext* pRenderCtx)
    {
        CHECK_ASSERT(pRenderCtx);
        m_pCurrentRenderCtx = pRenderCtx;
    }

    KVulkanGraphicsCommandRecorder::~KVulkanGraphicsCommandRecorder()
    {
        SAFE_DELETE(m_PoolDrawCmds);
        SAFE_DELETE(m_PoolRenderStateCmds);
        SAFE_DELETE(m_PoolPushConstantCmds);
        SAFE_DELETE(m_PoolVertexBindCmds);
        SAFE_DELETE(m_PoolIndexBindCmds);
        SAFE_DELETE(m_PoolDescriptorSetCmds);
        SAFE_DELETE(m_PoolPipelineCmds);
        SAFE_DELETE(m_PoolClearAttachmentsCmds);
        SAFE_DELETE(m_PoolBeginDebugLabelCmds);
        SAFE_DELETE(m_PoolEndDebugLabelCmds);
        SAFE_DELETE(m_PoolBeginOptickProfileCmds);
        SAFE_DELETE(m_PoolEndOptickProfileCmds);
    }

    void KVulkanGraphicsCommandRecorder::Begin(const VkRenderPassBeginInfo& InBeginInfo, VkSubpassContents ePassContents, bool bImmediateMode)
    {
        CHECK_ASSERT(!m_bBeginRecord);

        CHECK_ASSERT(m_pCurrentRenderCtx);
        CHECK_ASSERT(m_CmdRecordList.empty());

        m_bBeginRecord = true;
        
        m_RenderBeginInfo = InBeginInfo;
        m_ePassContents = ePassContents;
        m_bDeferredMode = !bImmediateMode;

        if (!IsDeferredMode())
        {
            VkCommandBuffer vkCmdBuffer = m_pCurrentRenderCtx->GetCommandBufferVk();
            CHECK_ASSERT(vkCmdBuffer);

            vks::vkCmdBeginRenderPass(vkCmdBuffer, &m_RenderBeginInfo, ePassContents);
        }
        else if (m_PoolDrawCmds == nullptr)
        {
            m_PoolDrawCmds = new DRAW_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolDrawCmds);

            m_PoolRenderStateCmds = new RNEDERSTATE_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolRenderStateCmds);

            m_PoolPushConstantCmds = new PUSHCONSTANT_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolPushConstantCmds);

            m_PoolVertexBindCmds = new VERTEXBIND_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolVertexBindCmds);

            m_PoolIndexBindCmds = new INDEXBIND_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolIndexBindCmds);

            m_PoolDescriptorSetCmds = new DESCRIPTORSET_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolDescriptorSetCmds);

            m_PoolPipelineCmds = new PIPELINE_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolPipelineCmds);

            m_PoolClearAttachmentsCmds = new CLEARATTACHMENTS_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolClearAttachmentsCmds);

            m_PoolBeginDebugLabelCmds = new BEGINDEBUGLABEL_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolBeginDebugLabelCmds);

            m_PoolEndDebugLabelCmds = new ENDDEBUGLABEL_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolEndDebugLabelCmds);

            m_PoolBeginOptickProfileCmds = new BEGINOPTICKPROFILE_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolBeginOptickProfileCmds);

            m_PoolEndOptickProfileCmds = new ENDOPTICKPROFILE_CMD_LIST(m_CmdEventPoolResource);
            CHECK_ASSERT(m_PoolEndOptickProfileCmds);
        }
    }

    void KVulkanGraphicsCommandRecorder::End()
    {
        CHECK_ASSERT(m_pCurrentRenderCtx);
        CHECK_ASSERT(m_bBeginRecord);

        m_bBeginRecord = false;

        if (IsDeferredMode())
        {
            vks::vkCmdBeginRenderPass(m_pCurrentRenderCtx->GetCommandBufferVk(), &m_RenderBeginInfo, m_ePassContents);
            VkCommandBuffer pCmdBuffer = m_pCurrentRenderCtx->GetCommandBufferVk();

            for (auto& iter : m_CmdRecordList)
            {
                CHECK_ASSERT(iter);
                iter->Execute(pCmdBuffer, m_pCurrentRenderCtx);
            }

            _ClearCmdRecordList();
        }

        {
            vks::vkCmdEndRenderPass(m_pCurrentRenderCtx->GetCommandBufferVk());
            m_RenderBeginInfo = {};
        }
    }

    bool KVulkanGraphicsCommandRecorder::IsInGraphicsPipelineScope() const
    {
        return m_bBeginRecord;
    }

    bool KVulkanGraphicsCommandRecorder::IsInCmdRecording() const
    {
        return m_bBeginRecord && IsDeferredMode();
    }

    bool KVulkanGraphicsCommandRecorder::IsDeferredMode() const
    {
        return m_bDeferredMode && s_bEnableGraphicsPipelineRecording;
    }

    void KVulkanGraphicsCommandRecorder::CmdClearAttachments(VkCommandBuffer pCmdBuffer, const std::vector<VkClearAttachment>& vkClearAttachments, const VkClearRect& vkClearRect)
    {
        if (IsInCmdRecording())
        {
            _PushRenderStateCmd();

            CLEARATTACHMENTS_CMD* pCmdElem = (CLEARATTACHMENTS_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_CLEARATTACHMENTS);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_CLEARATTACHMENTS);

            pCmdElem->ClearAttchments = vkClearAttachments;
            pCmdElem->ClearRect = vkClearRect;
        }
        else
        {
            vks::vkCmdClearAttachments(pCmdBuffer, (uint32_t)vkClearAttachments.size(), vkClearAttachments.data(), 1, &vkClearRect);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdSetLineWidth(VkCommandBuffer pCmdBuffer, float fLineWidth)
    {
        if (IsInCmdRecording())
        {
            m_CurRenderStateCmd.uCmdApplyElemBits = m_CurRenderStateCmd.uCmdApplyElemBits | (uint8_t)CMD_APPLY_ELEM::CMD_LINEWIDTH;
            m_CurRenderStateCmd.fLineWidth = fLineWidth;
        }
        else
        {
            vks::vkCmdSetLineWidth(pCmdBuffer, fLineWidth);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdSetDepthBias(VkCommandBuffer pCmdBuffer, float fConstant, float fClamp, float fSlope)
    {
        if (IsInCmdRecording())
        {
            m_CurRenderStateCmd.uCmdApplyElemBits = m_CurRenderStateCmd.uCmdApplyElemBits | (uint8_t)CMD_APPLY_ELEM::CMD_DEPTHBIAS;
            m_CurRenderStateCmd.DepthBias.fConstant = fConstant;
            m_CurRenderStateCmd.DepthBias.fClamp = fClamp;
            m_CurRenderStateCmd.DepthBias.fSlope = fSlope;
        }
        else
        {
            vks::vkCmdSetDepthBias(pCmdBuffer, fConstant, fClamp, fSlope);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdSetScissor(VkCommandBuffer pCmdBuffer, const VkRect2D& ScissorRect)
    {
        if (IsInCmdRecording())
        {
            m_CurRenderStateCmd.uCmdApplyElemBits = m_CurRenderStateCmd.uCmdApplyElemBits | (uint8_t)CMD_APPLY_ELEM::CMD_SCISSOR;
            m_CurRenderStateCmd.ScissorRect = ScissorRect;
        }
        else
        {
            vks::vkCmdSetScissor(pCmdBuffer, 0, 1, &ScissorRect);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdSetViewport(VkCommandBuffer pCmdBuffer, const VkViewport& Viewport)
    {
        if (IsInCmdRecording())
        {
            m_CurRenderStateCmd.uCmdApplyElemBits = m_CurRenderStateCmd.uCmdApplyElemBits | (uint8_t)CMD_APPLY_ELEM::CMD_VIEWPORT;
            m_CurRenderStateCmd.Viewport = Viewport;
        }
        else
        {
            vks::vkCmdSetViewport(pCmdBuffer, 0, 1, &Viewport);
        }
    }

    void KVulkanGraphicsCommandRecorder::_PushRenderStateCmd()
    {
        if (m_CurRenderStateCmd.uCmdApplyElemBits != 0)
        {
            RNEDERSTATE_CMD* pCmdElem = (RNEDERSTATE_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_RNEDERSTATE);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_RNEDERSTATE);

            *pCmdElem = m_CurRenderStateCmd;
            m_CurRenderStateCmd.Clear();
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdBindVertexBuffers(VkCommandBuffer pCmdBuffer, int nFirstBinding, int nBindingCount, VkBuffer Buffer[], VkDeviceSize Offsets[])
    {
        if (IsInCmdRecording())
        {
            CHECK_ASSERT(nBindingCount <= KMAX_BIND_VERT_STREAM);

            VERTEXBIND_CMD* pCmdElem = (VERTEXBIND_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_VERTEXBIND);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_VERTEXBIND);

            pCmdElem->nFirstBinding = nFirstBinding;
            pCmdElem->nBindingCount = nBindingCount;

            for (int i = 0; i < nBindingCount; ++i)
            {
                pCmdElem->vkBuffer[i] = Buffer[i];
                pCmdElem->uOffset[i] = Offsets[i];
            }
        }
        else
        {
            vks::vkCmdBindVertexBuffers(pCmdBuffer, nFirstBinding, nBindingCount, Buffer, Offsets);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdBindIndexBuffer(VkCommandBuffer pCmdBuffer, VkBuffer pBuffer, int nOffset, VkIndexType indexType)
    {
        if (IsInCmdRecording())
        {
            CHECK_ASSERT(pBuffer);

            INDEXBIND_CMD* pCmdElem = (INDEXBIND_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_INDEXBIND);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_INDEXBIND);

            pCmdElem->eIndexType = indexType;
            pCmdElem->vkBuffer = pBuffer;
            pCmdElem->nOffset = nOffset;
        }
        else
        {
            vks::vkCmdBindIndexBuffer(pCmdBuffer, pBuffer, nOffset, indexType);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdDraw(VkCommandBuffer pCmdBuffer, int nVertexCount, int nFirstVertex)
    {
        if (IsInCmdRecording())
        {
            _PushRenderStateCmd();

            DRAW_CMD* pCmdElem = (DRAW_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_DRAW);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_DRAW);

            pCmdElem->eDrawCmdType = DRAW_CMD_TYPE::CMD_DRAW;
            pCmdElem->DrawParam.Draw.nFirstVertex = nFirstVertex;
            pCmdElem->DrawParam.Draw.nVertexCount = nVertexCount;
        }
        else
        {
            vks::vkCmdDraw(pCmdBuffer, nVertexCount, 1, nFirstVertex, 0);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdDrawInstanced(VkCommandBuffer pCmdBuffer, int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance)
    {
        if (IsInCmdRecording())
        {
            _PushRenderStateCmd();

            DRAW_CMD* pCmdElem = (DRAW_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_DRAW);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_DRAW);

            pCmdElem->eDrawCmdType = DRAW_CMD_TYPE::CMD_DRAW_INSTANCED;
            pCmdElem->DrawParam.DrawInstanced.nVertexCount = nVertexCount;
            pCmdElem->DrawParam.DrawInstanced.nFirstVertex = nFirstVertex;
            pCmdElem->DrawParam.DrawInstanced.nInstanceCount = nInstanceCount;
            pCmdElem->DrawParam.DrawInstanced.nFirstInstance = nFirstInstance;
        }
        else
        {
            vks::vkCmdDraw(pCmdBuffer, nVertexCount, nInstanceCount, nFirstVertex, nFirstInstance);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdDrawIndexed(VkCommandBuffer pCmdBuffer, int nIndexCount, int nInstanceCount, int nFirstIndex, int nVertexOffset, int nFirstInstance)
    {
        if (IsInCmdRecording())
        {
            _PushRenderStateCmd();

            DRAW_CMD* pCmdElem = (DRAW_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_DRAW);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_DRAW);

            pCmdElem->eDrawCmdType = DRAW_CMD_TYPE::CMD_DRAW_INDEXED;
            pCmdElem->DrawParam.DrawIndexed.nIndexCount = nIndexCount;
            pCmdElem->DrawParam.DrawIndexed.nInstanceCount = nInstanceCount;
            pCmdElem->DrawParam.DrawIndexed.nFirstIndex = nFirstIndex;
            pCmdElem->DrawParam.DrawIndexed.nVertexOffset = nVertexOffset;
            pCmdElem->DrawParam.DrawIndexed.nFirstInstance = nFirstInstance;
        }
        else
        {
            vks::vkCmdDrawIndexed(pCmdBuffer, nIndexCount, nInstanceCount, nFirstIndex, nVertexOffset, nFirstInstance);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdDrawIndexedIndirect(VkCommandBuffer pCmdBuffer, VkBuffer pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride)
    {
        if (IsInCmdRecording())
        {
            _PushRenderStateCmd();

            DRAW_CMD* pCmdElem = (DRAW_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_DRAW);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_DRAW);

            pCmdElem->eDrawCmdType = DRAW_CMD_TYPE::CMD_DRAW_INDEXED_INDIRECT;
            pCmdElem->DrawParam.DrawIndexedIndirect.pIndirectCmdBuffer = pIndirectCmdBuffer;
            pCmdElem->DrawParam.DrawIndexedIndirect.nOffset = nOffset;
            pCmdElem->DrawParam.DrawIndexedIndirect.nDrawCount = nDrawCount;
            pCmdElem->DrawParam.DrawIndexedIndirect.nStride = nStride;
        }
        else
        {
            vks::vkCmdDrawIndexedIndirect(pCmdBuffer, pIndirectCmdBuffer, nOffset, nDrawCount, nStride);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdDrawIndirect(VkCommandBuffer pCmdBuffer, VkBuffer pIndirectCmdBuffer, int nOffset, int nDrawCount, int nStride)
    {
        if (IsInCmdRecording())
        {
            _PushRenderStateCmd();

            DRAW_CMD* pCmdElem = (DRAW_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_DRAW);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_DRAW);

            pCmdElem->eDrawCmdType = DRAW_CMD_TYPE::CMD_DRAW_INDIRECT;
            pCmdElem->DrawParam.DrawIndexedIndirect.pIndirectCmdBuffer = pIndirectCmdBuffer;
            pCmdElem->DrawParam.DrawIndexedIndirect.nOffset = nOffset;
            pCmdElem->DrawParam.DrawIndexedIndirect.nDrawCount = nDrawCount;
            pCmdElem->DrawParam.DrawIndexedIndirect.nStride = nStride;
        }
        else
        {
            vks::vkCmdDrawIndirect(pCmdBuffer, pIndirectCmdBuffer, nOffset, nDrawCount, nStride);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdBindPipeline(VkCommandBuffer pCmdBuffer, VkPipelineBindPoint eBindPoint, VkPipeline pPipeline)
    {
        if (IsInCmdRecording())
        {
            CHECK_ASSERT(eBindPoint == VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS);

            PIPELINE_CMD* pCmdElem = (PIPELINE_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_PIPELINE);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_PIPELINE);

            pCmdElem->vkPipeline = pPipeline;
        }
        else
        {
            vks::vkCmdBindPipeline(pCmdBuffer, eBindPoint, pPipeline);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdBindDescriptorSets(
        VkCommandBuffer pCmdBuffer,
        VkPipelineBindPoint eBindPoint,
        VkPipelineLayout pLayout,
        uint32_t uSet,
        VkDescriptorSet pDescriptorSet,
        uint32_t uDynamicOffsetCount,
        const uint32_t auDynamicOffsets[]
    )
    {
        if (IsInCmdRecording())
        {
            CHECK_ASSERT(eBindPoint == VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS);

            DESCRIPTORSET_CMD* pCmdElem = (DESCRIPTORSET_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_DESCRIPTORSET);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_DESCRIPTORSET);

            pCmdElem->vkLayout = pLayout;
            pCmdElem->uSet = uSet;
            pCmdElem->DescriptorSet = pDescriptorSet;

            if (uDynamicOffsetCount > 0)
            {
                pCmdElem->DynamicOffsets.resize(uDynamicOffsetCount);
                for (uint32_t i = 0; i < uDynamicOffsetCount; ++i)
                {
                    pCmdElem->DynamicOffsets[i] = auDynamicOffsets[i];
                }
            }
        }
        else
        {
            vks::vkCmdBindDescriptorSets(pCmdBuffer, eBindPoint, pLayout, uSet, 1, &pDescriptorSet, uDynamicOffsetCount, auDynamicOffsets);
        }
    }

    void KVulkanGraphicsCommandRecorder::CmdPushConstants(VkCommandBuffer pCmdBuffer, VkPipelineLayout pLayout, VkShaderStageFlags eShaderStage, int nOffset, int nSize, const void* pValues)
    {
        if (IsInCmdRecording())
        {
            CHECK_ASSERT(pValues && nSize > 0);

            PUSHCONSTANT_CMD* pCmdElem = (PUSHCONSTANT_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_PUSHCONSTANTS);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_PUSHCONSTANTS);

            pCmdElem->PushConstants.resize(nSize);
            memcpy(pCmdElem->PushConstants.data(), pValues, nSize);

            pCmdElem->PipelineLayout = pLayout;
            pCmdElem->StageFlags = eShaderStage;
            pCmdElem->nOffset = nOffset;
        }
        else
        {
            vks::vkCmdPushConstants(pCmdBuffer, pLayout, eShaderStage, nOffset, nSize, pValues);
        }
    }

    void KVulkanGraphicsCommandRecorder::BeginDebugLabel(VkCommandBuffer pCmdBuffer, const char* szDebugLabel)
    {
        if (IsInCmdRecording())
        {
            CHECK_ASSERT(szDebugLabel);

            BEGINDEBUGLABEL_CMD* pCmdElem = (BEGINDEBUGLABEL_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_BEGINDEBUGLABEL);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_BEGINDEBUGLABEL);

            pCmdElem->szDebugLabel = szDebugLabel;
        }
        else
        {
            VkDebugUtilsLabelEXT markerInfo = {};
            markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            markerInfo.pLabelName = szDebugLabel;
            vks::vkCmdBeginDebugUtilsLabelEXT(pCmdBuffer, &markerInfo);
        }
    }

    void KVulkanGraphicsCommandRecorder::EndDebugLabel(VkCommandBuffer pCmdBuffer)
    {
        if (IsInCmdRecording())
        {
            ENDDEBUGLABEL_CMD* pCmdElem = (ENDDEBUGLABEL_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_ENDDEBUGLABEL);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_ENDDEBUGLABEL);
        }
        else
        {
            vks::vkCmdEndDebugUtilsLabelEXT(pCmdBuffer);
        }
    }

    void KVulkanGraphicsCommandRecorder::BeginOptickProfile()
    {
        if (IsInCmdRecording())
        {
            BEGINOPTICKPROFILE_CMD* pCmdElem = (BEGINOPTICKPROFILE_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_BEGINOPTICKPROFILE);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_BEGINOPTICKPROFILE);
        }
        else
        {
            auto CmdBuffer = m_pCurrentRenderCtx->GetVulkanCommandBuffer();
            CHECK_ASSERT(CmdBuffer);

            CmdBuffer->OptickBeginGpuContext();
        }
    }

    void KVulkanGraphicsCommandRecorder::EndOptickProfile()
    {
        if (IsInCmdRecording())
        {
            ENDOPTICKPROFILE_CMD* pCmdElem = (ENDOPTICKPROFILE_CMD*)_RequestCmdElem(CMD_ELEM_TYPE::CMD_ELEM_ENDOPTICKPROFILE);
            CHECK_ASSERT(pCmdElem);
            CHECK_ASSERT(pCmdElem->eCmdElemType == CMD_ELEM_TYPE::CMD_ELEM_ENDOPTICKPROFILE);
        }
        else
        {
            auto CmdBuffer = m_pCurrentRenderCtx->GetVulkanCommandBuffer();
            CHECK_ASSERT(CmdBuffer);

            CmdBuffer->OptickEndGpuContext();
        }
    }

    void KVulkanGraphicsCommandRecorder::_ClearCmdRecordList()
    {
        m_CmdRecordList.clear();

        if (m_PoolDrawCmds)
        {
            m_PoolDrawCmds->Clear();
            m_PoolRenderStateCmds->Clear();
            m_PoolPushConstantCmds->Clear();
            m_PoolVertexBindCmds->Clear();
            m_PoolIndexBindCmds->Clear();
            m_PoolDescriptorSetCmds->Clear();
            m_PoolPipelineCmds->Clear();
            m_PoolClearAttachmentsCmds->Clear();
            m_PoolBeginDebugLabelCmds->Clear();
            m_PoolEndDebugLabelCmds->Clear();
            m_PoolBeginOptickProfileCmds->Clear();
            m_PoolEndOptickProfileCmds->Clear();
        }
    }

    KVulkanGraphicsCommandRecorder::CMD_ELEM* KVulkanGraphicsCommandRecorder::_RequestCmdElem(CMD_ELEM_TYPE eElemType)
    {
        CMD_ELEM* RetElem = nullptr;
        switch (eElemType)
        {
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_DRAW:
        {
            DRAW_CMD* RetElemImp = m_PoolDrawCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_RNEDERSTATE:
        {
            RNEDERSTATE_CMD* RetElemImp = m_PoolRenderStateCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_PUSHCONSTANTS:
        {
            PUSHCONSTANT_CMD* RetElemImp = m_PoolPushConstantCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_VERTEXBIND:
        {
            VERTEXBIND_CMD* RetElemImp = m_PoolVertexBindCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_INDEXBIND:
        {
            INDEXBIND_CMD* RetElemImp = m_PoolIndexBindCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_DESCRIPTORSET:
        {
            DESCRIPTORSET_CMD* RetElemImp = m_PoolDescriptorSetCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_PIPELINE:
        {
            PIPELINE_CMD* RetElemImp = m_PoolPipelineCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_CLEARATTACHMENTS:
        {
            CLEARATTACHMENTS_CMD* RetElemImp = m_PoolClearAttachmentsCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_BEGINDEBUGLABEL:
        {
            BEGINDEBUGLABEL_CMD* RetElemImp = m_PoolBeginDebugLabelCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_ENDDEBUGLABEL:
        {
            ENDDEBUGLABEL_CMD* RetElemImp = m_PoolEndDebugLabelCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_BEGINOPTICKPROFILE:
        {
            BEGINOPTICKPROFILE_CMD* RetElemImp = m_PoolBeginOptickProfileCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        case gfx::KVulkanGraphicsCommandRecorder::CMD_ELEM_TYPE::CMD_ELEM_ENDOPTICKPROFILE:
        {
            ENDOPTICKPROFILE_CMD* RetElemImp = m_PoolEndOptickProfileCmds->Alloc();
            CHECK_ASSERT(RetElemImp);
            RetElem = RetElemImp;
            break;
        }
        default:
        {
            DEBUG_BREAK();
            return nullptr;
        }
        }

        m_CmdRecordList.push_back(RetElem);
        return RetElem;
    }

    void KVulkanGraphicsCommandRecorder::DRAW_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        switch (eDrawCmdType)
        {
        case gfx::KVulkanGraphicsCommandRecorder::DRAW_CMD_TYPE::CMD_DRAW:
            vks::vkCmdDraw(pCmdBuffer, DrawParam.Draw.nVertexCount, 1, DrawParam.Draw.nFirstVertex, 0);
            break;
        case gfx::KVulkanGraphicsCommandRecorder::DRAW_CMD_TYPE::CMD_DRAW_INSTANCED:
            vks::vkCmdDraw(pCmdBuffer, DrawParam.DrawInstanced.nVertexCount, DrawParam.DrawInstanced.nInstanceCount, DrawParam.DrawInstanced.nFirstVertex, DrawParam.DrawInstanced.nFirstInstance);
            break;
        case gfx::KVulkanGraphicsCommandRecorder::DRAW_CMD_TYPE::CMD_DRAW_INDEXED:
            vks::vkCmdDrawIndexed(pCmdBuffer, DrawParam.DrawIndexed.nIndexCount, DrawParam.DrawIndexed.nInstanceCount, DrawParam.DrawIndexed.nFirstIndex, DrawParam.DrawIndexed.nVertexOffset, DrawParam.DrawIndexed.nFirstInstance);
            break;
        case gfx::KVulkanGraphicsCommandRecorder::DRAW_CMD_TYPE::CMD_DRAW_INDIRECT:
            vks::vkCmdDrawIndirect(pCmdBuffer, DrawParam.DrawIndirect.pIndirectCmdBuffer, DrawParam.DrawIndirect.nOffset, DrawParam.DrawIndirect.nDrawCount, DrawParam.DrawIndirect.nStride);
            break;
        case gfx::KVulkanGraphicsCommandRecorder::DRAW_CMD_TYPE::CMD_DRAW_INDEXED_INDIRECT:
            vks::vkCmdDrawIndexedIndirect(pCmdBuffer, DrawParam.DrawIndexedIndirect.pIndirectCmdBuffer, DrawParam.DrawIndexedIndirect.nOffset, DrawParam.DrawIndexedIndirect.nDrawCount, DrawParam.DrawIndexedIndirect.nStride);
            break;
        default:
            break;
        }
    }

    void KVulkanGraphicsCommandRecorder::RNEDERSTATE_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        if (uCmdApplyElemBits & (uint8_t)CMD_APPLY_ELEM::CMD_VIEWPORT)
        {
            vks::vkCmdSetViewport(pCmdBuffer, 0, 1, &Viewport);
        }

        if (uCmdApplyElemBits & (uint8_t)CMD_APPLY_ELEM::CMD_SCISSOR)
        {
            vks::vkCmdSetScissor(pCmdBuffer, 0, 1, &ScissorRect);
        }

        if (uCmdApplyElemBits & (uint8_t)CMD_APPLY_ELEM::CMD_DEPTHBIAS)
        {
            vks::vkCmdSetDepthBias(pCmdBuffer, DepthBias.fConstant, DepthBias.fClamp, DepthBias.fSlope);
        }

        if (uCmdApplyElemBits & (uint8_t)CMD_APPLY_ELEM::CMD_LINEWIDTH)
        {
            vks::vkCmdSetLineWidth(pCmdBuffer, fLineWidth);
        }
    }

    void KVulkanGraphicsCommandRecorder::PUSHCONSTANT_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        vks::vkCmdPushConstants(pCmdBuffer, PipelineLayout, StageFlags, nOffset, (uint32_t)PushConstants.size(), PushConstants.data());
    }

    void KVulkanGraphicsCommandRecorder::VERTEXBIND_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        vks::vkCmdBindVertexBuffers(pCmdBuffer, nFirstBinding, nBindingCount, vkBuffer, uOffset);
    }

    void KVulkanGraphicsCommandRecorder::INDEXBIND_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        vks::vkCmdBindIndexBuffer(pCmdBuffer, vkBuffer, nOffset, eIndexType);
    }

    void KVulkanGraphicsCommandRecorder::DESCRIPTORSET_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        vks::vkCmdBindDescriptorSets(pCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkLayout, uSet, 1, &DescriptorSet, (uint32_t)DynamicOffsets.size(), DynamicOffsets.data());
    }

    void KVulkanGraphicsCommandRecorder::PIPELINE_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        vks::vkCmdBindPipeline(pCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
    }

    void KVulkanGraphicsCommandRecorder::CLEARATTACHMENTS_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        vks::vkCmdClearAttachments(pCmdBuffer, (uint32_t)ClearAttchments.size(), ClearAttchments.data(), 1, &ClearRect);
    }

    void KVulkanGraphicsCommandRecorder::BEGINDEBUGLABEL_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        VkDebugUtilsLabelEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        markerInfo.pLabelName = szDebugLabel.c_str();
        vks::vkCmdBeginDebugUtilsLabelEXT(pCmdBuffer, &markerInfo);
    }

    void KVulkanGraphicsCommandRecorder::ENDDEBUGLABEL_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        vks::vkCmdEndDebugUtilsLabelEXT(pCmdBuffer);
    }

    void KVulkanGraphicsCommandRecorder::BEGINOPTICKPROFILE_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        auto CmdBuffer = pRenderCtx->GetVulkanCommandBuffer();
        CHECK_ASSERT(CmdBuffer);

        CmdBuffer->OptickBeginGpuContext();
    }

    void KVulkanGraphicsCommandRecorder::ENDOPTICKPROFILE_CMD::Execute(VkCommandBuffer pCmdBuffer, KVulkanRenderContext* pRenderCtx)
    {
        auto CmdBuffer = pRenderCtx->GetVulkanCommandBuffer();
        CHECK_ASSERT(CmdBuffer);

        CmdBuffer->OptickEndGpuContext();
    }

}

#pragma once

#include "KEnginePub/Public/IGFX_Public.h"
#include "KEnginePub/Private/IGFX_Private.h"
#include "GFXVulkan.h"

namespace gfx
{
#define memlect_RenderFrameBuffer 0
    class KVulkanRenderFrameBuffer : public IKGFX_RenderFrameBuffer, public KGFX_DelayReleaseObject
    {
    public:
        // interface KGfxRef
        int Release() override;

    public:
        // interface IKGFX_RenderFrameBuffer
        uint32_t GetWidth() const override;
        uint32_t GetHeight() const override;
        uint32_t GetRenderTargetCount() const override { return m_uRenderTargetNum; }

        BOOL BeginPass(IKGFX_RenderContext* pRenderCtx, bool bUseBarrier = true, bool bImmediateMode = false) override;
        BOOL EndPass(IKGFX_RenderContext* pRenderCtx) override;
        void SetObjectName(const char* szName) override;

    public:
        // interface KVulkanRenderFrameBuffer
        KVulkanRenderFrameBuffer();
        virtual ~KVulkanRenderFrameBuffer();

        BOOL Create(const KGfxFrameBufferDesc* pDesc);

        VkFramebuffer GetFrameBuffer();
        KVulkanRenderPass* GetRenderPassPtr();

        uint64_t GetCode();
        uint32_t GetId() const { return m_uid; }

    private:
        BOOL _BeginPass(IKGFX_RenderContext* pRenderCtx, BOOL bSecondary, BOOL bUseBarrier = true, bool bImmediateMode = false);

    public:
        VkViewport m_viewport;
        VkRect2D   m_scissor;
        VkViewport m_CustomViewport;
        VkRect2D   m_CustomScissor;
        BOOL       m_bUseCustomViewport;

    private:
        VkCommandBufferBeginInfo            m_cmdBufInfo;
        std::vector<VkClearValue>           m_clearValues;
        VkRenderPassBeginInfo               m_renderPassBeginInfo;
        uint32_t                            m_uWidth;
        uint32_t                            m_uHeight;
        uint32_t                            m_uArraySize;
        VkFramebuffer                       m_pFramebuffer;
        std::vector<KGfxBarrier>            m_BeginPassLayoutTrans = {};
        uint16_t                            m_uRenderTargetNum = 0;
#if memlect_RenderFrameBuffer
        uint8_t*                            m_pMemLectBuffer;
#endif
        uint32_t                            m_uid;
        KVulkanRenderPass*                        m_pRenderPass = nullptr;
    };
}

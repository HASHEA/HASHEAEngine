#pragma once
#ifdef _WIN32
#include "KGFX_DX12Header.h"

namespace gfx
{
    class KGFX_RenderFrameBufferDx12 final : public IKGFX_RenderFrameBuffer
    {
    public:
        // interface KGfxRef
        int Release() override;

    public:
        KGFX_RenderFrameBufferDx12(const KGfxFrameBufferDesc& fbDesc);

        ~KGFX_RenderFrameBufferDx12() override;

        KGFX_RenderFrameBufferDx12(const KGFX_RenderFrameBufferDx12&) = delete;
        KGFX_RenderFrameBufferDx12& operator=(const KGFX_RenderFrameBufferDx12&) = delete;
        KGFX_RenderFrameBufferDx12(const KGFX_RenderFrameBufferDx12&&) = delete;
        KGFX_RenderFrameBufferDx12& operator=(const KGFX_RenderFrameBufferDx12&&) = delete;

        uint32_t GetWidth() const override;

        uint32_t GetHeight() const override;

        void SetObjectName(const char* szName) override;

        uint32_t GetRenderTargetCount() const override;

        BOOL EndPass(IKGFX_RenderContext* pRenderCtx) override;

        const KGfxFrameBufferDesc& GetFBDesc() const;
        BOOL BeginPass(IKGFX_RenderContext* pRenderCtx, bool bUseBarrier, bool bImmediateMode) override;

    private:
        ViewportDescription m_ViewPort = {};
        KGfxFrameBufferDesc m_FBDesc = {};
        uint32_t m_uWidth = {};
        uint32_t m_uHeight = {};
        std::string m_FBName = {};
    };
};

#endif

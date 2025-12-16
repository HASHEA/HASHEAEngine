#pragma once
#include "KEnginePub/Public/IKTexture.h"

namespace gfx
{
#define KVulkanMemTexture_Memlect_Detector 0
    class KGFX_MemTexture : public KGfxMemTexture
    {
    private:
        uint32_t              m_uByteSride;
        uint32_t              m_uPixelsCount;
        uint32_t              m_uMemorySize;
        uint32_t              m_uMemoryOffset;
        uint32_t              m_rowPitch;
        uint32_t              m_pixelByteSride;
        enumTextureFormat     m_format;
        std::atomic<int32_t>  m_nRef;

        IKGFX_TextureResource* m_pTextureResource = nullptr;
        IKGFX_TextureView*     m_pSRV = nullptr;
        IKGFX_TextureView*     m_pUAV = nullptr;

    public:
        KGFX_MemTexture();
        virtual ~KGFX_MemTexture();
        BOOL                   Create(uint32_t uWidth, uint32_t uHeight, enumTextureFormat targetFormat, const void* pBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) override;
        BOOL                   Create(uint32_t uWidth, uint32_t uHeight, uint32_t uDepth, enumTextureFormat targetFormat, const void* pBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) override;
        BOOL                   Create(uint32_t uWidth, uint32_t uHeight, enumTextureFormat targetFormat, KRenderTarget* pSrcRT) override;
        BOOL                   LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData) override;
        //BOOL                   LoadFromRGBA8DataAndCompress(unsigned int uWidth, unsigned int uHeight, const void* pData) override;
        BOOL                   ReadPixels(void* pBytes, uint32_t ubytes) override;
        BOOL                   Destroy() override;
        BOOL                   Update(void* pBytes, uint32_t ubytes) override;
        BOOL                   UpdateSubImage(uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height, const void* pBytes, uint32_t uBytes, BOOL b2n = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) override;
        BOOL                   UpdateSubImage(uint32_t xoffset, uint32_t yoffset, uint32_t zoffset, uint32_t width, uint32_t height, uint32_t Depth, const void* pBytes, uint32_t uBytes, BOOL b2n = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) override;

        BOOL                   Blit(KRenderTarget* pSrcRT, KBlitRegion blitSrc, KBlitRegion blitDest) override;
        uint64_t               GetNameHash() override;
        int32_t                AddRef() override;
        int32_t                GetRef() override;
        int32_t                Release() override;
        uint32_t               GetWidth() const override;
        uint32_t               GetHeight() const override;
        TextureType            GetTextureType() const override;
        uint64_t               GetResourceSize() override;
        void                   SetObjectName(const char* szName) override;
        uint64_t               GetId() override;
        void                   CopyFromCompressTex(IKGFX_RenderContext* pGraphicsCommand, KRenderTarget* pTex) override;
        void                   CopyFromCompressTex(IKGFX_RenderContext* pGraphicsCommand, KRenderTarget* pTex, IKGFX_Buffer* pBuffer) override;

        virtual void           CopyFromMemTexture(IKGFX_RenderContext* pGraphicsCommand, KGfxMemTexture* pSrcTex) override;

        virtual IKGFX_TextureView* GetSRV() const override
        {
            return m_pSRV;
        }

        virtual IKGFX_TextureView* GetUAV() const override
        {
            return m_pUAV;
        }

        void* GetNativeImageHandle() const override;
        IKGFX_TextureResource* GetTextureResource() const override;
        const KGFX_TextureDesc& GetTexDesc() const override;
        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) override;

#if KVulkanMemTexture_Memlect_Detector
        char* memlect_detector;
#endif
    };
}

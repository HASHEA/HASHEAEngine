#ifndef IKTEXTURE_H
#define IKTEXTURE_H

#include "KEnginePub/Public/IKResource.h"
#include "KEngine/Public/IK3DTypes.h"
#include "KEnginePub/Public/IGFX_Public.h"

namespace gfx
{
    struct KSamplerState;
    class KTextureMask;
    class IKGFX_TextureView;
    class IKGFX_TextureResource;
    class IKGFX_RenderContext;
} // namespace gfx

class IKTexture : public IKResource
{
public:
    IKTexture()
    {
        static std::atomic<uint32_t> uTextureIdSeed(0);
        m_uTextureId = ++uTextureIdSeed;
    }

private:
    BOOL m_bManagedTexture = FALSE;

protected:
    uint32_t m_uTextureUpdateCode = 0;
    uint32_t m_uTextureId = 0;

public:
    virtual uint64_t GetId() = 0;
    BOOL             IsManagedTexture() { return m_bManagedTexture; } // 表示是否注册到 m_mapTexture
    void             SetIsManagedTexture(BOOL bManagedTexture)
    {
        ASSERT(bManagedTexture != m_bManagedTexture);
        m_bManagedTexture = bManagedTexture;
    }
    virtual uint64_t GetCode(BOOL bRender = TRUE) { return GetId() + m_uTextureUpdateCode; }

public:
    virtual void AddOwnerModelFlags(uint32_t uFlags) {};
    virtual BOOL HasOwnerModelFlag(uint32_t uFlag) { return FALSE; };

public:
    //virtual BOOL     LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData) = 0;
    //virtual BOOL     LoadFromRGBA8DataAndCompress(unsigned int uWidth, unsigned int uHeight, const void* pData) { return FALSE; }
    //virtual BOOL     LoadCompressedRGBA8Data(const void* pData, unsigned int uFileSize, const char* cszFileName) { return FALSE; }
    //virtual BOOL     ReLoad()          = 0;
    virtual BOOL     HasAlphaChannel() = 0;
    virtual uint32_t GetFormat() = 0;
    //virtual uint32_t GetChannels()     = 0;

    virtual uint32_t   GetWidth() const = 0;
    virtual uint32_t   GetHeight() const = 0;
    virtual KUniqueStr GetRealResourceName() const = 0;

    //virtual void*    ToImGuiTexutureId() const { return nullptr; }
    virtual uint32_t GetMipMapCount() = 0;

    virtual gfx::IKGFX_TextureResource* GetTextureResource() const = 0;
    virtual gfx::IKGFX_TextureView* GetSRV() const { return nullptr; }	// TODO: = 0 later
    virtual gfx::IKGFX_TextureView* GetUAV() const { return nullptr; }	// TODO: = 0 later
public:
    virtual KEmuLoadAbleType GetLoadAbleType() override
    {
        return LOADABLE_TEXTURE;
    }
    virtual BOOL             IsPostLoadInLogicThread() const override
    {
        return FALSE;
    }

    virtual TextureType GetTextureType() const = 0;
    virtual BOOL        IsRenderTarget() const
    {
        return false;
    }
};

namespace gfx
{
    class KGfxTexture : public IKTexture
    {
    public:
        KGfxTexture()
        {
            m_delayReleaseCounter = 0;
        }
        virtual ~KGfxTexture()
        {
        }
        virtual const char* GetName() { return ""; }
        virtual uint64_t    GetNameHash() = 0;

        /// 返回图形API最底层的数据  比如ID3D12Resource vkimage // TODO: 后续可以删除这个接口，从IKGFX_TextureResource中获取
        virtual void* GetNativeImageHandle() const = 0;

        virtual const KGFX_TextureDesc& GetTexDesc() const = 0;

        virtual KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) = 0;

        uint32_t GetMipMapCount() override { return 1; }

    public:
        KRESOURCETYPE GetResourceType() override
        {
            return TEXTURE_VK_TYPE;
        }

        BOOL Load() override
        {
            return true;
        }
        BOOL PostLoad() override
        {
            SetPostLoaded();
            return true;
        }
        KUniqueStr GetResourceName() override
        {
            static KUniqueStr ustrEmpty;
            return ustrEmpty;
        }
        KUniqueStr GetRealResourceName() const override
        {
            static KUniqueStr ustrEmpty;
            return ustrEmpty;
        }

        BOOL HasAlphaChannel() override
        {
            ASSERT(0);
            return false;
        }

        uint32_t GetFormat() override
        {
            ASSERT(0);
            return 0;
        }

    public:
        virtual BOOL    IsEnableMipmapStreaming() { return FALSE; }
        virtual uint8_t GetWantedMipmap() { return FALSE; };
        virtual BOOL    IMST_UpdateWantedMipmap(uint8_t uWantedMipLevel) { return FALSE; };
        virtual BOOL    IMST_IsNeedStreaming(uint8_t uWantedMipLevel) { return FALSE; };
        virtual BOOL    ReLoad() { return FALSE; }
        float           GetGCTime() override { return m_fGCTime; }
        void            AddGCTime(float fGCTime) override { m_fGCTime += fGCTime; }
        void            ClearGCTime() override { m_fGCTime = 0; }
        virtual BOOL    LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData) { return FALSE; }
        TextureType     GetTextureType() const override { return m_eTextureType; }

        uint32_t m_delayReleaseCounter;
        uint32_t m_uReleaseSwapChainLoopCount[CONTEXT_COUNT] = { 0 };

    protected:
        float       m_fGCTime = 0.0f;
        TextureType m_eTextureType = TextureType::Texture2D;
        gfx::IKGFX_TextureView* m_pBindlessSRV = nullptr;
        gfx::IKGFX_TextureView* m_pBindlessUAV = nullptr;
    };

    class KGfxFileTexture : public KGfxTexture
    {
    public:
        virtual BOOL LoadFromFile(const char* szFileName, unsigned dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) = 0;
        virtual BOOL LoadFromTexsetPack(const KUniqueStr& ustrTexsetPackName, const KUniqueStr& ustrSubTexFileName, unsigned dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) = 0;
        virtual void SetRequestTextureType(TextureType eRequestTextureType) = 0;
    };


    class KGfxMemTexture : public KGfxTexture
    {
    protected:
        std::string m_szName;
        uint32_t    m_uWidth = 0;
        uint32_t    m_uHeight = 0;
        uint32_t    m_uDepth = 0;

    public:
        KGfxMemTexture() {}
        virtual ~KGfxMemTexture() {}
        virtual void SetName(const char* pName) { m_szName = pName; }

        virtual BOOL    Create(uint32_t uWidth, uint32_t uHeight, enumTextureFormat targetFormat, const void* pBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) = 0;
        virtual BOOL    Create(uint32_t uWidth, uint32_t uHeight, uint32_t uDepth, enumTextureFormat targetFormat, const void* pBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) = 0;
        virtual BOOL    Create(uint32_t uWidth, uint32_t uHeight, enumTextureFormat targetFormat, KRenderTarget* pSrcRT) = 0;
        virtual BOOL    ReadPixels(void* pBytes, uint32_t ubytes) = 0;
        virtual BOOL    Destroy() = 0;
        virtual BOOL    Update(void* pBytes, uint32_t ubytes) = 0;
        virtual BOOL    UpdateSubImage(uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height, const void* pBytes, uint32_t uBytes, BOOL b2n = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) = 0;
        virtual BOOL    UpdateSubImage(uint32_t xoffset, uint32_t yoffset, uint32_t zoffset, uint32_t width, uint32_t height, uint32_t Depth, const void* pBytes, uint32_t uBytes, BOOL b2n = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) = 0;
        virtual BOOL    Blit(KRenderTarget* pSrcRT, KBlitRegion blitSrc, KBlitRegion blitDest) = 0;
        virtual void    SetObjectName(const char* szName) = 0;

        virtual void CopyFromCompressTex(IKGFX_RenderContext* pGraphicsCommand, KRenderTarget* pTex) = 0;
        virtual void CopyFromCompressTex(IKGFX_RenderContext* pGraphicsCommand, KRenderTarget* pTex, IKGFX_Buffer* pBuffer) = 0;
        virtual void CopyFromMemTexture(IKGFX_RenderContext* pGraphicsCommand, KGfxMemTexture* pSrcTex) = 0;

        uint32_t m_delayReleaseCounter = 0;
    };
}

struct IKTexturePool
{
    // memory texture
    virtual BOOL CreateMemTexture(gfx::KGfxMemTexture** ppRetMemoryTex, const uint32_t cuWidth, const uint32_t cuHeight, gfx::enumTextureFormat eTargetFormat, const void* cpInitBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) = 0;
    virtual BOOL CreateMemTexture(gfx::KGfxMemTexture** ppRetMemoryTex, const uint32_t cuWidth, const uint32_t cuHeight, const uint32_t cuDepth, gfx::enumTextureFormat eTargetFormat, const void* cpInitBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) = 0;
    virtual BOOL CreateMemTexture(gfx::KGfxMemTexture** ppRetMemoryTex, uint32_t uWidth, uint32_t uHeight, gfx::enumTextureFormat targetFormat, gfx::KRenderTarget* pSrcRT) = 0;

    // file texture
    virtual IKTexture* RequestTexture(
        const KUniqueStr& ustrPackName, const KUniqueStr& ustrSubTexFileName,
        TextureType eTextureType = TextureType::Texture2D, BOOL bThreadLoad = FALSE,
        uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false, BOOL bForceTextureArray = false
    ) = 0;
    virtual IKTexture* RequestTexture(const char* pcszResPath, TextureType eTextureType = TextureType::Texture2D, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false, BOOL bForceTextureArray = false) = 0;
    virtual IKTexture* RequestRawTexture(const char* pcszResName, uint32_t dwOption = 0) = 0;
    virtual IKTexture* RequestEmptyTexture() = 0;
    virtual IKTexture* RequestEmptyFileTexture() = 0;
    virtual IKTexture* RequestMergedTexture2DArray(const char* pcszPathNames[], uint32_t uPathNameCount, TextureType eTextureType = TextureType::MergedTexture2DArray, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) = 0;

    virtual IKTexture* GetTextureByResPath(const char* pcszResPath) = 0;
    virtual IKTexture* GetEvnMap() = 0;
    virtual void       FrameMove(float fDeltaTime) = 0;
    virtual gfx::KGfxFileTexture* GetErrorTexture() = 0;
    virtual gfx::KGfxFileTexture* GetErrorTextureArray() = 0;
    virtual gfx::KGfxFileTexture* GetErrorTextureCube() = 0;
    virtual gfx::KGfxFileTexture* GetBlankTexture() = 0;
    virtual gfx::KGfxFileTexture* GetBlackTexture() = 0;
    virtual gfx::KGfxFileTexture* GetDefaultNormalTexture() = 0;
    virtual gfx::KRenderTarget* GetDefaultShadowTex() = 0;

    virtual gfx::KTextureMask* CreateTextureMask() = 0;

    virtual void SetToHighLoadPriority(BOOL bHigh) = 0;

};

namespace NSEngine
{
    extern "C" BOOL           CreateTexturePool();
    extern "C" BOOL           DestroyTexturePool();
    extern "C" IKTexturePool* GetTexturePool();
} // namespace NSEngine

#endif

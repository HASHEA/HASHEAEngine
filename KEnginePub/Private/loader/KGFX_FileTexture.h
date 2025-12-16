#pragma once
#include "KEnginePub/Public/IKTexture.h"
#include "gli/gli.hpp"

namespace gfx
{
    class KGFX_FileTexture : public gfx::KGfxFileTexture
    {
        struct tagTextureData
        {

            gli::texture*                    pGLITexture        = nullptr;
            gli::texture*                    pGLICommonTex      = nullptr;
            IKGFX_TextureResource*           pTextureResource   = nullptr;
            IKGFX_TextureView*               pTextureView       = nullptr;
            enumTextureFormat                Format             = enumTextureFormat::TEX_FORMAT_NONE;
            TextureDimensionType             TextureType        = TextureDimensionType::Texture2D;
            uint32_t                         uMemoryByteSize    = 0;
            uint16_t                         uWidth             = 0;
            uint16_t                         uHeight            = 0;
            uint16_t                         uDepth             = 0;
            uint16_t                         uBaseMipLevel      = 0;
            uint16_t                         uMipLevels         = 0;
            uint16_t                         uArraySize         = 0;
            bool                             bHasAlpha          = false;
            bool                             bSRGB              = false;
            std::vector<KGfxSubResourceData> vecSubResourceData;

            KUniqueStr                      m_ustrTextureName;

            void Destroy();
            tagTextureData& operator=(tagTextureData&& other) noexcept;
        };

    public:
        KGFX_FileTexture();
        virtual ~KGFX_FileTexture();

        BOOL LoadFromFile(const char* szFileName, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) override;
        virtual BOOL LoadFromTexsetPack(const KUniqueStr& ustrTexsetPackName, const KUniqueStr& ustrSubTexFileName, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) override;
        BOOL LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData) override;
        BOOL LoadFromRGBA8DataAndCompress(unsigned int uWidth, unsigned int uHeight, const void* pData);
        BOOL LoadCompressedRGBA8Data(const void* pData, unsigned int uFileSize, const char* cszFileName);

        KUniqueStr GetResourceName() override;
        uint64_t GetResourceSize() override;
        uint64_t GetNameHash() override;
        int AddRef() override;
        int Release() override;
        int GetRef() override { return m_nRefCount; }

        BOOL Load() override;
        BOOL PostLoad() override;
        KEmuLoadAbleType GetLoadAbleType() override;
        BOOL HasAlphaChannel() override;
        uint32_t GetFormat() override;
        uint32_t GetWidth() const override;
        uint32_t GetHeight() const override;
        KUniqueStr GetRealResourceName() const override;
        uint64_t GetId() override;
        uint32_t GetMipMapCount() override;
        
    public:
        BOOL m_bIsMetaFileTexture = FALSE;

    public:
        TextureType GetTextureType() const override;

        //////////////////////////////////////////////////////////////////////////
        // IKMipmapStreamingTexture

    public:
        virtual BOOL    IsEnableMipmapStreaming() override { return m_bHasMips; }
        virtual uint8_t GetWantedMipmap() override { return m_uWantedMips; }
        virtual BOOL    IMST_UpdateWantedMipmap(uint8_t uWantedMipmap) override;
        virtual BOOL    IMST_IsNeedStreaming(uint8_t uWantedMipmap) override;
        // IKMipmapStreamingTexture
        //////////////////////////////////////////////////////////////////////////

    public:
        virtual void AddOwnerModelFlags(uint32_t uFlags) override { m_uOwnerModelFlags |= uFlags; };
        virtual BOOL HasOwnerModelFlag(uint32_t uFlag) override { return (m_uOwnerModelFlags & uFlag) != 0; }

    public:
        void SetRequestTextureType(TextureType eRequestTextureType) override
        {
            m_eRequestTextureType = eRequestTextureType;
        }
        void SetResourceSize();
        void SetForceTextureArray(BOOL bForceTextureArray);
        BOOL IsForceTextureArray();


    private:
        BOOL _LoadInternal(tagTextureData& sTextureData);
        void _LoadInternalUpSlice(tagTextureData& sTextureData);
        BOOL _ParseGLITextureAfterLoad(tagTextureData& sTextureData);
        BOOL _ParseGLITextureInPostLoad();

        void _DoLoadStreamingGLITextureAsync(uint64_t uLoadStreamingGLITextureTaskLaunchTimeInMs);
        void _OnLoadStreamingGLITextureDone();

        BOOL _IsNeedLowMipLevelMode();
        void _AdjustRealWantedMipLevel(int nWantedMipLevel, int nMaxFileMipLevel, int& nRealWantMipLevel, int& nStartUseFileMipLevel, int& nUseFileMipCount);

    public:
        void* GetNativeImageHandle() const override;
        IKGFX_TextureResource* GetTextureResource() const override;
        IKGFX_TextureView* GetSRV() const override;
        const KGFX_TextureDesc& GetTexDesc() const override;
        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) override;

    private:
        IKGFX_TextureResource* GetImage() const;
        IKGFX_TextureView* GetImageView() const;

    private:
        std::atomic<int>     m_nRefCount;
        BOOL                 m_bHasMips = FALSE;
        std::atomic<uint8_t> m_uWantedMips{ 0 };
        std::atomic<int>     m_nCurMipLevelCount{ 0 };
        std::atomic<int>     m_nMaxFileMipsLevel{ 0 };

    protected:
        KUniqueStr m_ustrPackName;
        KUniqueStr m_ustrTexSetSubFileName;

    public:
        KUniqueStr m_ustrResourceName;
        KUniqueStr m_ustrRealResourceName;

        uint32_t   m_uLoadOption = 0;
        uint32_t   m_uOwnerModelFlags = 0;

    public:
        tagTextureData    m_sTextureData;
        tagTextureData    m_sStreamingTextureData;
        std::atomic<bool> m_bStreamingLoad;

        KAutoRefPtr<NSKBase::KAsyncTask>     m_pStreamingTask;
        // 可能正在执行异步任务的时候，又触发了新的任务，恶劣情况下前后两个任务可能并发
        std::atomic<uint64_t>                m_uLoadStreamingGLITextureTaskLaunchTimeInMs;

    private:
        BOOL        m_bDDS = FALSE;
        BOOL        m_bTGA = FALSE;
        BOOL        m_bThreadLoad = FALSE;
        BOOL        m_bLowTextureQuality = FALSE;
        TextureType m_eRequestTextureType = TextureType::Count;
        TextureType m_eTextureType = TextureType::Count;
        uint64_t    m_uNameHash = 0;
        uint64_t    m_uMemSize = 0;
        std::mutex  m_lock;
        BOOL        m_bForceTextureArray = false;
    };
}

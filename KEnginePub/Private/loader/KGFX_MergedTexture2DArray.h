#pragma once
#include "KEnginePub/Public/IKTexture.h"
#include "../IGFX_Private.h"

namespace gli
{
    class texture2d;
}

namespace gfx
{
    class KGFX_MergedTexture2DArray : public KGfxFileTexture
    {
    public:
        KGFX_MergedTexture2DArray();
        ~KGFX_MergedTexture2DArray();

        virtual BOOL    LoadFromTexsetPack(const KUniqueStr& ustrTexsetPackName, const KUniqueStr& ustrSubTexFileName, unsigned dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) override;
        BOOL            LoadFromFile(const char* szFileNames, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) override;
        BOOL            LoadFromFiles(const char* szFileNames[], uint32_t uPathNameCount, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false);
        KUniqueStr      GetResourceName() override;
        void            SetResourceName(const char* pcszResourceName);
        BOOL            Load() override;
        BOOL            PostLoad() override;
        const char*     GetName() override;
        uint64_t        GetNameHash() override;
        void            SetNameHash(uint64_t uNameHash);

        uint64_t GetId() override;


        uint32_t GetWidth() const override;


        uint32_t GetHeight() const override;


        uint64_t GetResourceSize() override;


        int AddRef() override;


        int Release() override;


        int  GetRef() override;
        void SetRequestTextureType(TextureType eRequestTextureType) override
        {
            m_eRequestTextureType = eRequestTextureType;
        }

        void* GetNativeImageHandle() const override;
        IKGFX_TextureResource* GetTextureResource() const override;
        IKGFX_TextureView* GetSRV() const override;
        const KGFX_TextureDesc& GetTexDesc() const override;
        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) override;

    private:
        std::vector<std::string>        m_vecNames;
        uint64_t                        m_uNameHash;
        TextureType                     m_eRequestTextureType;
        KUniqueStr                      m_ustrResourceName;
        uint32_t                        m_uOwnerModelFlags = 0;
        uint32_t                        m_uLoadOption = 0;
        BOOL                            m_bDDS;
        std::vector<gli::texture2d*>    m_vecGLITexture;
        std::atomic<int>                m_nRefCount{ 1 };
        uint32_t                        m_uSliceDataSize = 0;

        enumTextureFormat               m_Format = enumTextureFormat::TEX_FORMAT_NONE;
        TextureDimensionType            m_TextureType = TextureDimensionType::Texture2D;
        uint32_t                        m_uMemoryByteSize = 0;
        uint16_t                        m_uWidth = 0;
        uint16_t                        m_uHeight = 0;
        uint16_t                        m_uDepth = 0;
        uint16_t                        m_uBaseMipLevel = 0;
        uint16_t                        m_uMipLevels = 0;
        uint16_t                        m_uArraySize = 0;
        bool                            m_bHasAlpha = false;
        bool                            m_bSRGB = false;
        IKGFX_TextureResource*          m_pTextureResource = nullptr;
        IKGFX_TextureView*              m_pTextureView = nullptr;
    };
} // namespace gfx

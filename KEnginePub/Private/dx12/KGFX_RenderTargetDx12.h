#pragma once
#ifdef _WIN32
#include "KGFX_DX12Header.h"
#include "../IGFX_Private.h"
#include "KGFX_TextureImplDx12.h"

namespace gfx
{
    class KGFX_RenderTargetDx12 final : public KRenderTarget
    {
    public:
        KGFX_RenderTargetDx12() = default;
        ~KGFX_RenderTargetDx12() override;

        BOOL IsRenderTarget() const override;

        BOOL Create(const KRenderTargetDesc* pDesc, BOOL bTileOptimize = false);
        BOOL Destroy();

        /**
         * 返回KGFX_TextureDx12
         * @return
         */
        IKGFX_TextureResource* GetTextureResource() const override;

        /**
         *  返回ID3D12Resource
         * @return
         */
        void* GetNativeImageHandle() const override;

        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) override;
        const KGFX_TextureDesc& GetTexDesc() const override;

        uint64_t GetNameHash() override;
        int32_t AddRef() override;
        int32_t GetRef() override;
        int32_t Release() override;
        uint32_t GetWidth() const override;
        uint32_t GetHeight() const override;
        bool IsForDepth() override;
        bool IsHasStencil() override;
        uint64_t GetResourceSize() override;
        void SetObjectName(const char* szName) override;
        bool SaveToFile(const char* pcszSaveFilePath) override;
        uint64_t GetId() override;
        uint32_t GetMipMapCount() override;
        const char* GetName() override;

        /**
         * 用于返回特定范围的SRV
         * @param format
         * @param type
         * @param range
         * @param aspect
         * @return
         */
        D3D12Descriptor GetSRVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM) const;
        D3D12Descriptor GetUAVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM) const;
        D3D12Descriptor GetRTVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM) const;
        D3D12Descriptor GetDSVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM) const;

        gfx::IKGFX_TextureView* GetSRV() const override;
        gfx::IKGFX_TextureView* GetUAV() const override;

        IKGFX_TextureView* GetFullRTV() const override;
        IKGFX_TextureView* GetFullDSV() const override;
        IKGFX_TextureView* GetFullSRV() const override;
        IKGFX_TextureView* GetFullUAV() const override;
        IKGFX_TextureView* GetMipSRV(uint32_t MipLevel, uint32_t uArraySlice) const override;
        IKGFX_TextureView* GetMipUAV(uint32_t MipLevel, uint32_t uArraySlice) const override;
        IKGFX_TextureView* GetMipRTV(uint32_t MipLevel, uint32_t uArraySlice) const override;
        IKGFX_TextureView* GetMipDSV(uint32_t MipLevel, uint32_t uArraySlice) const override;

    private:
        KGFX_TextureImplDx12* m_Resource = nullptr;
        IKGFX_TextureView* m_pFullSRV = nullptr;
        IKGFX_TextureView* m_pFullUAV = nullptr;
        IKGFX_TextureView* m_pFullDSVOrRTV = nullptr;
        std::vector<IKGFX_TextureView*> m_MipmapSRVs = {};
        std::vector<IKGFX_TextureView*> m_MipmapUAVs = {};
        std::vector<IKGFX_TextureView*> m_MipmapRTVorDSVs = {};
    };
}

#endif

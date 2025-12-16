#pragma once
#include <optional>
#ifdef _WIN32
#include "KGFX_ResourceLayoutTracker.h"
#include "DMA_2.0.0/D3D12MemAlloc.h"
#include "KGFX_DX12Header.h"
#include "../IGFX_Private.h"
#include "KGFX_Dx12Healper.h"

namespace gfx
{
    class KGFX_TextureImplDx12 final : public IKGFX_TextureResource, public KGFX_DelayReleaseObject
    {
    public:
        KGFX_TextureImplDx12() = default;
        ~KGFX_TextureImplDx12() override;
        KGFX_TextureImplDx12(const KGFX_TextureImplDx12&) = delete;
        KGFX_TextureImplDx12& operator=(const KGFX_TextureImplDx12&) = delete;
        KGFX_TextureImplDx12(const KGFX_TextureImplDx12&&) = delete;
        KGFX_TextureImplDx12& operator=(const KGFX_TextureImplDx12&&) = delete;
        /**
         * 从renderTarget类创建走此创建函数
         * @param pDesc
         * @return
         */
        bool Create(const KRenderTargetDesc* pDesc);
        /**
         * 从文件读取的贴图类创建走此创建函数
         * @param desc 
         * @param canBUAV 
         * @param dxgiFormat 这个是可选的，如果不传入则使用desc中的格式，由于desc中的格式不包含所有DX的格式，所以需要传入这个参数来兼容DX的格式
         * @return 
         */
        bool Create(const KGFX_TextureDesc& desc, bool canBUAV, std::optional<DXGI_FORMAT> dxgiFormat);

        uintptr_t GetNativeResourceHandle() override;

        void SetDebugName(const char* name) override;

        const char* GetDebugName() override;

        const KGFX_TextureDesc* GetDesc() const override;
        uint32_t GetDeviceMemorySize() const override;

        int32_t AddRef() override;
        int32_t GetRef() override;
        int32_t Release() override;

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        uint32_t GetMipMapCount() const;

        bool IsForDepth() const;
        bool IsHasStencil() const;

        TextureType GetTextureType() const;

        KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) const;
        KGFX_ResourceLayoutTracker<false>& GetLayoutTracker();
        KGfxAccess GetDefaultLayout() const;
        const KD3DX12_RESOURCE_DESC1& GetDXResourceDesc() const;

        /**
         * 如果使用特殊的view，需要创建独立的view，通过view来获取
         * @param format
         * @param type
         * @param range
         * @param aspect
         * @return
         */
        D3D12Descriptor GetSRV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TextureAspectFlagBits::TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM);
        D3D12Descriptor GetUAV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TextureAspectFlagBits::TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM);
        D3D12Descriptor GetRTV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TextureAspectFlagBits::TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM);
        D3D12Descriptor GetDSV(gfx::enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect = TextureAspectFlagBits::TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM);

    private:
        bool Create(const std::optional<KClearValue>& clearValue);

        bool m_CanbDepthTex = false;
        bool m_CanbStencilTex = false;
        bool m_bCubeTex = false;
        bool m_CanbRenderTarget = false;
        bool m_CanbUAV = false;
        bool m_bForSwapChain = false;
        std::string m_szName = {};
        ID3D12Resource* m_Resource = nullptr;
        D3D12MA::Allocation* m_DMAAllocation = nullptr;
        KD3DX12_RESOURCE_DESC1 m_Desc = {};
        KGFX_TextureDesc m_TextureGfxDesc = {};
        KGFX_ResourceLayoutTracker<false> m_ResourceLayout = { D3D12_RESOURCE_STATE_COMMON };
        KGfxAccess m_ResDefaultLayout = {};

        D3D12_HEAP_TYPE m_ResourceHeapType = {};
        struct ViewKeyHasher
        {
            size_t operator()(const KGFX_TextureViewDesc& key) const
            {
                size_t hash = 0;
                HashCombine(hash, key.eViewDimension);
                HashCombine(hash, key.eFormat);
                //HashCombine(hash, key.uAspectFlags);
                HashCombine(hash, key.sSubresourceRange.uBaseArraySlice);
                HashCombine(hash, key.sSubresourceRange.uArrayCount);
                HashCombine(hash, key.sSubresourceRange.uBaseMipLevel);
                HashCombine(hash, key.sSubresourceRange.uMipCount);
                return hash;
            }
        };

        std::unordered_map<KGFX_TextureViewDesc, D3D12Descriptor, ViewKeyHasher> m_SRVS = {};
        std::unordered_map<KGFX_TextureViewDesc, D3D12Descriptor, ViewKeyHasher> m_RTVS = {};
        std::unordered_map<KGFX_TextureViewDesc, D3D12Descriptor, ViewKeyHasher> m_UAVS = {};
        std::unordered_map<KGFX_TextureViewDesc, D3D12Descriptor, ViewKeyHasher> m_DSVS = {};
    };
}

#endif

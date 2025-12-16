#ifdef _WIN32
#include "KGFX_RenderTargetDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/IGFX_RHIHelper.h"


namespace gfx
{
    KGFX_RenderTargetDx12::~KGFX_RenderTargetDx12()
    {
        SAFE_RELEASE(m_pFullDSVOrRTV);
        SAFE_RELEASE(m_pFullSRV);
        SAFE_RELEASE(m_pFullUAV);

        for (auto& view : m_MipmapSRVs)
        {
            SAFE_RELEASE(view);
        }

        for (auto& view : m_MipmapUAVs)
        {
            SAFE_RELEASE(view);
        }

        for (auto& view : m_MipmapRTVorDSVs)
        {
            SAFE_RELEASE(view);
        }

        SAFE_RELEASE(m_Resource);
    }

    BOOL KGFX_RenderTargetDx12::IsRenderTarget() const
    {
        return true;
    }

    BOOL KGFX_RenderTargetDx12::Create(const KRenderTargetDesc* pDesc, BOOL bTileOptimize)
    {
        auto pGraphicDevice = gfx::KGFX_GetGraphicDevice();

        mDesc = *pDesc;
        CHECK_ASSERT(mDesc.m_szRTName[0] != '\0');

        bool bClear = false;
        if (mDesc.uDepth > 1)
            mDesc.eDimension = TextureDimensionType::Texture3D;
        else if (mDesc.uHeight > 1)
            mDesc.eDimension = TextureDimensionType::Texture2D;
        else
            mDesc.eDimension = TextureDimensionType::Texture1D;

        m_Resource = new KGFX_TextureImplDx12();

        BOOL bRes = m_Resource->Create(pDesc);
        KGLOG_PROCESS_ERROR(bRes);

        m_Resource->SetDebugName(mDesc.m_szRTName);

        {
            bool uavRequired = false;
            if ((mDesc.uUsageFlags & TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT) > 0)
            {
                uavRequired = true;
            }


            KGFX_TextureViewDesc srvDesc = RHIHelper::InitTexture2DViewDesc_SRV(mDesc.eFormat);
            KGFX_TextureViewDesc uavDesc = RHIHelper::InitTexture2DViewDesc_UAV(mDesc.eFormat);
            KGFX_TextureViewDesc rtvDesc;

            rtvDesc.eFormat = mDesc.eFormat;
            rtvDesc.eViewType = !m_Resource->IsForDepth() ? gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV : gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV;

            if (mDesc.eDimension == TextureDimensionType::TextureCube)
            {
                srvDesc.eViewDimension = mDesc.uArraySize > 6 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
                uavDesc.eViewDimension = mDesc.uArraySize > 6 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
                rtvDesc.eViewDimension = mDesc.uArraySize > 6 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
            }
            else if (mDesc.eDimension == TextureDimensionType::Texture3D)
            {
                srvDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
                uavDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
                rtvDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D; // TODO: maybe not supported.
            }
            else if (mDesc.eDimension == TextureDimensionType::Texture2D)
            {
                srvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
                uavDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
                rtvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
                bClear = true;
            }
            else
            {
                srvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
                uavDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
                rtvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
            }

            bRes = pGraphicDevice->CreateTextureView(m_Resource, srvDesc, (IKGFX_TextureView**)&m_pFullSRV);
            KGLOG_ASSERT_EXIT(bRes);

            bRes = pGraphicDevice->CreateTextureView(m_Resource, rtvDesc, (IKGFX_TextureView**)&m_pFullDSVOrRTV);
            KGLOG_ASSERT_EXIT(bRes);

            if (uavRequired)
            {
                bRes = pGraphicDevice->CreateTextureView(m_Resource, uavDesc, (IKGFX_TextureView**)&m_pFullUAV);
                KGLOG_ASSERT_EXIT(bRes);
            }
            uint32_t depthOrArraySize = std::max(mDesc.uDepth, mDesc.uArraySize);
            uint32_t numRTVs = mDesc.uMipLevels * depthOrArraySize;
            m_MipmapSRVs.resize(numRTVs, nullptr);
            m_MipmapRTVorDSVs.resize(numRTVs, nullptr);

            if (uavRequired)
                m_MipmapUAVs.resize(numRTVs, nullptr);

            if (depthOrArraySize > 1) // image type is texture array
            {
                ASSERT(mDesc.uMipLevels == 1);

                for (uint32_t i = 0; i < depthOrArraySize; ++i)
                {
                    srvDesc.sSubresourceRange.uBaseArraySlice = i;
                    srvDesc.sSubresourceRange.uArrayCount = 1;
                    srvDesc.sSubresourceRange.uBaseMipLevel = 0;
                    srvDesc.sSubresourceRange.uMipCount = 1;

                    uavDesc.sSubresourceRange = srvDesc.sSubresourceRange;
                    rtvDesc.sSubresourceRange = srvDesc.sSubresourceRange;

                    bRes = pGraphicDevice->CreateTextureView(m_Resource, srvDesc, (IKGFX_TextureView**)&(m_MipmapSRVs[i]));
                    KGLOG_ASSERT_EXIT(bRes);

                    bRes = pGraphicDevice->CreateTextureView(m_Resource, rtvDesc, (IKGFX_TextureView**)&(m_MipmapRTVorDSVs[i]));
                    KGLOG_ASSERT_EXIT(bRes);

                    if (uavRequired)
                    {
                        bRes = pGraphicDevice->CreateTextureView(m_Resource, uavDesc, (IKGFX_TextureView**)&(m_MipmapUAVs[i]));
                        KGLOG_ASSERT_EXIT(bRes);
                    }
                }
            }
            else // if (mDesc.uMipLevels > 1)
            {
                for (uint32_t i = 0; i < mDesc.uMipLevels; ++i)
                {
                    srvDesc.sSubresourceRange.uBaseArraySlice = 0;
                    srvDesc.sSubresourceRange.uArrayCount = 1;
                    srvDesc.sSubresourceRange.uBaseMipLevel = i;
                    srvDesc.sSubresourceRange.uMipCount = 1;

                    uavDesc.sSubresourceRange = srvDesc.sSubresourceRange;
                    rtvDesc.sSubresourceRange = srvDesc.sSubresourceRange;

                    bRes = pGraphicDevice->CreateTextureView(m_Resource, srvDesc, (IKGFX_TextureView**)&(m_MipmapSRVs[i]));
                    KGLOG_ASSERT_EXIT(bRes);

                    bRes = pGraphicDevice->CreateTextureView(m_Resource, rtvDesc, (IKGFX_TextureView**)&(m_MipmapRTVorDSVs[i]));
                    KGLOG_ASSERT_EXIT(bRes);

                    if (uavRequired)
                    {
                        bRes = pGraphicDevice->CreateTextureView(m_Resource, uavDesc, (IKGFX_TextureView**)&(m_MipmapUAVs[i]));
                        KGLOG_ASSERT_EXIT(bRes);
                    }
                }
            }
        }

        if (bClear && pDesc->cpNativeHandle == nullptr)
        {
            GetRenderContext()->CmdClearTextureView(m_pFullDSVOrRTV, {}, KGFX_ClearResourceViewFlags::ClearDepth | KGFX_ClearResourceViewFlags::ClearStencil);
        }

        return true;
    Exit0:
        SAFE_RELEASE(m_Resource);
        return false;
    }

    BOOL KGFX_RenderTargetDx12::Destroy()
    {
        SAFE_DELETE(m_pFullDSVOrRTV);
        SAFE_DELETE(m_pFullSRV);
        SAFE_DELETE(m_pFullUAV);

        for (auto& view : m_MipmapSRVs)
        {
            SAFE_DELETE(view);
        }

        for (auto& view : m_MipmapUAVs)
        {
            SAFE_DELETE(view);
        }

        for (auto& view : m_MipmapRTVorDSVs)
        {
            SAFE_DELETE(view);
        }

        SAFE_RELEASE(m_Resource);

        return true;
    }

    IKGFX_TextureResource* KGFX_RenderTargetDx12::GetTextureResource() const
    {
        return m_Resource;
    }

    void* KGFX_RenderTargetDx12::GetNativeImageHandle() const
    {
        return (void*)m_Resource->GetNativeResourceHandle();
    }

    KGfxSubresourceRange KGFX_RenderTargetDx12::ResolveSubresourceRange(const KGfxSubresourceRange& range)
    {
        return m_Resource->ResolveSubresourceRange(range);
    }

    const KGFX_TextureDesc& KGFX_RenderTargetDx12::GetTexDesc() const
    {
        return static_cast<const KGFX_TextureDesc&>(mDesc);
    }

    uint64_t KGFX_RenderTargetDx12::GetNameHash()
    {
        return reinterpret_cast<uint64_t>(this);
    }

    int32_t KGFX_RenderTargetDx12::AddRef()
    {
        int32_t nRef = ++m_nRef;
        return nRef;
    }

    int32_t KGFX_RenderTargetDx12::GetRef()
    {
        return m_nRef;
    }

    int32_t KGFX_RenderTargetDx12::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            delete this;
        }
        return nRef;
    }

    uint32_t KGFX_RenderTargetDx12::GetWidth() const
    {
        return mDesc.uWidth;
    }

    uint32_t KGFX_RenderTargetDx12::GetHeight() const
    {
        return mDesc.uHeight;
    }

    bool KGFX_RenderTargetDx12::IsForDepth()
    {
        return m_Resource->IsForDepth();
    }

    bool KGFX_RenderTargetDx12::IsHasStencil()
    {
        return m_Resource->IsHasStencil();
    }

    uint64_t KGFX_RenderTargetDx12::GetResourceSize()
    {
        return m_Resource->GetDeviceMemorySize();
    }

    void KGFX_RenderTargetDx12::SetObjectName(const char* szName)
    {
        m_Resource->SetDebugName(szName);
    }

    bool KGFX_RenderTargetDx12::SaveToFile(const char* pcszSaveFilePath)
    {
        throw std::logic_error("接口不应该包含此函数");
    }

    uint64_t KGFX_RenderTargetDx12::GetId()
    {
        return m_uTextureId;
    }

    uint32_t KGFX_RenderTargetDx12::GetMipMapCount()
    {
        return m_Resource->GetMipMapCount();
    }

    const char* KGFX_RenderTargetDx12::GetName()
    {
        return m_Resource->GetDebugName();
    }

    D3D12Descriptor KGFX_RenderTargetDx12::GetSRVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect) const
    {
        return m_Resource->GetSRV(format, type, range, aspect);
    }

    D3D12Descriptor KGFX_RenderTargetDx12::GetUAVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect) const
    {
        return m_Resource->GetUAV(format, type, range, aspect);
    }

    D3D12Descriptor KGFX_RenderTargetDx12::GetRTVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect) const
    {
        return m_Resource->GetRTV(format, type, range, aspect);
    }

    D3D12Descriptor KGFX_RenderTargetDx12::GetDSVImpl(enumTextureFormat format, ResourceViewDimension type, const KGfxSubresourceRange& range, TextureAspectFlagBits aspect) const
    {
        return m_Resource->GetDSV(format, type, range, aspect);
    }

    gfx::IKGFX_TextureView* KGFX_RenderTargetDx12::GetSRV() const
    {
        return m_pFullSRV;
    }

    gfx::IKGFX_TextureView* KGFX_RenderTargetDx12::GetUAV() const
    {
        return m_pFullUAV;
    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetFullRTV() const
    {
        return m_pFullDSVOrRTV;
    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetFullDSV() const
    {
        return m_pFullDSVOrRTV;
    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetFullSRV() const
    {
        return m_pFullSRV;
    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetFullUAV() const
    {
        return m_pFullUAV;
    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetMipSRV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_Resource)
        {
            auto texDesc = m_Resource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapSRVs.size());

            return m_MipmapSRVs[Index];
        }
        else
        {
            return nullptr;
        }
    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetMipUAV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_Resource)
        {
            auto texDesc = m_Resource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapUAVs.size());

            return m_MipmapUAVs[Index];
        }
        else
        {
            return nullptr;
        }

    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetMipRTV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_Resource && !m_Resource->IsForDepth())
        {
            auto texDesc = m_Resource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapRTVorDSVs.size());

            return m_MipmapRTVorDSVs[Index];
        }
        else
        {
            return nullptr;
        }
    }

    IKGFX_TextureView* KGFX_RenderTargetDx12::GetMipDSV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_Resource && m_Resource->IsForDepth())
        {
            auto texDesc = m_Resource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapRTVorDSVs.size());

            return m_MipmapRTVorDSVs[Index];
        }
        else
        {
            return nullptr;
        }
    }
} // namespace gfx

#endif

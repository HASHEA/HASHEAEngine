#include "KGFX_MemTexture.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KEnginePub/Public/KProfileTools.h"
#include "KEngine/Public/KEngineCore.h"

#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    KGFX_MemTexture::KGFX_MemTexture()
    {
        m_uByteSride = 0;
        m_uPixelsCount = 0;
        m_uMemorySize = 0;
        m_rowPitch = 0;
        m_pixelByteSride = 0;
        m_format = TEX_FORMAT_NONE;
        m_uMemoryOffset = 0;
        m_nRef = 1;
#if KVulkanMemTexture_Memlect_Detector
        static uint32_t memlect_detector_counter = 0;
        memlect_detector_counter++;
        memlect_detector = new char[memlect_detector_counter];
        if (memlect_detector_counter >= 26 && memlect_detector_counter <= 32)
        {
            int x = 0;
        }
#endif
    }
    KGFX_MemTexture::~KGFX_MemTexture()
    {
#if KVulkanMemTexture_Memlect_Detector
        SAFE_DELETE_ARRAY(memlect_detector);
#endif
        Destroy();
    }

    BOOL KGFX_MemTexture::Destroy()
    {
        PROF_CPU();

        if (m_pTextureResource)
        {
            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            pPerfMonitor->m_sVkImage.UsageImageCountDec(m_pTextureResource->GetDesc()->uUsageFlags);
        }

        SAFE_RELEASE(m_pSRV);
        SAFE_RELEASE(m_pUAV);
        SAFE_RELEASE(m_pTextureResource);

        m_uWidth = 0;
        m_uHeight = 0;
        m_uMemorySize = 0;

        return TRUE;
    }

    void KGFX_MemTexture::SetObjectName(const char* szName)
    {
        if (m_pTextureResource)
        {
            m_pTextureResource->SetDebugName(szName);
        }
    }

    void* KGFX_MemTexture::GetNativeImageHandle() const
    {
        return m_pTextureResource ? (void*)m_pTextureResource->GetNativeResourceHandle() : nullptr;
    }

    IKGFX_TextureResource* KGFX_MemTexture::GetTextureResource() const
    {
        return m_pTextureResource;
    }

    uint64_t KGFX_MemTexture::GetId()
    {
        if (m_pUAV)
        {
            return (uint64_t)m_pUAV->GetNativeHandle() + m_uTextureId;
        }
        else
        {
            return (uint64_t)m_pSRV->GetNativeHandle() + m_uTextureId;
        }
    }

    BOOL KGFX_MemTexture::Blit(KRenderTarget* pSrcRT, KBlitRegion blitSrc, KBlitRegion blitDest)
    {
        throw std::logic_error("The method or operation is not implemented.");
        return false;
    }

    uint64_t KGFX_MemTexture::GetNameHash()
    {
        return (uint64_t)this;
    }


    int32_t KGFX_MemTexture::AddRef()
    {
        m_nRef++;
        return m_nRef;
    }
    int32_t KGFX_MemTexture::GetRef()
    {
        return m_nRef;
    }

    int32_t KGFX_MemTexture::Release()
    {
        m_nRef--;
        if (m_nRef == 0)
        {
            OnRelease();
            delete this;
        }
        return m_nRef;
    }

    uint32_t KGFX_MemTexture::GetWidth() const
    {
        return m_uWidth;
    }

    uint32_t KGFX_MemTexture::GetHeight() const
    {
        return m_uHeight;
    }

    TextureType KGFX_MemTexture::GetTextureType() const
    {
        return TextureType::Texture2D;
    }

    uint64_t KGFX_MemTexture::GetResourceSize()
    {
        // UNDONE KGFX_MemTexture 【wait check】Byte
        return m_uMemorySize;
    }

    void KGFX_MemTexture::CopyFromCompressTex(IKGFX_RenderContext* pGraphicsCommand, KRenderTarget* pCompressRT)
    {
        PROF_CPU_DETAIL();
        pGraphicsCommand->CmdCopyTexture(pCompressRT->GetTextureResource(), m_pTextureResource);
    }

    void KGFX_MemTexture::CopyFromCompressTex(IKGFX_RenderContext* pGraphicsCommand, KRenderTarget* pCompressRT, IKGFX_Buffer* pBuffer)
    {
        
        pGraphicsCommand->CmdCopyTextureToBuffer(pCompressRT->GetTextureResource(), pBuffer, nullptr, 0);
        pGraphicsCommand->CmdCopyBufferToTexture(pBuffer, m_pTextureResource, nullptr, 0);
    }

    void KGFX_MemTexture::CopyFromMemTexture(IKGFX_RenderContext* pGraphicsCommand, KGfxMemTexture* pSrcMemTex)
    {
        PROF_CPU_DETAIL();
        pGraphicsCommand->CmdCopyTexture(pSrcMemTex->GetTextureResource(), m_pTextureResource);
    }

    const KGFX_TextureDesc& KGFX_MemTexture::GetTexDesc() const
    {
        if (m_pTextureResource)
        {
            return *m_pTextureResource->GetDesc();
        }
        else
        {
            return KGFX_TextureDesc::g_EmptryValue;
        }
    }

    KGfxSubresourceRange KGFX_MemTexture::ResolveSubresourceRange(const KGfxSubresourceRange& range)
    {
        KGfxSubresourceRange resolved = range;
        auto             texDesc = m_pTextureResource->GetDesc();

        resolved.uBaseMipLevel = std::min<uint32_t>(resolved.uBaseMipLevel, texDesc->uMipLevels - 1);
        resolved.uMipCount = std::min<uint32_t>(resolved.uMipCount, texDesc->uMipLevels - resolved.uBaseMipLevel);

        uint32_t arrayLayerCount = texDesc->uArraySize;
        resolved.uBaseArraySlice = std::min<uint32_t>(resolved.uBaseArraySlice, arrayLayerCount - 1);
        resolved.uArrayCount = std::min<uint32_t>(resolved.uArrayCount, arrayLayerCount - resolved.uBaseArraySlice);

        return resolved;
    }

    static void GetTextureFormatFromTargetFormat(enumTextureFormat srcfmt, BOOL& bColorAttach, BOOL& bDepth, BOOL& bStencil, uint32_t& bytesStride, BOOL &bCompress)
    {
        bColorAttach = false;
        bDepth = false;
        bStencil = false;
        bytesStride = 4;
        bCompress = false;
        switch (srcfmt)
        {
        case TEX_FORMAT_R8G8B8A8_UNORM:
        {
            bytesStride = 4;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_R8G8B8A8_SNORM:
        {
            bytesStride = 4;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_R8G8B8A8_SRGB:
        {
            bytesStride = 4;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_B8G8R8_UNORM:
        {
            bytesStride = 3;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_B5G6R5_UNORM_PACK16:
        {
            bytesStride = 2;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_B8G8R8A8_UNORM:
        {
            bytesStride = 4;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_B8G8R8A8_SRGB:
        {
            bytesStride = 4;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_R8_UNORM:
        {
            bytesStride = 1;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_R8G8_UNORM:
        {
            bytesStride = 2;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_R16G16_UINT:
        {
            bytesStride = 4;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_R16G16B16A16_UNORM:
        {
            bytesStride = 8;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
        }
        break;
        case TEX_FORMAT_R16G16B16A16_SFLOAT:
        {
            bytesStride = 8;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_R32G32B32A32_SFLOAT:
        {
            bytesStride = 16;
            bColorAttach = TRUE;
        }
        break;
        case TEX_FORMAT_BC7_SRGB_UNORM:
        {
            bytesStride = 16;
            bColorAttach = TRUE;
            bDepth = false;
            bStencil = false;
            bCompress = true;
            break;
        }
        case TEX_FORMAT_R32G32_UINT:
            bytesStride = 8;
            bColorAttach = TRUE;
            break;
        case TEX_FORMAT_R32G32B32A32_UINT:
            bytesStride = 16;
            bColorAttach = TRUE;
            break;
        case TEX_FORMAT_A2R10G10B10_UNORM_PACK32:
        {
            bytesStride = 8;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_B10G11R11_UFLOAT_PACK32:
        {
            bytesStride = 8;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_R16G16_SFLOAT:
        {
            bytesStride = 8;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_R16_SFLOAT:
        {
            bytesStride = 2;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_R16_UINT:
        {
            bytesStride = 2;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_R32_SINT:
        {
            bytesStride = 4;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_R32_UINT:
        {
            bytesStride = 4;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_R32_FLOAT:
        {
            bytesStride = 4;
            bColorAttach = true;
        }
        break;
        case TEX_FORMAT_D24_UNORM_S8_UINT:
        {
            bytesStride = 4;
            bDepth = true;
            bStencil = true;
        }
        break;
        case TEX_FORMAT_D16_UNORM:
        {
            bytesStride = 2;
            bDepth = true;
        }
        break;
        case TEX_FORMAT_D32_SFLOAT:
        {
            bytesStride = 4;
            bDepth = true;
        }
        break;
        case TEX_FORMAT_D32_SFLOAT_S8_UINT:
        {
            bytesStride = 4;
            bDepth = true;
            bStencil = true;
        }
        break;
        case TEX_FORMAT_R16G16_UNORM:
            bytesStride = 4;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
            break;
        case TEX_FORMAT_R16_UNORM:
            bytesStride = 2;
            bColorAttach = true;
            bDepth = false;
            bStencil = false;
            break;
        case TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
            bytesStride = 8;
            bDepth = false;
            bStencil = false;
            bCompress = true;
            break;
        case TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
            bytesStride = 16;
            bDepth = false;
            bStencil = false;
            bCompress = true;
            break;
        case TEX_FORMAT_ETC2_RG_UNORM_BLOCK:
            bytesStride = 16;
            bCompress = true;
            break;
        case TEX_FORMAT_BC1_RGB_UNORM:
            bytesStride = 8;
            bCompress = true;
            break;
        case TEX_FORMAT_BC3_UNORM:
            bytesStride = 16;
            bCompress = true;
            break;
        case TEX_FORMAT_BC5_UNORM:
            bytesStride = 16;
            bCompress = true;
            break;
        case TEX_FORMAT_ASTC_4X4_UNORM_BLOCK:
            bytesStride = 16;
            bCompress = true;
            break;
        case TEX_FORMAT_R8_UINT:
            bytesStride = 1;
            bDepth = false;
            bStencil = false;
            break;
        default:
            ASSERT(0);
            break;
        }
    }

    BOOL KGFX_MemTexture::Create(
        const uint32_t uWidth, const uint32_t uHeight,
        const enumTextureFormat texFormat, const void* pBytes, uint32_t uBytes,
        BOOL bTile, BOOL bSupportSample, BOOL bSupportStorage
    )
    {
        BOOL     bResult = false;
        BOOL     bRetCode = false;
        BOOL     bColor = false;
        BOOL     bDepth = false;
        BOOL     bStencil = false;
        BOOL     bCompress = false;

        auto pRenderCtx = gfx::GetRenderContext();
        CHECK_ASSERT(pRenderCtx);

        auto pGraphicsDevice = KGFX_GetGraphicDevice();
        CHECK_ASSERT(pGraphicsDevice);

        GetTextureFormatFromTargetFormat(texFormat, bColor, bDepth, bStencil, m_pixelByteSride, bCompress);
        m_uByteSride = m_pixelByteSride;
        m_format = texFormat;
        m_uWidth = uWidth;
        m_uHeight = uHeight;
        m_uDepth = 1;

        if (pBytes)
        {
            ASSERT(uBytes);
        }

        BOOL b2n = false;
        if (NSKMath::Is2n(m_uWidth) && NSKMath::Is2n(m_uHeight))
        {
            b2n = true;
        }

        KGFX_TextureDesc texDesc;
        texDesc.eDimension = TextureDimensionType::Texture2D;
        texDesc.uWidth = m_uWidth;
        texDesc.uHeight = m_uHeight;
        texDesc.uDepth  = m_uDepth;
        texDesc.eFormat = m_format;
        texDesc.uMipLevels = 1;
        texDesc.uArraySize = 1;
        texDesc.uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_DST_BIT | TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_SRC_BIT;

        if (bSupportSample)
        {
            texDesc.uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;
        }

        if (bSupportStorage)
        {
            texDesc.uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT;
        }

        KGFX_TextureViewDesc viewDesc;
        viewDesc.eFormat = m_format;

        bRetCode = pGraphicsDevice->CreateTexture(texDesc, "KGFX_MemTexture", & m_pTextureResource);
        KGLOG_ASSERT_EXIT(bRetCode);

        m_uMemorySize = m_pTextureResource->GetDeviceMemorySize();

        if (bSupportSample)
        {
            viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;

            bRetCode = pGraphicsDevice->CreateTextureView(m_pTextureResource, viewDesc, &m_pSRV);
            KGLOG_ASSERT_EXIT(bRetCode);
        }

        if (bSupportStorage)
        {
            viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV;

            bRetCode = pGraphicsDevice->CreateTextureView(m_pTextureResource, viewDesc, &m_pUAV);
            KGLOG_ASSERT_EXIT(bRetCode);
        }

        {
            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            pPerfMonitor->m_sVkImage.UsageImageCountInc(texDesc.uUsageFlags);
            m_uPixelsCount = m_uHeight * m_uWidth;

            if (pBytes)
            {
                UpdateSubImage(0, 0, uWidth, uHeight, pBytes, uBytes, b2n, bSupportSample, bSupportStorage);
            }
        }
        bResult = true;
    Exit0:
        SetLoadState(bResult ? ELoadableState::PostLoaded : ELoadableState::Failed);
        return bResult;
    }
    BOOL KGFX_MemTexture::Create(
        uint32_t uWidth, uint32_t uHeight, uint32_t uDepth,
        enumTextureFormat texFormat,
        const void* pBytes /*= nullptr*/, uint32_t uBytes /*= 0*/,
        BOOL bTile /*= TRUE*/, BOOL bSupportSample /*= TRUE*/, BOOL bSupportStorage /*= FALSE*/
    )
    {
        BOOL     bResult = false;
        BOOL     bRetCode = false;
        BOOL     bColor = false;
        BOOL     bDepth = false;
        BOOL     bStencil = false;
        BOOL     bCompress = false;

        auto pRenderCtx = gfx::GetRenderContext();
        CHECK_ASSERT(pRenderCtx);

        auto pGraphicsDevice = KGFX_GetGraphicDevice();
        CHECK_ASSERT(pGraphicsDevice);

        GetTextureFormatFromTargetFormat(texFormat, bColor, bDepth, bStencil, m_pixelByteSride, bCompress);
        m_uByteSride = m_pixelByteSride;
        m_format = texFormat;
        m_uWidth = uWidth;
        m_uHeight = uHeight;
        m_uDepth = uDepth;

        if (pBytes)
        {
            ASSERT(uBytes);
        }

        BOOL b2n = false;
        if (NSKMath::Is2n(m_uWidth) && NSKMath::Is2n(m_uHeight) && NSKMath::Is2n(m_uDepth))
        {
            b2n = true;
        }

        KGFX_TextureDesc texDesc;
        texDesc.eDimension = TextureDimensionType::Texture3D;
        texDesc.uWidth = m_uWidth;
        texDesc.uHeight = m_uHeight;
        texDesc.uDepth = m_uDepth;
        texDesc.eFormat = m_format;
        texDesc.uMipLevels = 1;
        texDesc.uArraySize = 1;
        texDesc.uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_DST_BIT | TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_SRC_BIT;

        if (bSupportSample)
        {
            texDesc.uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;
        }

        if (bSupportStorage)
        {
            texDesc.uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT;
        }

        KGFX_TextureViewDesc viewDesc;
        viewDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
        viewDesc.eFormat = m_format;

        bRetCode = pGraphicsDevice->CreateTexture(texDesc, "KGFX_MemTexture", &m_pTextureResource);
        KGLOG_ASSERT_EXIT(bRetCode);

        m_uMemorySize = m_pTextureResource->GetDeviceMemorySize();

        if (bSupportSample)
        {
            viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;

            bRetCode = pGraphicsDevice->CreateTextureView(m_pTextureResource, viewDesc, &m_pSRV);
            KGLOG_ASSERT_EXIT(bRetCode);
        }

        if (bSupportStorage)
        {
            viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV;

            bRetCode = pGraphicsDevice->CreateTextureView(m_pTextureResource, viewDesc, &m_pUAV);
            KGLOG_ASSERT_EXIT(bRetCode);
        }

        {
            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            pPerfMonitor->m_sVkImage.UsageImageCountInc(texDesc.uUsageFlags);
            m_uPixelsCount = m_uHeight * m_uWidth * m_uDepth;

            if (pBytes)
            {
                UpdateSubImage(0, 0, 0, uWidth, uHeight, uDepth, pBytes, uBytes, b2n);
            }
        }
        bResult = true;
    Exit0:
        SetLoadState(bResult ? ELoadableState::PostLoaded : ELoadableState::Failed);
        return bResult;
    }

    

    BOOL KGFX_MemTexture::Create(uint32_t uWidth, uint32_t uHeight, enumTextureFormat targetFormat, KRenderTarget* pSrcRT)
    {
        BOOL                       bRetCode = FALSE;
        BOOL                       bColor = false;
        BOOL                       bDepth = false;
        BOOL                       bStencil = false;
        uint32_t                   uMipLevels = pSrcRT->GetMipMapCount();
        BOOL                       bCompress = false;
        auto pRenderCtx = gfx::GetRenderContext();
        CHECK_ASSERT(pRenderCtx);

        auto pGraphicsDevice = KGFX_GetGraphicDevice();
        CHECK_ASSERT(pGraphicsDevice);

        GetTextureFormatFromTargetFormat(targetFormat, bColor, bDepth, bStencil, m_pixelByteSride, bCompress);
        m_uByteSride = m_pixelByteSride;
        m_format = targetFormat;
        m_uWidth = uWidth;
        m_uHeight = uHeight;
        m_uPixelsCount = m_uHeight * m_uWidth;

        KGFX_TextureDesc texDesc;
        texDesc.eDimension = TextureDimensionType::Texture2D;
        texDesc.uWidth = m_uWidth;
        texDesc.uHeight = m_uHeight;
        texDesc.uDepth = m_uDepth;
        texDesc.eFormat = m_format;
        texDesc.uMipLevels = uMipLevels;
        texDesc.uArraySize = 1;
        if(bCompress)
        {
            texDesc.uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_DST_BIT | TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_SRC_BIT | TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;
        }
        else
        {
            texDesc.uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_DST_BIT | TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_SRC_BIT | TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT | TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT;
        }

        KGFX_TextureViewDesc viewDesc;
        viewDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
        viewDesc.eFormat = m_format;

        bRetCode = pGraphicsDevice->CreateTexture(texDesc, "KGFX_MemTexture", &m_pTextureResource);
        KGLOG_ASSERT_EXIT(bRetCode);

        m_uMemorySize = m_pTextureResource->GetDeviceMemorySize();

        viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;

        bRetCode = pGraphicsDevice->CreateTextureView(m_pTextureResource, viewDesc, &m_pSRV);
        KGLOG_ASSERT_EXIT(bRetCode);

        viewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV;

        if (!bCompress)
        {
            bRetCode = pGraphicsDevice->CreateTextureView(m_pTextureResource, viewDesc, &m_pUAV);
            KGLOG_ASSERT_EXIT(bRetCode);
        }
 

        {
            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            pPerfMonitor->m_sVkImage.UsageImageCountInc(texDesc.uUsageFlags);
        }

        bRetCode = TRUE;
    Exit0:
        SetLoadState(bRetCode ? ELoadableState::PostLoaded : ELoadableState::Failed);
        return bRetCode;
    }

    BOOL KGFX_MemTexture::LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData)
    {
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;
        bRetCode = Create(uWidth, uHeight, gfx::enumTextureFormat::TEX_FORMAT_R8G8B8A8_UNORM, pData, uWidth * uHeight * 4);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        SetLoadState(bResult ? ELoadableState::PostLoaded : ELoadableState::Failed);
        return bResult;
    }

    BOOL KGFX_MemTexture::Update(void* pBytes, uint32_t ubytes)
    {
        PROF_CPU();
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;

        BOOL b2n = false;
        if (NSKMath::Is2n(m_uWidth) && NSKMath::Is2n(m_uHeight))
        {
            b2n = true;
        }

        return UpdateSubImage(0, 0, m_uWidth, m_uHeight, pBytes, ubytes, b2n);
    }

    BOOL KGFX_MemTexture::UpdateSubImage(
        uint32_t xoffset, uint32_t yoffset,
        uint32_t width, uint32_t height,
        const void* pBytes, uint32_t uBytes,
        BOOL b2n, BOOL bSupportSample, BOOL bSupportStorage)
    {
        PROF_CPU();
        BOOL     bResult = FALSE;
        KGfxCopyRegion copyRegion;

        ASSERT(uBytes <= m_uMemorySize);

        auto pRenderCtx = gfx::GetRenderContext();
        CHECK_ASSERT(pRenderCtx);

        if (width == 0 && height == 0)
        {
            auto texDesc = m_pTextureResource->GetDesc();
            width = texDesc->uWidth;
            height = texDesc->uHeight;
        }

        KGLOG_ASSERT_EXIT(width > 0 && height > 0);

        copyRegion.left = xoffset;
        copyRegion.top  = yoffset;
        copyRegion.right = xoffset + width;
        copyRegion.bottom = yoffset + height;
        copyRegion.front  = 0;
        copyRegion.back   = 1;

        pRenderCtx->CmdUpdateSubResource(m_pTextureResource, 0, 0, &copyRegion, pBytes, 0, 0);

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_MemTexture::UpdateSubImage(uint32_t xoffset, uint32_t yoffset, uint32_t zoffset, uint32_t width, uint32_t height, uint32_t depth, const void* pBytes, uint32_t uBytes, BOOL b2n /*= TRUE*/, BOOL bSupportSample, BOOL bSupportStorage)
    {
        BOOL bResult = FALSE;
        ASSERT(uBytes <= m_uMemorySize);

        auto pRenderCtx = gfx::GetRenderContext();
        CHECK_ASSERT(pRenderCtx);

        KGfxCopyRegion copyRegion;
        copyRegion.left = xoffset;
        copyRegion.top = yoffset;
        copyRegion.right = xoffset + width;
        copyRegion.bottom = yoffset + height;
        copyRegion.front = zoffset;
        copyRegion.back = zoffset + depth;

        pRenderCtx->CmdUpdateSubResource(m_pTextureResource, 0, 0, &copyRegion, pBytes, 0, 0);

        bResult = true;
        return bResult;
    }

    BOOL KGFX_MemTexture::ReadPixels(void* pBytes, uint32_t ubytes)
    {
    //    BOOL bResult = FALSE;
    //    BOOL bRetCode = FALSE;
    //    BOOL hRetCode = VK_INCOMPLETE;
    //
    //    VkDevice            pDevice = GetVkDevice();
    //    vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
    //
    //    BOOL b2n = false;
    //    if (NSKMath::Is2n(m_uWidth) && NSKMath::Is2n(m_uHeight))
    //    {
    //        b2n = true;
    //    }
    //
    //    if (ubytes <= m_uMemorySize)
    //    {
    //        uint8_t* pbytes = (uint8_t*)pBytes;
    //        uint8_t* data = nullptr;
    //        // Map image memory
    //        // #if X3D_VK_USE_VMA
    //        if (DrvOption::bX3D_VK_USE_VMA)
    //        {
    //            bRetCode = pVulkanDevice->VMAMapMemory(m_pVMAllocation, (void**)&data);
    //            KGLOG_ASSERT_EXIT(bRetCode);
    //        }
    //        // #else
    //        else
    //        {
    //            hRetCode = vks::vkMapMemory(pDevice, m_pMemory, m_uMemoryOffset, m_uMemorySize, 0, (void**)&data);
    //        }
    //        // #endif
    //        if (hRetCode)
    //        {
    //            // Copy image data into memory
    //            if (b2n)
    //            {
    //                memcpy(pBytes, data, ubytes);
    //            }
    //            else
    //            {
    //                uint32_t rowbytesCount = m_pixelByteSride * m_uWidth;
    //                for (uint32_t i = 0; i < m_uHeight; ++i)
    //                {
    //                    memcpy(pbytes, data, rowbytesCount);
    //                    data += m_rowPitch;
    //                    pbytes += rowbytesCount;
    //                }
    //            }
    //        }
    //        // #if X3D_VK_USE_VMA
    //        if (DrvOption::bX3D_VK_USE_VMA)
    //        {
    //            pVulkanDevice->VMAUnmapMemory(m_pVMAllocation);
    //        }
    //        // #else
    //        else
    //        {
    //            vks::vkUnmapMemory(pDevice, m_pMemory);
    //        }
    //        // #endif
    //        KGLOG_COM_ASSERT_EXIT(hRetCode);
    //    }
    //
    //    bResult = true;
    //Exit0:
    //    return bResult;
        throw std::logic_error("The method or operation is not implemented.");
        return false;
    }
}

#include "gli/gli.hpp"
#include "KGFX_MergedTexture2DArray.h"
#include "KTexturePool.h"
#include <iostream>
#include <string>
#include <sstream>
#include "KBase/Public/str/KStrHelper.h"
#include "Engine/KGLog.h"
#include "KBase/Public/thread/KThread.h"
#include "KBase/Public/io/KFile.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KEngine/Public/KEngineCore.h"

/////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/KProfileTools.h"

namespace gfx
{
    extern enumTextureFormat Util_GetImageFormatFromGLI(gli::format fmtType, bool& bSRGB, bool& bHasAlpha);

    KGFX_MergedTexture2DArray::KGFX_MergedTexture2DArray()
    {
        m_uNameHash           = 0;
        m_eRequestTextureType = TextureType::Count;
        m_uLoadOption         = 0;
        m_bDDS                = false;
        SetIsProcedural(false);

        m_eTextureType = TextureType::MergedTexture2DArray;

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->nTextureVKCount;
    }

    KGFX_MergedTexture2DArray::~KGFX_MergedTexture2DArray()
    {
        SAFE_RELEASE(m_pTextureView);
        SAFE_RELEASE(m_pTextureResource);

        for (auto it : m_vecGLITexture)
        {
            gli::texture* pGLITex = it;
            SAFE_DELETE(pGLITex);
        }
        m_vecGLITexture.clear();

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->nTextureVKCount;
    }

    BOOL KGFX_MergedTexture2DArray::LoadFromFile(const char* szFileNames, uint32_t dwOption, uint32_t uOwnerModelFlags, BOOL bHighLoadPriority)
    {
        ASSERT(0 && "change to load from LoadFromFiles");
        return false;
    }

    KUniqueStr KGFX_MergedTexture2DArray::GetResourceName()
    {
        return m_ustrResourceName;
    }

    void KGFX_MergedTexture2DArray::SetResourceName(const char* pcszResourceName)
    {
        m_ustrResourceName = g_CachePathString(pcszResourceName, TRUE);
    }

    BOOL KGFX_MergedTexture2DArray::LoadFromFiles(const char* szFileNames[], uint32_t uPathNameCount, uint32_t dwOption, uint32_t uOwnerModelFlags, BOOL bHighLoadPriority)
    {
        BOOL bResult     = FALSE;
        BOOL bRetCode    = FALSE;
        BOOL bThreadLoad = FALSE;

        KGLOG_PROCESS_ERROR(uPathNameCount && szFileNames[0][0]);
        for (uint32_t i = 0; i < uPathNameCount; ++i)
        {
            m_vecNames.push_back(szFileNames[i]);
        }

        SetLoadState(ELoadableState::Loading);

        m_uLoadOption      = (uint32_t)dwOption;
        m_uOwnerModelFlags = uOwnerModelFlags;

        bThreadLoad = ENABLE_RESOURCE_MULTITHREAD_LOAD && (m_uLoadOption & RESOURCE_LOAD_MULTITHREAD);
        if (ENABLE_RESOURCE_MULTITHREAD_LOAD && bThreadLoad)
        {
            LoadAsync(bHighLoadPriority ? NSKBase::EAsyncTaskPriority::High : NSKBase::EAsyncTaskPriority::Normal);
        }
        else
        {
            bRetCode = Load();
            KGLOG_PROCESS_ERROR(bRetCode);

            if (IsMainThread())
            {
                PostLoad();
            }
        }

        bResult = true;
    Exit0:
        return bResult;
    }


    uint64_t KGFX_MergedTexture2DArray::GetNameHash()
    {
        return m_uNameHash;
    }

    void KGFX_MergedTexture2DArray::SetNameHash(uint64_t uNameHash)
    {
        m_uNameHash = uNameHash;
    }

    BOOL KGFX_MergedTexture2DArray::Load()
    {
        PROF_CPU();
        BOOL                       bRet     = false;
        BOOL                       bRetCode = false;
        uint32_t                   width    = 0;
        uint32_t                   height   = 0;
        gli::format                fmt;
        uint32_t                   n      = 0;
        uint32_t                   levels = 0;
        std::vector<gli::texture*> vecTempTexture;
        KGLOG_PROCESS_ERROR(!m_vecNames.empty());

        for (const auto it : m_vecNames)
        {
            const char* pcszRealResName = it.c_str();

            char szConvResName[NSEngine::MAX_PATH_LEN] = "";
            g_StrCpyLen(szConvResName, pcszRealResName, countof(szConvResName));
            char* p = strrchr(szConvResName, '.');
            if (p)
            {
                *p = '\0';
            }

#if defined(_WIN32) || defined(__MACOS__)
            strcat(szConvResName, ".dds");
#else
            strcat(szConvResName, ".ktx");
#endif
            if (KGFExist(szConvResName))
            {
                pcszRealResName = szConvResName;
            }
            else
            {
                KGLogPrintf(KGLOG_ERR, "%s：缺失，创建MergedTexture2DArray失败", pcszRealResName);
                goto Exit0;
            }

            m_bDDS = KSTR_HELPER::StrEndWith(pcszRealResName, ".dds");

#if !defined(_WIN32) && !defined(__MACOS__)
            if (m_bDDS)
            {
                KGLogPrintf(KGLOG_ERR, "[KGFX_MergedTexture2DArray] dds is not support on mobile device, res: %s.", pcszRealResName);
                KG_PROCESS_ERROR(FALSE);
            }
#endif

            gli::texture* pGliTexture      = new gli::texture;
            *pGliTexture                   = gli::load(pcszRealResName);
            gli::texture2d* pGliTexture2d  = new gli::texture2d(*pGliTexture);
            pGliTexture2d->srcChannelcount = pGliTexture->srcChannelcount;
            // SAFE_DELETE(pGliTexture);
            vecTempTexture.push_back(pGliTexture);

            m_vecGLITexture.push_back(pGliTexture2d);
            if (pGliTexture2d->empty())
            {
                KGLogPrintf(KGLOG_ERR, "[X3DEngine] KGFX_MergedTexture2DArray::Load: %s load failed.", pcszRealResName);
                KG_PROCESS_ERROR(false);
            }

            if (n == 0)
            {
                m_uBaseMipLevel = 0;
                m_uWidth        = pGliTexture2d->extent(0).x;
                m_uHeight       = pGliTexture2d->extent(0).y;
                m_uDepth        = 1;
                m_uMipLevels    = (uint16_t)pGliTexture2d->levels();
                m_uArraySize    = (uint16_t)m_vecNames.size();

                levels = (uint32_t)pGliTexture2d->levels();
                fmt    = pGliTexture2d->format();

                gli::format fmtType = pGliTexture2d->format();
                m_Format            = Util_GetImageFormatFromGLI(fmtType, m_bSRGB, m_bHasAlpha);
                if (m_Format == enumTextureFormat::TEX_FORMAT_NONE)
                {
                    KGLogPrintf(KGLOG_ERR, "not support texture gli_format:%d, path%s", (int)fmtType, m_ustrResourceName.Str());
                    KG_PROCESS_ERROR(FALSE);
                }

                if (m_Format == enumTextureFormat::TEX_FORMAT_B8G8R8_UNORM)
                {
                    KGLogPrintf(KGLOG_ERR, "%s：is three channel texture, not support, please correct this resource", m_ustrResourceName.Str());
                    KG_PROCESS_ERROR(FALSE);
                }

                m_uSliceDataSize = 0;
                for (uint32_t lv = 0; lv < levels; ++lv)
                {
                    m_uSliceDataSize += (uint32_t)pGliTexture2d->size(lv);
                }

                m_uMemoryByteSize = m_uSliceDataSize * m_uArraySize;
            }
            else if (
                m_uWidth != pGliTexture2d->extent(0).x ||
                m_uHeight != pGliTexture2d->extent(0).y ||
                m_uMipLevels != pGliTexture2d->levels() ||
                fmt != pGliTexture2d->format()
            )
            {
                KGLogPrintf(KGLOG_ERR, "[X3DEngine] KGFX_MergedTexture2DArray::Load: %s is not same format or size with others.", pcszRealResName);
                KG_PROCESS_ERROR(false);
            }

            ++n;
        }

        bRet = true;
    Exit0:
        for (gli::texture* pTmpGLITex : vecTempTexture)
        {
            SAFE_DELETE(pTmpGLITex);
        }
        vecTempTexture.clear();

        if (!bRet)
        {
            for (auto it : m_vecGLITexture)
            {
                gli::texture* pGLITex = it;
                SAFE_DELETE(pGLITex);
            }
            m_vecGLITexture.clear();
        }
        SetLoadState(bRet ? ELoadableState::Loaded : ELoadableState::Failed);
        return bRet;
    }

    BOOL KGFX_MergedTexture2DArray::PostLoad()
    {
        PROF_CPU();
        BOOL                             bRet        = false;
        BOOL                             bRetCode    = false;
        uint32_t                         uLayerCount = (uint32_t)m_vecNames.size();
        gli::target                      eTexTarget;
        std::vector<KGfxSubResourceData> vecSubResourceData;
        KGFX_TextureDesc                 texDesc;
        KGFX_TextureViewDesc             srvDesc;

        auto pGraphicDevice = gfx::KGFX_GetGraphicDevice();
        CHECK_ASSERT(pGraphicDevice);

        KGLOG_PROCESS_ERROR(uLayerCount);
        KGLOG_PROCESS_ERROR(IsLoaded() || IsLoadFailed());
        KG_PROCESS_SUCCESS(IsPostLoaded());

        ASSERT(!m_vecGLITexture.empty());
        if (m_vecGLITexture.empty())
            return FALSE;

        eTexTarget = m_vecGLITexture[0]->target();
        CHECK_ASSERT(eTexTarget == gli::TARGET_2D || eTexTarget == gli::TARGET_2D_ARRAY);

        texDesc.eDimension  = m_TextureType;
        texDesc.memoryType  = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        texDesc.uWidth      = m_uWidth;
        texDesc.uHeight     = m_uHeight;
        texDesc.uDepth      = m_uDepth;
        texDesc.uArraySize  = m_uArraySize;
        texDesc.uMipLevels  = m_uMipLevels;
        texDesc.eFormat     = m_Format;
        texDesc.bSRGB       = m_bSRGB;
        texDesc.uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;

        srvDesc.eFormat                       = texDesc.eFormat;
        srvDesc.eViewType                     = KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;
        srvDesc.eViewDimension                = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY;
        srvDesc.uAspectFlags                  = TextureAspectFlagBits::TEXTURE_ASPECT_COLOR_BIT;
        srvDesc.sSubresourceRange.uBaseArraySlice = 0;
        srvDesc.sSubresourceRange.uArrayCount = texDesc.uArraySize;
        srvDesc.sSubresourceRange.uBaseMipLevel   = 0;
        srvDesc.sSubresourceRange.uMipCount   = texDesc.uMipLevels;

        bRetCode = pGraphicDevice->CreateTexture(texDesc, m_ustrResourceName.Str(), &m_pTextureResource);
        KGLOG_ASSERT_EXIT(bRetCode);

        bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, srvDesc, &m_pTextureView);
        KGLOG_ASSERT_EXIT(bRetCode);

        for (auto it : m_vecGLITexture)
        {
            gli::texture2d* pTex = it;

            for (std::size_t stLevel = m_uBaseMipLevel; stLevel < m_uBaseMipLevel + m_uMipLevels; ++stLevel)
            {
                uint32_t uLayerWidth  = 0;
                uint32_t uLayerHeight = 0;
                uint32_t uLayerDepth  = 0;

                KGfxSubResourceData sSubResourceData = {};
                sSubResourceData.pMemData            = pTex->data(0, 0, stLevel);

                vecSubResourceData.emplace_back(sSubResourceData);
            }
        }

        {
            auto pRenderCtx = gfx::GetRenderContext();
            CHECK_ASSERT(pRenderCtx);

            pRenderCtx->CmdUpdateAllResource(m_pTextureResource, vecSubResourceData);
        }

    Exit1:
        bRet = true;
    Exit0:
        SetLoadState(bRet ? ELoadableState::PostLoaded : ELoadableState::Failed);
        for (auto it : m_vecGLITexture)
        {
            gli::texture* pGLITex = it;
            SAFE_DELETE(pGLITex);
        }
        m_vecGLITexture.clear();
        return bRet;
    }

    const char* KGFX_MergedTexture2DArray::GetName()
    {
        return m_ustrResourceName.Str();
    }

    BOOL KGFX_MergedTexture2DArray::LoadFromTexsetPack(
        const KUniqueStr& ustrTexsetPackName, const KUniqueStr& ustrSubTexFileName,
        unsigned dwOption /*= 0*/, uint32_t uOwnerModelFlags /*= 0*/,
        BOOL bHighLoadPriority /*= false*/
    )
    {
        ASSERT(FALSE);
        return FALSE;
    }

    void* gfx::KGFX_MergedTexture2DArray::GetNativeImageHandle() const
    {
        return m_pTextureResource ? (void*)m_pTextureResource->GetNativeResourceHandle() : nullptr;
    }

    uint64_t gfx::KGFX_MergedTexture2DArray::GetId()
    {
        return (uint64_t)m_pTextureView->GetNativeHandle() + m_uTextureId;
    }

    uint32_t gfx::KGFX_MergedTexture2DArray::GetWidth() const
    {
        return m_uWidth;
    }

    uint32_t gfx::KGFX_MergedTexture2DArray::GetHeight() const
    {
        return m_uHeight;
    }

    uint64_t gfx::KGFX_MergedTexture2DArray::GetResourceSize()
    {
        return m_uMemoryByteSize;
    }

    int gfx::KGFX_MergedTexture2DArray::AddRef()
    {
        uint32_t n = 0;
        if (!IsProcedural())
        {
            ++m_nRefCount;
            n = m_nRefCount;
        }
        return n;
    }

    int gfx::KGFX_MergedTexture2DArray::Release()
    {
        int nRef = 0;
        if (!IsProcedural())
        {
            nRef = --m_nRefCount;
            ASSERT(nRef >= 0);
            if (nRef == 0)
            {
                KTexturePool* pTexturePool = (KTexturePool*)NSEngine::GetTexturePool();
                // 延时删除
                pTexturePool->RemoveTexture(this);
            }
        }
        return nRef;
    }

    int gfx::KGFX_MergedTexture2DArray::GetRef()
    {
        return m_nRefCount;
    }

    IKGFX_TextureResource* KGFX_MergedTexture2DArray::GetTextureResource() const
    {
        return m_pTextureResource;
    }

    IKGFX_TextureView* KGFX_MergedTexture2DArray::GetSRV() const
    {
        return m_pTextureView;
    }

    const KGFX_TextureDesc& KGFX_MergedTexture2DArray::GetTexDesc() const
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

    KGfxSubresourceRange KGFX_MergedTexture2DArray::ResolveSubresourceRange(const KGfxSubresourceRange& range)
    {
        KGfxSubresourceRange resolved = range;
        auto                 texDesc  = m_pTextureResource->GetDesc();

        resolved.uBaseMipLevel = std::min<uint32_t>(resolved.uBaseMipLevel, texDesc->uMipLevels - 1);
        resolved.uMipCount = std::min<uint32_t>(resolved.uMipCount, texDesc->uMipLevels - resolved.uBaseMipLevel);

        uint32_t arrayLayerCount = texDesc->uArraySize;
        resolved.uBaseArraySlice     = std::min<uint32_t>(resolved.uBaseArraySlice, arrayLayerCount - 1);
        resolved.uArrayCount     = std::min<uint32_t>(resolved.uArrayCount, arrayLayerCount - resolved.uBaseArraySlice);

        return resolved;
    }
} // namespace gfx

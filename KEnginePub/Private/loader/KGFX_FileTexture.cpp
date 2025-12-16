#include "KGFX_FileTexture.h"
#include "KTexturePool.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/thread/KThread.h"
#include "KBase/Public/async_task/KAsyncTaskManager.h"
#include "KBase/Public/io/KFile.h"
#include "KBase/Public/io/IFile.h"
#include "KEngine/Public/KEngineCore.h"
#include "Engine/File.h"
#include "KEnginePub/Private/loader/image/etcpak/EtcPak.h"
#include "KBase/Public/mics/KResourceErrorReporter.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
#if defined(_WIN32) && defined(_DEBUG)
    std::unordered_set<KGFX_FileTexture*> g_setFileTexture;
#endif

    KGFX_FileTexture::KGFX_FileTexture()
        : m_nRefCount(1)
    {
        // TestTGA();
        m_bStreamingLoad     = false;
        m_uLoadOption        = 0;
        m_bThreadLoad        = false;
        m_bLowTextureQuality = false;
        m_eTextureType       = TextureType::Texture2D;
        m_uNameHash          = 0;

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->nTextureVKCount;

#if defined(_WIN32) && defined(_DEBUG)
        g_setFileTexture.insert(this);
#endif
    }

    KGFX_FileTexture::~KGFX_FileTexture()
    {
        // 查bug用，先关掉
        // ASSERT(m_nRefCount == 0);
        if (m_pStreamingTask)
        {
            m_pStreamingTask->Cancel();
            m_pStreamingTask.Reset();
        }

        m_sTextureData.Destroy();
        m_sStreamingTextureData.Destroy();

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->nTextureVKCount;
#if defined(_WIN32) && defined(_DEBUG)
        g_setFileTexture.erase(this);
#endif
    }

    KUniqueStr KGFX_FileTexture::GetResourceName()
    {
        return m_ustrResourceName;
    }

    uint64_t KGFX_FileTexture::GetResourceSize()
    {
        // UNDONE KGFX_FileTexture [ wait check ] bytes (info from note)
        SetResourceSize();
        return m_uMemSize;
    }

    int KGFX_FileTexture::AddRef()
    {
        if (!IsProcedural())
        {
            ++m_nRefCount;
        }
        return 0;
    }

    int KGFX_FileTexture::Release()
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

    KEmuLoadAbleType KGFX_FileTexture::GetLoadAbleType()
    {
        return LOADABLE_TEXTURE_VK;
    }

    IKGFX_TextureResource* KGFX_FileTexture::GetImage() const
    {
        if (!IsPostLoaded())
        {
            // 可能走的多线程加载，还没加载完成
            return nullptr;
        }

        return m_sTextureData.pTextureResource;
    }

    IKGFX_TextureView* KGFX_FileTexture::GetImageView() const
    {
        if (!IsPostLoaded())
        {
            // 可能走的多线程加载，还没加载完成
            return nullptr;
        }

        return m_sTextureData.pTextureView;
    }

    uint32_t KGFX_FileTexture::GetMipMapCount()
    {
        return m_sTextureData.uMipLevels;
    }

    enumTextureFormat Util_GetImageFormatFromGLI(gli::format fmtType, bool& bSRGB, bool& bHasAlpha)
    {
        enumTextureFormat format = enumTextureFormat::TEX_FORMAT_NONE;
        bSRGB                    = false;
        bHasAlpha                = false;

        switch (fmtType)
        {
        case gli::FORMAT_RGBA32_SFLOAT_PACK32:
            format    = enumTextureFormat::TEX_FORMAT_R32G32B32A32_SFLOAT;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA8_UNORM_PACK8:
            format    = enumTextureFormat::TEX_FORMAT_R8G8B8A8_UNORM;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGB8_UNORM_PACK8:
            format = enumTextureFormat::TEX_FORMAT_R8G8B8_UNORM;
            break;
        case gli::FORMAT_R8_UNORM_PACK8:
            format = enumTextureFormat::TEX_FORMAT_R8_UNORM;
            break;
        case gli::FORMAT_L8_UNORM_PACK8:
            format = enumTextureFormat::TEX_FORMAT_R8_UNORM;
            break;
        case gli::FORMAT_BGRA8_UNORM_PACK8:
            format    = enumTextureFormat::TEX_FORMAT_B8G8R8A8_UNORM;
            bHasAlpha = true;
            break;
        case gli::FORMAT_BGR8_UNORM_PACK8:
            format = enumTextureFormat::TEX_FORMAT_B8G8R8_UNORM;
            break;
        case gli::FORMAT_RGBA8_SRGB_PACK8:
            format    = enumTextureFormat::TEX_FORMAT_R8G8B8A8_UNORM;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA16_UNORM_PACK16:
            format    = enumTextureFormat::TEX_FORMAT_R16G16B16A16_UNORM;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA16_SFLOAT_PACK16:
            format    = enumTextureFormat::TEX_FORMAT_R16G16B16A16_SFLOAT;
            bHasAlpha = true;
            break;
        case gli::FORMAT_L16_UNORM_PACK16:
            format = enumTextureFormat::TEX_FORMAT_R16_UNORM;
            break;
        case gli::FORMAT_RGBA_DXT1_UNORM_BLOCK8:
            format    = enumTextureFormat::TEX_FORMAT_BC1_RGBA_UNORM;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
            format = enumTextureFormat::TEX_FORMAT_BC1_RGB_UNORM;
            bSRGB  = true;
            break;
        case gli::FORMAT_RGBA_DXT1_SRGB_BLOCK8:
            format    = enumTextureFormat::TEX_FORMAT_BC1_RGBA_UNORM;
            bHasAlpha = true;
            bSRGB     = true;
            break;
        case gli::FORMAT_RGBA_DXT3_UNORM_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_BC2_UNORM;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_DXT3_SRGB_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_BC2_UNORM;
            bHasAlpha = true;
            bSRGB     = true;
            break;
        case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_BC3_UNORM;
            break;
        case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_BC3_UNORM;
            bSRGB  = true;
            break;
        case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_BC7_UNORM;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_BC6H_UFLOAT;
            break;
        case gli::FORMAT_RGB_BP_SFLOAT_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_BC6H_SFLOAT;
            break;
        case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_BC7_SRGB_UNORM;
            bHasAlpha = true;
            bSRGB = true;
            break;
        case gli::FORMAT_RGB_ETC2_UNORM_BLOCK8:
            format = enumTextureFormat::TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
            break;
        case gli::FORMAT_RGB_ETC2_SRGB_BLOCK8:
            format = enumTextureFormat::TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
            bSRGB  = true;
            break;
        case gli::FORMAT_RGBA_ETC2_UNORM_BLOCK8:
            format    = enumTextureFormat::TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_ETC2_SRGB_BLOCK8:
            format    = enumTextureFormat::TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
            bHasAlpha = true;
            bSRGB     = true;
            break;
        case gli::FORMAT_RGBA_ETC2_UNORM_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
            bHasAlpha = true;
            break;
        case gli::FORMAT_R_EAC_UNORM_BLOCK8:
            format = enumTextureFormat::TEX_FORMAT_ETC2_R_UNORM_BLOCK;
            break;
        case gli::FORMAT_R_EAC_SNORM_BLOCK8:
            format = enumTextureFormat::TEX_FORMAT_ETC2_R_SNORM_BLOCK;
            break;
        case gli::FORMAT_RG_EAC_UNORM_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_ETC2_RG_UNORM_BLOCK;
            break;
        case gli::FORMAT_RG_EAC_SNORM_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_ETC2_RG_SNORM_BLOCK;
            break;
        case gli::FORMAT_RGB10A2_UNORM_PACK32:
            format    = enumTextureFormat::TEX_FORMAT_A2R10G10B10_UNORM_PACK32;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_ASTC_4X4_UNORM_BLOCK;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_ASTC_4X4_UNORM_BLOCK;
            bSRGB     = true;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_ASTC_6X6_UNORM_BLOCK;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_ASTC_6X6_UNORM_BLOCK;
            bSRGB     = true;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_ASTC_8X8_UNORM_BLOCK;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16:
            format    = enumTextureFormat::TEX_FORMAT_ASTC_8X8_UNORM_BLOCK;
            bSRGB     = true;
            bHasAlpha = true;
            break;
        case gli::FORMAT_RG_ATI2N_UNORM_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_BC5_UNORM;
            break;
        case gli::FORMAT_RG_ATI2N_SNORM_BLOCK16:
            format = enumTextureFormat::TEX_FORMAT_BC5_SNORM;
            break;
        case gli::FORMAT_R_ATI1N_UNORM_BLOCK8:
            format = enumTextureFormat::TEX_FORMAT_BC4_UNORM;
            break;
        case gli::FORMAT_R_ATI1N_SNORM_BLOCK8:
            format = enumTextureFormat::TEX_FORMAT_BC4_SNORM;
            break;
        default:
            KGLogPrintf(KGLOG_ERR, "Maybe unSuport texture format(%u)", gli::FORMAT_R_ATI1N_SNORM_BLOCK8);
            break;
        }

        return format;
    }

    gli::format Util_GetGLIImageFormatFromVk(enumTextureFormat gfxFormat, bool bSRGB)
    {
        switch (gfxFormat)
        {
        case TEX_FORMAT_BC7_SRGB_UNORM:
            return gli::FORMAT_RGBA_BP_SRGB_BLOCK16;

        case TEX_FORMAT_R32G32B32A32_SFLOAT:
            return gli::FORMAT_RGBA32_SFLOAT_PACK32;

        case TEX_FORMAT_R8G8B8A8_UNORM:
            return bSRGB ? gli::FORMAT_RGBA8_SRGB_PACK8 : gli::FORMAT_RGBA8_UNORM_PACK8;

        case TEX_FORMAT_R8G8B8_UNORM:
            return gli::FORMAT_RGB8_UNORM_PACK8;

        case TEX_FORMAT_R8_UNORM:
            return gli::FORMAT_R8_UNORM_PACK8;

        case TEX_FORMAT_B8G8R8A8_UNORM:
            return gli::FORMAT_BGRA8_UNORM_PACK8;

        case TEX_FORMAT_B8G8R8_UNORM:
            return gli::FORMAT_BGR8_UNORM_PACK8;

        case TEX_FORMAT_R16G16B16A16_UNORM:
            return gli::FORMAT_RGBA16_UNORM_PACK16;

        case TEX_FORMAT_R16G16B16A16_SFLOAT:
            return gli::FORMAT_RGBA16_SFLOAT_PACK16;

        case TEX_FORMAT_R16_UNORM:
            return gli::FORMAT_L16_UNORM_PACK16;

        case TEX_FORMAT_BC1_RGBA_UNORM:
            return bSRGB ? gli::FORMAT_RGBA_DXT1_SRGB_BLOCK8 : gli::FORMAT_RGBA_DXT1_UNORM_BLOCK8;

        case TEX_FORMAT_BC1_RGB_UNORM:
            return bSRGB ? gli::FORMAT_RGB_DXT1_SRGB_BLOCK8 : gli::FORMAT_RGB_DXT1_UNORM_BLOCK8;

        case TEX_FORMAT_BC2_UNORM:
            return bSRGB ? gli::FORMAT_RGBA_DXT3_SRGB_BLOCK16 : gli::FORMAT_RGBA_DXT3_UNORM_BLOCK16;

        case TEX_FORMAT_BC3_UNORM:
            return bSRGB ? gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16 : gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16;

        case TEX_FORMAT_BC7_UNORM:
            return gli::FORMAT_RGBA_BP_UNORM_BLOCK16;

        case TEX_FORMAT_BC6H_UFLOAT:
            return gli::FORMAT_RGB_BP_UFLOAT_BLOCK16;

        case TEX_FORMAT_BC6H_SFLOAT:
            return gli::FORMAT_RGB_BP_SFLOAT_BLOCK16;

        case TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
            return bSRGB ? gli::FORMAT_RGB_ETC2_SRGB_BLOCK8 : gli::FORMAT_RGB_ETC2_UNORM_BLOCK8;

        case TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
            return bSRGB ? gli::FORMAT_RGBA_ETC2_SRGB_BLOCK8 : gli::FORMAT_RGBA_ETC2_UNORM_BLOCK8;

        case TEX_FORMAT_ETC2_R_UNORM_BLOCK:
            return gli::FORMAT_R_EAC_UNORM_BLOCK8;

        case TEX_FORMAT_ETC2_R_SNORM_BLOCK:
            return gli::FORMAT_R_EAC_SNORM_BLOCK8;

        case TEX_FORMAT_ETC2_RG_UNORM_BLOCK:
            return gli::FORMAT_RG_EAC_UNORM_BLOCK16;

        case TEX_FORMAT_ETC2_RG_SNORM_BLOCK:
            return gli::FORMAT_RG_EAC_SNORM_BLOCK16;

        case TEX_FORMAT_ASTC_4X4_UNORM_BLOCK:
            return bSRGB ? gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16 : gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16;

        case TEX_FORMAT_ASTC_6X6_UNORM_BLOCK:
            return bSRGB ? gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16 : gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16;

        case TEX_FORMAT_ASTC_8X8_UNORM_BLOCK:
            return bSRGB ? gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16 : gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16;

        case TEX_FORMAT_BC5_UNORM:
            return gli::FORMAT_RG_ATI2N_UNORM_BLOCK16;

        case TEX_FORMAT_BC5_SNORM:
            return gli::FORMAT_RG_ATI2N_SNORM_BLOCK16;

        case TEX_FORMAT_BC4_UNORM:
            return gli::FORMAT_R_ATI1N_UNORM_BLOCK8;

        case TEX_FORMAT_BC4_SNORM:
            return gli::FORMAT_R_ATI1N_SNORM_BLOCK8;

        case TEX_FORMAT_A2R10G10B10_UNORM_PACK32:
            return gli::FORMAT_RGB10A2_UNORM_PACK32;

        default:
            // 如果没有匹配项，可以返回一个无效格式
            return gli::FORMAT_UNDEFINED;
        }
    }

    TextureType KGFX_FileTexture::GetTextureType() const
    {
        return m_eTextureType;
    }

    // void* KGFX_FileTexture::ToImGuiTexutureId() const
    //{
    //	if (m_sTextureData.pTexturedesc)
    //	{
    //		KVulkantextureDesc* pTexDesc = (KVulkantextureDesc*)m_sTextureData.pTexturedesc;
    //		return pTexDesc->m_ImageInfo.imageView;
    //	}
    //	return nullptr;
    // }

    uint64_t KGFX_FileTexture::GetId()
    {
        return (uint64_t)m_sTextureData.pTextureView + m_uTextureId;
    }

    void KGFX_FileTexture::SetResourceSize()
    {
        PROF_CPU();

        int nUseFileMipsCount      = 0;
        int nMaxFileMipsLevel      = 0;
        int nStartUseFileMipsLevel = 0;
        int nRealWantedMips        = 0;

        KG_PROCESS_ERROR(m_sTextureData.pGLITexture);

        nUseFileMipsCount = (int)m_sTextureData.pGLITexture->levels();
        nMaxFileMipsLevel = (uint8_t)m_sTextureData.pGLITexture->levels() - 1;
        m_uMemSize        = 0;

        _AdjustRealWantedMipLevel((int)m_uWantedMips, nMaxFileMipsLevel, nRealWantedMips, nStartUseFileMipsLevel, nUseFileMipsCount);

        for (std::size_t stLayer = 0; stLayer < m_sTextureData.pGLITexture->layers(); ++stLayer)
        {
            for (std::size_t stFace = 0; stFace < m_sTextureData.pGLITexture->faces(); ++stFace)
            {
                for (int nMipLvl = nStartUseFileMipsLevel; nMipLvl < nStartUseFileMipsLevel + nUseFileMipsCount; ++nMipLvl)
                {
                    m_uMemSize += m_sTextureData.pGLITexture->size(nMipLvl);
                }
            }
        }

    Exit0:
        return;
    }

    BOOL KGFX_FileTexture::IMST_UpdateWantedMipmap(uint8_t uWantedMipmap)
    {
        PROF_CPU();

#if 0
        KGLOG_ASSERT_EXIT(IsMainThread());
        KGLOG_ASSERT_EXIT(IsLoaded());

        if (!IsPostLoaded())
            PostLoad();

        KG_PROCESS_ERROR(IsPostLoaded());
        ASSERT(m_ustrResourceName.IsValid());

        if (m_uWantedMips == uWantedMipmap)
            return TRUE;

        // 发起升降级任务，如果当前已有任务的话，返回，下次再执行
        if (m_pStreamingTask)
        {
            return FALSE;
        }
        ASSERT(!m_sStreamingTextureData.pGLITexture);

        m_sStreamingTextureData.Destroy();
        m_uWantedMips = uWantedMipmap;

        {
            uint64_t uLoadStreamingGLITextureTaskLaunchTimeInMs = NSKBase::GetCurTimeInMS();
            m_uLoadStreamingGLITextureTaskLaunchTimeInMs        = uLoadStreamingGLITextureTaskLaunchTimeInMs;

            NSKBase::KAsyncTask::TaskExecute fnTaskExecute = [this, uLoadStreamingGLITextureTaskLaunchTimeInMs]() {
                _DoLoadStreamingGLITextureAsync(uLoadStreamingGLITextureTaskLaunchTimeInMs);
            };
            NSKBase::KAsyncTask::TaskFinish fnTaskFinish = [this]() {
                this->_OnLoadStreamingGLITextureDone();
                m_pStreamingTask.Reset();
            };
            m_pStreamingTask = NSKBase::g_GetAsyncManager()->AddTask(NSKBase::EAsyncTaskPriority::Normal, false, std::move(fnTaskExecute), std::move(fnTaskFinish));
        }

    Exit0:
        return TRUE;
#else
        return TRUE;
#endif
    }

    BOOL KGFX_FileTexture::IMST_IsNeedStreaming(uint8_t uWantedMipLevel)
    {
        if (uWantedMipLevel == 0 || !m_bHasMips)
        {
            return FALSE;
        }
        if (!IsPostLoaded())
        {
            return FALSE;
        }
        if (m_nCurMipLevelCount < 1)
        {
            ASSERT(FALSE);
            return FALSE;
        }

        int nWantedMipLevel       = (int)uWantedMipLevel;
        int nRealWantMipLevel     = 0;
        int nStartUseFileMipLevel = 0;
        int nUseFileMipCount      = 0;

        _AdjustRealWantedMipLevel(nWantedMipLevel, m_nMaxFileMipsLevel, nRealWantMipLevel, nStartUseFileMipLevel, nUseFileMipCount);

        int nCurMaxUseMipLevel = (m_nCurMipLevelCount - 1);
        if (nRealWantMipLevel == nCurMaxUseMipLevel)
        {
            return FALSE;
        }

        if (nRealWantMipLevel > nCurMaxUseMipLevel && nCurMaxUseMipLevel == m_nMaxFileMipsLevel)
        {
            return FALSE;
        }

        return TRUE;
    }

    void KGFX_FileTexture::_DoLoadStreamingGLITextureAsync(uint64_t uLoadStreamingGLITextureTaskLaunchTimeInMs)
    {
        PROF_CPU();

        ASSERT(!IsMainThread());
        if (m_uLoadStreamingGLITextureTaskLaunchTimeInMs != uLoadStreamingGLITextureTaskLaunchTimeInMs)
            return;
        ASSERT(!m_sStreamingTextureData.pGLITexture);
        SAFE_DELETE(m_sStreamingTextureData.pGLITexture);

        m_sStreamingTextureData.pGLITexture = new gli::texture();
        ASSERT(m_sStreamingTextureData.pGLITexture);
        // 程序创建贴图没有文件名
        if (!m_ustrRealResourceName.IsValid())
        {
            return;
        }
        *m_sStreamingTextureData.pGLITexture = gli::load(m_ustrRealResourceName.Str());

        _LoadInternal(m_sStreamingTextureData);
    }

    void KGFX_FileTexture::_OnLoadStreamingGLITextureDone()
    {
        PROF_CPU();
        ASSERT(IsMainThread());

        KG_PROCESS_ERROR(!NSEngine::g_pEngineCore->IsClosing());
        KG_PROCESS_ERROR(m_sStreamingTextureData.pGLITexture && !m_sStreamingTextureData.pGLITexture->empty());
        KG_PROCESS_ERROR(m_sStreamingTextureData.pGLICommonTex && !m_sStreamingTextureData.pGLICommonTex->empty());

        m_uTextureUpdateCode += (uint32_t)(this->GetId() + 1);

        m_sTextureData = std::move(m_sStreamingTextureData);
        KG_ASSERT_EXIT(m_sTextureData.pGLITexture && !m_sStreamingTextureData.pGLITexture);
        KG_ASSERT_EXIT(m_sTextureData.pGLICommonTex && !m_sStreamingTextureData.pGLICommonTex);

        PostLoad();
        KGLOG_PROCESS_ERROR(IsPostLoaded());
    Exit0:
        m_sStreamingTextureData.Destroy();
        SAFE_RELEASE(m_pStreamingTask);
    }

    BOOL KGFX_FileTexture::_IsNeedLowMipLevelMode()
    {
        KEngineOptions* pEngineOptions = NSEngine::GetEngineOptions();
        if (pEngineOptions->eSystemMemSize == EX3DSystemMemSize::Low)
        {
            return TRUE;
        }

#ifdef _WIN32
        if (DrvOption::fLocalGpuMemoryGB < 2.1f)
        {
            return TRUE;
        }
#endif

        return FALSE;
    }

    void KGFX_FileTexture::_AdjustRealWantedMipLevel(
        int  nWantedMipLevel,
        int  nMaxFileMipLevel,
        int& nRealWantMipLevel,
        int& nStartUseFileMipLevel,
        int& nUseFileMipCount
    )
    {
        BOOL            bNeedLowMipLevelMode = _IsNeedLowMipLevelMode();
        KEngineOptions* pEngineOptions       = NSEngine::GetEngineOptions();

        nStartUseFileMipLevel = 0;
        nUseFileMipCount      = nMaxFileMipLevel + 1;

        nRealWantMipLevel = nWantedMipLevel;
        if (bNeedLowMipLevelMode)
        {
            if (HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::Model))
            {
                nRealWantMipLevel = 7; // 128
                if (HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::MainPlayerModel))
                {
                    // 主角贴图 使用原始分辨率
                    nRealWantMipLevel = 0;
                }
                else if (HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::GamePlayModel))
                {
                    // Npc 等模型贴图
                    nRealWantMipLevel = 8; // 256
                }
            }
        }
        else
        {
            if (HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::Model))
            {
#if MACRO_X3D_ENABLE_TEX_STREAM
                int nMaxTextureStreamingMipLevel = pEngineOptions->nMaxTextureStreamingMipLevel;
                ASSERT(nMaxTextureStreamingMipLevel >= 0);
                if (nMaxTextureStreamingMipLevel > 0 && ((nRealWantMipLevel == 0) || (nRealWantMipLevel > nMaxTextureStreamingMipLevel)))
                {
                    nRealWantMipLevel = nMaxTextureStreamingMipLevel;
                }
#endif

                // 表现和性能折中，只有主角贴图 使用原始分辨率
                if (HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::MainPlayerModel))
                {
                    nRealWantMipLevel = 0;
                }
                else
                {
                    if (nRealWantMipLevel == 0)
                    {
                        nRealWantMipLevel = nMaxFileMipLevel;
                    }
                    // ASSERT(nRealWantMipLevel > 0);

                    // 非主角客户端创建模型 贴图最小分辨率限制
                    BOOL bGamePlayModel                   = HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::GamePlayModel);
                    //					int  nMinGamePlayModelTextureMipLevel = pEngineOptions->nMinGamePlayModelTextureMipLevel;
                    // 					if (nMinGamePlayModelTextureMipLevel > 0 && bGamePlayModel && nRealWantMipLevel > 0)
                    // 					{
                    // 						nRealWantMipLevel = NSKMath::Max(nMinGamePlayModelTextureMipLevel, nRealWantMipLevel);
                    // 					}
                    // 非主角客户端创建模型 贴图最大分辨率限制
                    int  nMaxGamePlayModelTextureMipLevel = pEngineOptions->nMaxGamePlayModelTextureMipLevel;
                    if (nMaxGamePlayModelTextureMipLevel > 0 && bGamePlayModel && nRealWantMipLevel > 0)
                    {
                        nRealWantMipLevel = NSKMath::Min(nMaxGamePlayModelTextureMipLevel, nRealWantMipLevel);
                    }

                    // 非客户端创建模型（场景模型） 贴图最大分辨率限制
                    int nMaxSceneActorModelTextureMipLevel = pEngineOptions->nMaxSceneActorModelTextureMipLevel;
                    if (nMaxSceneActorModelTextureMipLevel > 0 && !bGamePlayModel && nRealWantMipLevel > 0)
                    {
                        nRealWantMipLevel = NSKMath::Min(nMaxSceneActorModelTextureMipLevel, nRealWantMipLevel);
                    }
                }
            }
        }

        if (nRealWantMipLevel != 0)
        {
            // 反的: m_uWantedMips 0 指向最小层，FileMipsLevel 0 指向最大层
            nStartUseFileMipLevel = NSKMath::Max(0, nMaxFileMipLevel - nRealWantMipLevel);
            nUseFileMipCount      = nMaxFileMipLevel - nStartUseFileMipLevel + 1;
        }
    }

    void* KGFX_FileTexture::GetNativeImageHandle() const
    {
        IKGFX_TextureResource* pTextureResource = GetImage();
        if (pTextureResource)
        {
            return (void*)pTextureResource->GetNativeResourceHandle();
        }
        return nullptr;
    }

    IKGFX_TextureResource* KGFX_FileTexture::GetTextureResource() const
    {
        return GetImage();
    }

    IKGFX_TextureView* KGFX_FileTexture::GetSRV() const
    {
        return GetImageView();
    }

    const KGFX_TextureDesc& KGFX_FileTexture::GetTexDesc() const
    {
        auto TextureResource = GetImage();
        if (TextureResource)
        {
            return *TextureResource->GetDesc();
        }
        else
        {
            return KGFX_TextureDesc::g_EmptryValue;
        }
    }

    KGfxSubresourceRange KGFX_FileTexture::ResolveSubresourceRange(const KGfxSubresourceRange& range)
    {
        KGfxSubresourceRange resolved = range;

        resolved.uBaseMipLevel = std::min<uint32_t>(resolved.uBaseMipLevel, m_sTextureData.uMipLevels - 1);
        resolved.uMipCount = std::min<uint32_t>(resolved.uMipCount, m_sTextureData.uMipLevels - resolved.uBaseMipLevel);

        uint32_t arrayLayerCount = m_sTextureData.uArraySize;
        resolved.uBaseArraySlice     = std::min<uint32_t>(resolved.uBaseArraySlice, arrayLayerCount - 1);
        resolved.uArrayCount     = std::min<uint32_t>(resolved.uArrayCount, arrayLayerCount - resolved.uBaseArraySlice);

        return resolved;
    }


    void KGFX_FileTexture::tagTextureData::Destroy()
    {
        PROF_CPU();

        SAFE_RELEASE(pTextureView);
        SAFE_RELEASE(pTextureResource);
        SAFE_DELETE(pGLITexture);
        SAFE_DELETE(pGLICommonTex);

        vecSubResourceData.clear();
    }

    KGFX_FileTexture::tagTextureData& KGFX_FileTexture::tagTextureData::operator=(tagTextureData&& other) noexcept
    {
        Destroy();

        pGLITexture      = other.pGLITexture;
        pGLICommonTex    = other.pGLICommonTex;
        pTextureResource = other.pTextureResource;
        pTextureView     = other.pTextureView;

        uWidth        = other.uWidth;
        uHeight       = other.uHeight;
        uDepth        = other.uDepth;
        uBaseMipLevel = other.uBaseMipLevel;
        uMipLevels    = other.uMipLevels;
        uArraySize    = other.uArraySize;
        Format        = other.Format;
        TextureType   = other.TextureType;
        bHasAlpha     = other.bHasAlpha;

        std::swap(vecSubResourceData, other.vecSubResourceData);

        other.pGLITexture      = nullptr;
        other.pGLICommonTex    = nullptr;
        other.pTextureResource = nullptr;
        other.pTextureView     = nullptr;

        return *this;
    }

    BOOL KGFX_FileTexture::_LoadInternal(tagTextureData& sTextureData)
    {
        PROF_CPU();

        BOOL            bResult        = FALSE;
        IKGFileLibrary* pFileLibrary   = nullptr;
        IKG_Buffer*     pTexFileBuffer = nullptr;

        pFileLibrary = GetFileLibraryInterface();
        KGLOG_ASSERT_EXIT(pFileLibrary);

        if (!sTextureData.pGLITexture)
        {
            if (!m_ustrPackName.IsValid())
            {
                char        szConvResName[NSEngine::MAX_PATH_LEN] = "";
                const char* pcszRealResName                       = m_ustrResourceName.Str();

                g_StrCatLen(szConvResName, m_ustrResourceName.Str(), countof(szConvResName));
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
                KGLOG_ASSERT_EXIT(pcszRealResName && pcszRealResName[0]);

                if (KSTR_HELPER::StrEndWith(pcszRealResName, ".dds"))
                {
                    m_bDDS = TRUE;
                }
                else if (KSTR_HELPER::StrEndWith(pcszRealResName, ".tga"))
                {
                    m_bTGA = TRUE;
                }

#if !defined(_WIN32) && !defined(__MACOS__)
                if (m_bDDS)
                {
                    KGLogPrintf(KGLOG_ERR, "[X3DEngine] dds is not support on mobile device, res: %s.", pcszRealResName);
                    KG_PROCESS_ERROR(FALSE);
                }
#endif

                KGLOG_ASSERT_EXIT(!sTextureData.pGLITexture);
                sTextureData.pGLITexture = new gli::texture();
                KGLOG_ASSERT_EXIT(sTextureData.pGLITexture);
                if (pcszRealResName)
                {
                    PROF_CPU("KGFX_FileTexture::Load_gli_load");
                    *sTextureData.pGLITexture = gli::load(pcszRealResName);
                }
                if (sTextureData.pGLITexture->empty())
                {
                    KGLogPrintf(KGLOG_ERR, "[X3DEngine] KGFX_FileTexture::Load: %s load failed.", pcszRealResName);
                    KG_PROCESS_ERROR(false);
                }
                m_ustrRealResourceName = g_CachePathString(pcszRealResName, TRUE);
            }
            else
            {
                KGLOG_ASSERT_EXIT(m_ustrPackName.IsValid() && m_ustrTexSetSubFileName.IsValid());

                char szConvResName[NSEngine::MAX_PATH_LEN] = "";
                const char* pcszRealSubFileName = m_ustrTexSetSubFileName.Str();

                g_StrCatLen(szConvResName, m_ustrTexSetSubFileName.Str(), countof(szConvResName));

#if defined(_WIN32) || defined(__MACOS__)
                strcat(szConvResName, ".dds");
#else
                strcat(szConvResName, ".ktx");
#endif
                if (pFileLibrary->IndexPkgSubFileExist(m_ustrPackName.Str(), szConvResName))
                {
                    pcszRealSubFileName = szConvResName;
                }
                KGLOG_ASSERT_EXIT(pcszRealSubFileName && pcszRealSubFileName[0]);

                if (KSTR_HELPER::StrEndWith(pcszRealSubFileName, ".dds"))
                {
                    m_bDDS = TRUE;
                }
                else if (KSTR_HELPER::StrEndWith(pcszRealSubFileName, ".tga"))
                {
                    m_bTGA = TRUE;
                }

#if !defined(_WIN32) && !defined(__MACOS__)
                if (m_bDDS)
                {
                    KGLogPrintf(KGLOG_ERR, "[X3DEngine] dds is not support on mobile device, res: %s.", m_ustrResourceName.Str());
                    KG_PROCESS_ERROR(FALSE);
                }
#endif
                pTexFileBuffer = pFileLibrary->IndexPkgGetSubFileByName(m_ustrPackName.Str(), pcszRealSubFileName);
                KGLOG_PROCESS_ERROR(pTexFileBuffer);

                KGLOG_ASSERT_EXIT(!sTextureData.pGLITexture);
                sTextureData.pGLITexture = new gli::texture();
                KGLOG_ASSERT_EXIT(sTextureData.pGLITexture);
                {
                    PROF_CPU("KGFX_FileTexture::Load_gli_load");
                    *sTextureData.pGLITexture = gli::load(pTexFileBuffer, m_ustrTexSetSubFileName.Str());
                }
                if (sTextureData.pGLITexture->empty())
                {
                    KGLogPrintf(KGLOG_ERR, "[X3DEngine] KGFX_FileTexture::Load: %s load failed.", m_ustrResourceName.Str());
                    KG_PROCESS_ERROR(false);
                }
                m_ustrRealResourceName = m_ustrResourceName;
            }
        }
        else
        {
            ASSERT(!sTextureData.pGLITexture->empty());
            ASSERT(m_ustrResourceName.IsValid());
        }

        // up slice texture
        _LoadInternalUpSlice(sTextureData);

        SetResourceSize();

        bResult = _ParseGLITextureAfterLoad(sTextureData);
    Exit0:
        SAFE_RELEASE(pTexFileBuffer);
        SetLoadState(bResult ? ELoadableState::Loaded : ELoadableState::Failed);
        if (!bResult)
        {
            SAFE_DELETE(sTextureData.pGLITexture);
            SAFE_DELETE(sTextureData.pGLICommonTex);
        }
        return bResult;
    }

    void KGFX_FileTexture::_LoadInternalUpSlice(tagTextureData& sTextureData)
    {
        PROF_CPU();

        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        int                        nUpSliceTextureCount             = 0;
        char                       szUpSliceTextureFolder[MAX_PATH] = "";
        char                       szUpSliceTextureName[MAX_PATH]   = "";
        char                       szUpSliceTextureExt[MAX_PATH]    = "";
        char                       szUpSliceTexturePath[MAX_PATH]   = "";
        gli::texture*              pGliTexture                      = nullptr;
        std::vector<gli::texture*> vecGLIUpSliceTexture;
        gli::texture*              pGLIMergeTexture = nullptr;

        KGLOG_ASSERT_EXIT(sTextureData.pGLITexture);
        KGLOG_ASSERT_EXIT(!sTextureData.pGLICommonTex);
        KG_PROCESS_ERROR(m_ustrRealResourceName.IsValid()); // 等memtexture逻辑拆除去 再改回ASSERT
        // KGLOG_ASSERT_EXIT(m_ustrRealResourceName.IsValid());

        nUpSliceTextureCount = (int)sTextureData.pGLITexture->uUpsliceCount;
        KG_PROCESS_SUCCESS(nUpSliceTextureCount <= 0);

        bRetCode = KSTR_HELPER::SplitPath(m_ustrRealResourceName.Str(), szUpSliceTextureFolder, szUpSliceTextureName, szUpSliceTextureExt);
        KGLOG_ASSERT_EXIT(bRetCode);

        vecGLIUpSliceTexture.reserve(nUpSliceTextureCount);
        for (int i = 0; i < nUpSliceTextureCount; ++i)
        {
            snprintf(szUpSliceTexturePath, countof(szUpSliceTexturePath), "%s/%s#%d%s", szUpSliceTextureFolder, szUpSliceTextureName, i, szUpSliceTextureExt);
            szUpSliceTexturePath[countof(szUpSliceTexturePath) - 1] = 0;

            // 为了验证，强制拉下来
            //{
            //	auto pTestFile = g_OpenFile(szUpSliceTexturePath);
            //	SAFE_RELEASE(pTestFile);
            //}

            bRetCode = g_IsFileExistInLocal(szUpSliceTexturePath);
            if (!bRetCode)
            {
                ASSERT(i == 0);
                KG_PROCESS_ERROR(FALSE);
            }

            pGliTexture = new gli::texture();
            KGLOG_ASSERT_EXIT(pGliTexture);
            *pGliTexture = gli::load(szUpSliceTexturePath);
            if (pGliTexture->empty())
            {
                KGLogPrintf(KGLOG_ERR, "[X3DEngine] _LoadInternalUpSlice::Load: %s load failed.", szUpSliceTexturePath);
                KG_PROCESS_ERROR(FALSE);
            }
            KGLOG_ASSERT_EXIT(pGliTexture->target() == sTextureData.pGLITexture->target());
            KGLOG_ASSERT_EXIT(pGliTexture->layers() == sTextureData.pGLITexture->layers());
            KGLOG_ASSERT_EXIT(pGliTexture->faces() == sTextureData.pGLITexture->faces());

            vecGLIUpSliceTexture.push_back(pGliTexture);
            pGliTexture = nullptr;
        }

        {
            PROF_CPU("[KGFX_FileTexture::_LoadInternalUpSlice] merge");
            gli::texture* pGLIMaxLevelSliceTexture = vecGLIUpSliceTexture[0];
            ASSERT(pGLIMaxLevelSliceTexture && !pGLIMaxLevelSliceTexture->empty());

            gli::target          eTarget  = sTextureData.pGLITexture->target();
            gli::format          eFormat  = sTextureData.pGLITexture->format();
            const auto&          sExtent  = pGLIMaxLevelSliceTexture->extent();
            size_t               stLayers = sTextureData.pGLITexture->layers();
            size_t               stFaces  = sTextureData.pGLITexture->faces();
            size_t               stLevels = sTextureData.pGLITexture->levels() + vecGLIUpSliceTexture.size();
            NSKMath::KVectorInt2 v2UpSliceSize;

            pGLIMergeTexture = new gli::texture(eTarget, eFormat, sExtent, stLayers, stFaces, stLevels);
            KGLOG_ASSERT_EXIT(pGLIMergeTexture);
            for (std::size_t stLayer = 0; stLayer < stLayers; ++stLayer)
            {
                for (std::size_t stFace = 0; stFace < stFaces; ++stFace)
                {
                    v2UpSliceSize.Set(0, 0);
                    for (std::size_t stLevel = 0; stLevel < stLevels; ++stLevel)
                    {
                        size_t        stSrcLevel    = 0;
                        gli::texture* pGLSrcTexture = nullptr;
                        if (stLevel < vecGLIUpSliceTexture.size())
                        {
                            stSrcLevel    = 0;
                            pGLSrcTexture = vecGLIUpSliceTexture[stLevel];
                        }
                        else
                        {
                            stSrcLevel = stLevel - vecGLIUpSliceTexture.size();
                            ASSERT(stSrcLevel >= 0 && stSrcLevel < sTextureData.pGLITexture->levels());
                            pGLSrcTexture = sTextureData.pGLITexture;
                        }

                        // check mip size is legal
                        const auto& sSliceExtent = pGLSrcTexture->extent(stSrcLevel);
                        if (!v2UpSliceSize.IsZero())
                        {
                            bool bValidSlice = (std::max(v2UpSliceSize.x / 2, 1) == sSliceExtent.x) && (std::max(v2UpSliceSize.y / 2, 1) == sSliceExtent.y);
                            if (!bValidSlice)
                            {
                                KGLogPrintf(KGLOG_ERR, "[_LoadInternalUpSlice] invalid slice texture:5s, %d", m_ustrRealResourceName.Str(), (int)stLevel);
                                KGLOG_ASSERT_EXIT(FALSE);
                            }
                        }
                        v2UpSliceSize.x = sSliceExtent.x;
                        v2UpSliceSize.y = sSliceExtent.y;

                        pGLIMergeTexture->copy(*pGLSrcTexture, stLayer, stFace, stSrcLevel, stLayer, stFace, stLevel);
                    }
                }
            }

            SAFE_DELETE(sTextureData.pGLITexture);
            sTextureData.pGLITexture = pGLIMergeTexture;
            pGLIMergeTexture         = nullptr;
        }

    Exit1:
        bResult = TRUE;
    Exit0:
        for (auto pGliUpSliceTexture : vecGLIUpSliceTexture)
        {
            SAFE_DELETE(pGliUpSliceTexture);
        }
        vecGLIUpSliceTexture.clear();
        SAFE_DELETE(pGliTexture);
        SAFE_DELETE(pGLIMergeTexture);
        return;
    }

    BOOL KGFX_FileTexture::Load()
    {
        PROF_CPU();

        BOOL bResult = _LoadInternal(m_sTextureData);
        SetLoadState(bResult ? ELoadableState::Loaded : ELoadableState::Failed);
        //if (IsLoadFailed())
        //{
        //    NSKBase::KResourceErrorReporter::ReportResourceMissing("texture", m_ustrResourceName.Str(), "");
        //}
        return bResult;
    }

    BOOL KGFX_FileTexture::PostLoad()
    {
        PROF_CPU();
        std::lock_guard<std::mutex> lock(m_lock);

        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        KGLOG_PROCESS_ERROR(IsLoaded() || IsLoadFailed());
        KG_PROCESS_SUCCESS(IsPostLoaded());
        KGLOG_PROCESS_ERROR(m_sTextureData.pGLITexture && !m_sTextureData.pGLITexture->empty());

        {
            gli::target texTarget = ((gli::texture*)m_sTextureData.pGLITexture)->target();

            bRetCode = _ParseGLITextureInPostLoad();
            switch (texTarget)
            {
            case gli::TARGET_1D:
                m_eTextureType = TextureType::Texture1D;
                break;
            case gli::TARGET_1D_ARRAY:
                m_eTextureType = TextureType::Texture1DArray;
                break;
            case gli::TARGET_2D:
                m_eTextureType = TextureType::Texture2D;
                break;
            case gli::TARGET_2D_ARRAY:
                m_eTextureType = TextureType::Texture2DArray;
                break;
            case gli::TARGET_3D:
                m_eTextureType = TextureType::Texture3D;
                break;
            case gli::TARGET_CUBE:
                m_eTextureType = TextureType::Cubemap;
                break;
            case gli::TARGET_CUBE_ARRAY:
                m_eTextureType = TextureType::CubemapArray;
                break;
            default:
                break;
            };

            KGLOG_PROCESS_ERROR(bRetCode);
        }

        if (m_sTextureData.pTextureResource)
        {
            m_sTextureData.pTextureResource->SetDebugName(m_ustrResourceName.Str());
        }

        // 现在材质系统不知道纹理的具体类型
        //      if (m_eRequestTextureType != m_eTextureType)
        //      {
        //          KGLogPrintf(KGLOG_ERR, "[KGFX_FileTexture::PostLoad] load texture type:%d is not equal to request type:%d!!!", (int)m_eTextureType, (int)m_eRequestTextureType);
        //      }

    Exit1:
        m_nCurMipLevelCount = m_sTextureData.uMipLevels;

        if (m_sTextureData.pGLITexture && m_sTextureData.pGLITexture->levels() > 0)
        {
            m_nMaxFileMipsLevel = (int)m_sTextureData.pGLITexture->levels() - 1;
        }

        bResult = TRUE;
    Exit0:
        SetLoadState(bResult ? ELoadableState::PostLoaded : ELoadableState::Failed);
        {
            PROF_CPU("KGFX_FileTexture::PostLoad_delete");

            auto pAsyncMgr = NSKBase::g_GetAsyncManager();
            if (pAsyncMgr)
            {
                gli::texture* pDelGLITexture1 = m_sTextureData.pGLITexture;
                m_sTextureData.pGLITexture    = nullptr;
                gli::texture* pDelGLITexture2 = m_sTextureData.pGLICommonTex;
                m_sTextureData.pGLICommonTex  = nullptr;
                pAsyncMgr->AddTask(
                    NSKBase::EAsyncTaskPriority::Low,
                    false,
                    [pDelGLITexture1, pDelGLITexture2] {
                        if (pDelGLITexture1)
                            delete pDelGLITexture1;
                        if (pDelGLITexture2)
                            delete pDelGLITexture2;
                    },
                    nullptr
                );
            }
            else
            {
                SAFE_DELETE(m_sTextureData.pGLITexture);
                SAFE_DELETE(m_sTextureData.pGLICommonTex);
            }
        }

        return IsPostLoaded();
    }

    BOOL KGFX_FileTexture::LoadFromFile(const char* pcszFileName, uint32_t dwOption, uint32_t uOwnerModelFlags, BOOL bHighLoadPriority)
    {
        BOOL bResult     = FALSE;
        BOOL bRetCode    = FALSE;
        BOOL bThreadLoad = FALSE;

        KGLOG_PROCESS_ERROR(pcszFileName && pcszFileName[0]);

        m_uLoadOption      = (uint32_t)dwOption;
        m_uOwnerModelFlags = uOwnerModelFlags;
        m_uNameHash        = KSTR_HELPER::GetHashCodeForString64Bit(pcszFileName);
        m_ustrResourceName = g_CachePathString(pcszFileName, TRUE);

        bThreadLoad = ENABLE_RESOURCE_MULTITHREAD_LOAD && (m_uLoadOption & RESOURCE_LOAD_MULTITHREAD);
        if (bThreadLoad)
        {
            NSKBase::EAsyncTaskPriority ePriority = bHighLoadPriority ? NSKBase::EAsyncTaskPriority::High : NSKBase::EAsyncTaskPriority::Normal;
            // 以此逻辑模拟场景物件贴图
            if (HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::Model) && !HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::GamePlayModel))
            {
                ePriority = NSKBase::EAsyncTaskPriority::AboveHigh;
            }
            LoadAsync(ePriority);
        }
        else
        {
            bRetCode = LoadSync();
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_FileTexture::LoadFromTexsetPack(
        const KUniqueStr& ustrTexsetPackName, const KUniqueStr& ustrSubTexFileName,
        uint32_t dwOption /*= 0*/, uint32_t uOwnerModelFlags /*= 0*/,
        BOOL bHighLoadPriority /*= false*/
    )
    {
        BOOL bResult     = FALSE;
        BOOL bRetCode    = FALSE;
        BOOL bThreadLoad = FALSE;

        KGLOG_PROCESS_ERROR(ustrTexsetPackName.IsValid() && ustrSubTexFileName.IsValid());

        m_uLoadOption      = (uint32_t)dwOption;
        m_uOwnerModelFlags = uOwnerModelFlags;
        {
            char szFullName[MAX_PATH] = "";
            // 和 KTexturePool::_RequestTextureVK 中 Key 一致
            snprintf(szFullName, countof(szFullName), "%s%s%s", ustrTexsetPackName.Str(), NSKBase::tagFileLocation::GetPackNameAndSubFileSeparatorStr(), ustrSubTexFileName.Str());
            szFullName[countof(szFullName) - 1] = 0;
            m_uNameHash                         = KSTR_HELPER::GetHashCodeForString64Bit(szFullName);
            m_ustrResourceName                  = g_CachePathString(szFullName, TRUE);
        }
        m_ustrPackName    = ustrTexsetPackName;
        m_ustrTexSetSubFileName = ustrSubTexFileName;

        bThreadLoad = ENABLE_RESOURCE_MULTITHREAD_LOAD && (m_uLoadOption & RESOURCE_LOAD_MULTITHREAD);
        if (bThreadLoad)
        {
            NSKBase::EAsyncTaskPriority ePriority = bHighLoadPriority ? NSKBase::EAsyncTaskPriority::High : NSKBase::EAsyncTaskPriority::Normal;
            // 以此逻辑模拟场景物件贴图
            if (HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::Model) && !HasOwnerModelFlag((uint32_t)EResourceOwnerModelFlag::GamePlayModel))
            {
                ePriority = NSKBase::EAsyncTaskPriority::AboveHigh;
            }
            LoadAsync(ePriority);
        }
        else
        {
            bRetCode = LoadSync();
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_FileTexture::LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData)
    {
        BOOL           bRetCode     = FALSE;
        gli::format    fmt          = gli::FORMAT_RGBA8_UNORM_PACK8;
        int            outsize      = uWidth * uHeight * 4;
        int            channels     = 0;
        int            width        = uWidth;
        int            height       = uHeight;
        unsigned char* pBuffer_Data = nullptr;

        SAFE_DELETE(m_sTextureData.pGLITexture);

        m_sTextureData.pGLITexture = new gli::texture(
            gli::TARGET_2D,
            fmt,
            gli::texture::extent_type(
                width,
                height,
                1
            ),
            1,
            1,
            1
        );
        KGLOG_PROCESS_ERROR(m_sTextureData.pGLITexture);

        pBuffer_Data = (unsigned char*)m_sTextureData.pGLITexture->data(0, 0, 0);
        memcpy(pBuffer_Data, pData, outsize);

        PostLoad();
        bRetCode = TRUE;
    Exit0:
        return bRetCode;
    }

    BOOL KGFX_FileTexture::LoadFromRGBA8DataAndCompress(unsigned int uWidth, unsigned int uHeight, const void* pData)
    {
        BOOL      bResult   = FALSE;
        BOOL      bRetCode  = FALSE;
        uint8_t*  pFileData = nullptr;
        size_t    fileSize  = 0;
        uint32_t* buffer    = (uint32_t*)pData;

        m_bIsMetaFileTexture = TRUE;

        m_ustrResourceName = g_CachePathString("FaceMakerMetaTexture", TRUE);
#ifdef _WIN32
        ETC_PAK::TEX_TYPE texType = ETC_PAK::DXTC;
#else
        ETC_PAK::TEX_TYPE texType = ETC_PAK::ETC2;
#endif //
        bRetCode = ETC_PAK::BuildCompressdFileData(&pFileData, fileSize, buffer, uWidth, uHeight, true, false, texType);
        KGLOG_PROCESS_ERROR(bRetCode);

        // 		bRetCode = Create(uWidth, uHeight, gfx::enumTextureFormat::TEX_FORMAT_BC3_UNORM, pFileData, (uint32_t)fileSize);
        // 		KGLOG_PROCESS_ERROR(bRetCode);
        {
            ASSERT(!m_sTextureData.pGLITexture);
            m_sTextureData.pGLITexture = new gli::texture();
            ASSERT(m_sTextureData.pGLITexture);
            *m_sTextureData.pGLITexture = gli::load((char const*)pFileData, fileSize);
            // 			bRetCode = Create(uWidth, uHeight, gfx::enumTextureFormat::TEX_FORMAT_BC3_UNORM, GliTexture.data(), (uint32_t)GliTexture.size());
            // 			KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = TRUE;
    Exit0:
        SetLoadState(bResult ? ELoadableState::PostLoaded : ELoadableState::Failed);
        if (pFileData)
        {
            delete[] pFileData;
            pFileData = nullptr;
        }
        return bResult;
    }

    BOOL KGFX_FileTexture::LoadCompressedRGBA8Data(const void* pData, unsigned int uFileSize, const char* cszFileName)
    {
        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        m_bIsMetaFileTexture = TRUE;

        m_ustrResourceName = g_CachePathString(cszFileName, TRUE);

        ASSERT(!m_sTextureData.pGLITexture);
        m_sTextureData.pGLITexture = new gli::texture();
        ASSERT(m_sTextureData.pGLITexture);
        *m_sTextureData.pGLITexture = gli::load((char const*)pData, uFileSize);

        _LoadInternal(m_sTextureData);
        KGLOG_PROCESS_ERROR(m_sTextureData.pGLICommonTex);

        bResult = TRUE;
    Exit0:
        SetLoadState(bResult ? ELoadableState::PostLoaded : ELoadableState::Failed);
        return bResult;
    }

    uint64_t KGFX_FileTexture::GetNameHash()
    {
        return m_uNameHash;
    }

    BOOL KGFX_FileTexture::_ParseGLITextureAfterLoad(tagTextureData& sTextureData)
    {
        PROF_CPU();

        ASSERT(sTextureData.pGLITexture && !sTextureData.pGLITexture->empty());
        if (sTextureData.pGLITexture->empty())
            return FALSE;

        sTextureData.vecSubResourceData.clear();
        SAFE_DELETE(sTextureData.pGLICommonTex);

        BOOL        bResult    = FALSE;
        BOOL        bRetCode   = FALSE;
        gli::target eTexTarget = sTextureData.pGLITexture->target();

        uint32_t uImgWidth  = 1;
        uint32_t uImgHeight = 1;
        uint32_t uImgDepth  = 1;

        uint32_t uStagingMemOffset = 0;
        uint32_t uStagingMemSize   = 0;

        //////////////////////////////////////////////////////////////////////////
        int      nUseFileMipsCount      = 0;
        int      nMaxFileMipsLevel      = 0;
        int      nStartUseFileMipsLevel = 0;
        int      nRealWantedMips        = 0;
        uint32_t uTextureDataSize       = 0;
        bool     bArrayImage            = false;

        nUseFileMipsCount = (int)sTextureData.pGLITexture->levels();
        nMaxFileMipsLevel = (uint8_t)sTextureData.pGLITexture->levels() - 1;

        m_bHasMips = (nUseFileMipsCount > 1);
#ifdef _DEBUG
        if (!m_bHasMips && !m_bTGA)
        {
            KGLogPrintf(KGLOG_ERR, "[X3DEngine] texture has no mipmaps: %s", m_ustrRealResourceName.Str());
        }
#endif
        m_sTextureData.m_ustrTextureName = m_ustrRealResourceName;
        _AdjustRealWantedMipLevel((int)m_uWantedMips, nMaxFileMipsLevel, nRealWantedMips, nStartUseFileMipsLevel, nUseFileMipsCount);
        //////////////////////////////////////////////////////////////////////////

        gli::texture1d*          p1DTex      = nullptr; // new gli::texture1d(sGliTexture);
        gli::texture1d_array*    p1DArrTex   = nullptr;
        gli::texture2d*          p2DTex      = nullptr; // new gli::texture2d(sGliTexture);
        gli::texture2d_array*    p2DArrTex   = nullptr;
        gli::texture3d*          p3DTex      = nullptr;
        gli::texture_cube*       pCubeTex    = nullptr;
        gli::texture_cube_array* pCubeArrTex = nullptr;
        switch (eTexTarget)
        {
        case gli::TARGET_1D:
            sTextureData.TextureType   = TextureDimensionType::Texture1D;
            sTextureData.pGLICommonTex = p1DTex = new gli::texture1d(*sTextureData.pGLITexture);
            uImgWidth                           = p1DTex->extent(nStartUseFileMipsLevel).x;
            break;
        case gli::TARGET_1D_ARRAY:
            sTextureData.TextureType   = TextureDimensionType::Texture1D;
            sTextureData.pGLICommonTex = p1DArrTex = new gli::texture1d_array(*sTextureData.pGLITexture);
            uImgWidth                              = p1DArrTex->extent(nStartUseFileMipsLevel).x;
            bArrayImage                            = true;
            break;
        case gli::TARGET_2D:
            sTextureData.TextureType   = TextureDimensionType::Texture2D;
            sTextureData.pGLICommonTex = p2DTex = new gli::texture2d(*sTextureData.pGLITexture);
            uImgWidth                           = p2DTex->extent(nStartUseFileMipsLevel).x;
            uImgHeight                          = p2DTex->extent(nStartUseFileMipsLevel).y;
            break;
        case gli::TARGET_2D_ARRAY:
            sTextureData.TextureType   = TextureDimensionType::Texture2D;
            sTextureData.pGLICommonTex = p2DArrTex = new gli::texture2d_array(*sTextureData.pGLITexture);
            uImgWidth                              = p2DArrTex->extent(nStartUseFileMipsLevel).x;
            uImgHeight                             = p2DArrTex->extent(nStartUseFileMipsLevel).y;
            bArrayImage                            = true;
            break;
        case gli::TARGET_3D:
            sTextureData.TextureType   = TextureDimensionType::Texture3D;
            sTextureData.pGLICommonTex = p3DTex = new gli::texture3d(*sTextureData.pGLITexture);
            uImgWidth                           = p3DTex->extent(nStartUseFileMipsLevel).x;
            uImgHeight                          = p3DTex->extent(nStartUseFileMipsLevel).y;
            uImgDepth                           = p3DTex->extent(nStartUseFileMipsLevel).z;
            break;
        case gli::TARGET_CUBE:
            sTextureData.TextureType   = TextureDimensionType::TextureCube;
            sTextureData.pGLICommonTex = pCubeTex = new gli::texture_cube(*sTextureData.pGLITexture);
            uImgWidth                             = pCubeTex->extent(nStartUseFileMipsLevel).x;
            uImgHeight                            = pCubeTex->extent(nStartUseFileMipsLevel).y;
            break;
        case gli::TARGET_CUBE_ARRAY:
            sTextureData.TextureType   = TextureDimensionType::TextureCube;
            sTextureData.pGLICommonTex = pCubeArrTex = new gli::texture_cube_array(*sTextureData.pGLITexture);
            uImgWidth                                = pCubeTex->extent(nStartUseFileMipsLevel).x;
            uImgHeight                               = pCubeTex->extent(nStartUseFileMipsLevel).y;
            bArrayImage                              = true;
            break;
        default:
            KGLOG_ASSERT_EXIT(FALSE);
            break;
        }

        KGLOG_ASSERT_EXIT(sTextureData.pGLICommonTex);

        if (sTextureData.pGLICommonTex && !sTextureData.pGLICommonTex->empty())
        {
            PROF_CPU("ParseGLITextureAfterLoad_1");
            for (std::size_t stLayer = 0; stLayer < sTextureData.pGLITexture->layers(); ++stLayer)
            {
                for (std::size_t stFace = 0; stFace < sTextureData.pGLITexture->faces(); ++stFace)
                {
                    for (int nMipLvl = nStartUseFileMipsLevel; nMipLvl < nStartUseFileMipsLevel + nUseFileMipsCount; ++nMipLvl)
                        uTextureDataSize += (uint32_t)sTextureData.pGLICommonTex->size(nMipLvl);
                }
            }

            gli::format fmtType = sTextureData.pGLICommonTex->format();
            sTextureData.Format = Util_GetImageFormatFromGLI(fmtType, sTextureData.bSRGB, sTextureData.bHasAlpha);
            if (sTextureData.Format == enumTextureFormat::TEX_FORMAT_NONE)
            {
                KGLogPrintf(KGLOG_ERR, "not support texture gli_format:%d, path:%s", (int)fmtType, m_ustrRealResourceName.Str());
                KG_PROCESS_ERROR(FALSE);
            }

            if (sTextureData.Format == enumTextureFormat::TEX_FORMAT_B8G8R8_UNORM)
            {
                KGLogPrintf(KGLOG_ERR, "%s：is three channel texture witch not supported, please fix resource", m_ustrRealResourceName.Str());
                KG_PROCESS_ERROR(FALSE);
            }

            sTextureData.uWidth          = uImgWidth;
            sTextureData.uHeight         = uImgHeight;
            sTextureData.uDepth          = uImgDepth;
            sTextureData.uBaseMipLevel   = static_cast<uint32_t>(nStartUseFileMipsLevel);
            sTextureData.uMipLevels      = static_cast<uint32_t>(nUseFileMipsCount);
            sTextureData.uMemoryByteSize = static_cast<uint32_t>(uTextureDataSize);

            if (eTexTarget == gli::TARGET_CUBE)
            {
                sTextureData.uArraySize = (uint32_t)sTextureData.pGLICommonTex->faces();
                ASSERT(sTextureData.uArraySize == 6);
            }
            else
            {
                sTextureData.uArraySize = (uint32_t)sTextureData.pGLICommonTex->layers();
                if (!bArrayImage)
                    ASSERT(sTextureData.uArraySize == 1);
            }

            {
                PROF_CPU("ParseGLITextureAfterLoad_2");

                KGLOG_ASSERT_EXIT(m_sTextureData.pGLITexture);

                // Setup buffer copy regions for each mip level
                for (std::size_t stLayer = 0; stLayer < m_sTextureData.pGLITexture->layers(); ++stLayer)
                {
                    for (std::size_t stFace = 0; stFace < m_sTextureData.pGLITexture->faces(); ++stFace)
                    {
                        for (std::size_t stLevel = sTextureData.uBaseMipLevel; stLevel < sTextureData.uBaseMipLevel + sTextureData.uMipLevels; ++stLevel)
                        {
                            uint32_t uLayerWidth  = 0;
                            uint32_t uLayerHeight = 0;
                            uint32_t uLayerDepth  = 0;

                            KGfxSubResourceData sSubResourceData = {};
                            sSubResourceData.pMemData            = m_sTextureData.pGLICommonTex->data(stLayer, stFace, stLevel);

                            m_sTextureData.vecSubResourceData.emplace_back(sSubResourceData);
                        }
                    }
                }
            }
        }

        bResult = TRUE;
    Exit0:
        return bResult;
    }


    void KGFX_FileTexture::SetForceTextureArray(BOOL bForceTextureArray)
    {
        m_bForceTextureArray = bForceTextureArray;
    }

    BOOL KGFX_FileTexture::IsForceTextureArray()
    {
        return m_bForceTextureArray;
    }

    BOOL KGFX_FileTexture::_ParseGLITextureInPostLoad()
    {
        PROF_CPU();

        if (strstr(m_ustrRealResourceName.Str(), "/texturearray/") || strstr(m_ustrRealResourceName.Str(), "/textarrayreuse/"))
        {
            m_bForceTextureArray = true;
        }


        ASSERT(IsMainThread());

        ASSERT(m_sTextureData.pGLITexture && !m_sTextureData.pGLITexture->empty());
        if (m_sTextureData.pGLITexture->empty())
            return FALSE;

        BOOL bResult  = FALSE;
        bool bRetCode = FALSE;

        auto pGraphicDevice = gfx::KGFX_GetGraphicDevice();
        CHECK_ASSERT(pGraphicDevice);

        SAFE_RELEASE(m_sTextureData.pTextureResource);
        SAFE_RELEASE(m_sTextureData.pTextureView);

        KGFX_TextureDesc texDesc;
        texDesc.eDimension  = m_sTextureData.TextureType;
        texDesc.memoryType  = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        texDesc.uWidth      = m_sTextureData.uWidth;
        texDesc.uHeight     = m_sTextureData.uHeight;
        texDesc.uDepth      = m_sTextureData.uDepth;
        texDesc.uArraySize  = m_sTextureData.uArraySize;
        texDesc.uMipLevels  = m_sTextureData.uMipLevels;
        texDesc.eFormat     = m_sTextureData.Format;
        texDesc.bSRGB       = m_sTextureData.bSRGB;
        texDesc.uUsageFlags = TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT;

        KGFX_TextureViewDesc srvDesc;
        srvDesc.eFormat                       = texDesc.eFormat;
        srvDesc.eViewType                     = KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;
        srvDesc.eViewDimension                = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
        srvDesc.uAspectFlags                  = TextureAspectFlagBits::TEXTURE_ASPECT_COLOR_BIT;
        srvDesc.sSubresourceRange.uBaseArraySlice = 0;
        srvDesc.sSubresourceRange.uArrayCount = texDesc.uArraySize;
        srvDesc.sSubresourceRange.uBaseMipLevel   = 0;
        srvDesc.sSubresourceRange.uMipCount   = texDesc.uMipLevels;

        switch (m_sTextureData.TextureType)
        {
        case TextureDimensionType::Texture1D:
            srvDesc.eViewDimension = texDesc.uArraySize == 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY;
            break;
        case TextureDimensionType::Texture2D:
            srvDesc.eViewDimension = texDesc.uArraySize == 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY;
            ;
            break;
        case TextureDimensionType::TextureCube:
            CHECK_ASSERT(texDesc.uArraySize >= 6);
            srvDesc.eViewDimension = texDesc.uArraySize == 6 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY;
            break;
        case TextureDimensionType::Texture3D:
            srvDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
            break;
        }

        bRetCode = pGraphicDevice->CreateTexture(texDesc, m_sTextureData.m_ustrTextureName.Str(), &m_sTextureData.pTextureResource);
        KGLOG_ASSERT_EXIT(bRetCode);

        bRetCode = pGraphicDevice->CreateTextureView(m_sTextureData.pTextureResource, srvDesc, &m_sTextureData.pTextureView);
        KGLOG_ASSERT_EXIT(bRetCode);

        {
            auto pRenderCtx = gfx::GetRenderContext();
            CHECK_ASSERT(pRenderCtx);

            pRenderCtx->CmdUpdateAllResource(m_sTextureData.pTextureResource, m_sTextureData.vecSubResourceData);
        }

        bResult = TRUE;
    Exit0:
        SAFE_DELETE(m_sTextureData.pGLICommonTex);
        m_sTextureData.vecSubResourceData.clear();

        return bResult;
    }

    BOOL KGFX_FileTexture::HasAlphaChannel()
    {
        return m_sTextureData.bHasAlpha;
    }

    uint32_t KGFX_FileTexture::GetFormat()
    {
        return m_sTextureData.Format;
    }

    uint32_t KGFX_FileTexture::GetWidth() const
    {
        return m_sTextureData.uWidth;
    }

    uint32_t KGFX_FileTexture::GetHeight() const
    {
        return m_sTextureData.uHeight;
    }

    KUniqueStr KGFX_FileTexture::GetRealResourceName() const
    {
        return m_ustrRealResourceName;
    }
} // namespace gfx

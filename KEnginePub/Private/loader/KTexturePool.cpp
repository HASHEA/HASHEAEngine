#include "gli/gli.hpp"
#include "Common/KG_Memory.h"
#include "../stdafx.h"
#include <sstream>

#include "KTexturePool.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/time/KTimer.h"
#include "KBase/Public/io/KFile.h"
#include "KBase/Public/thread/KLock.h"
#include "KBase/Public/thread/KThread.h"
#include "KBase/Public/async_task/KAsyncTaskManager.h"
#include "KTextureRaw.h"
#include "KTextureMask.h"
#include "KEngine/Public/KEngineCore.h"
#include "KEngine/Private/resource/KResourcePool.h"
#include "KGFX_MergedTexture2DArray.h"
#include "KGFX_FileTexture.h"
#include "KGFX_MemTexture.h"
#include "KBase/Public/io/IFile.h"

//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/KProfileTools.h"

using namespace std;

#define EVN_SPECULAR_CUBEMAP_TEXTURE "data/public/evn.ktx"
#define TEXTURE_USE_GLI              1

KTexturePool::KTexturePool()
{
    BOOL bRetCode = FALSE;

    // m_TextureLoadThread.Init();
    m_pResoureceGC = new KResourceGC;
    // m_pEvnMap = RequestTexture(EVN_SPECULAR_CUBEMAP_TEXTURE, TRUE, TRUE, TextureType::Cubemap);

    m_pEvnMap           = nullptr;
    m_bHighLoadPriority = false;
}

KTexturePool::~KTexturePool()
{
    if (m_pBlankTexture)
    {
        m_pBlankTexture->OnRelease();
    }
    SAFE_DELETE(m_pBlankTexture);
    if (m_pBlackTexture)
    {
        m_pBlackTexture->OnRelease();
    }
    SAFE_DELETE(m_pBlackTexture);
    SAFE_DELETE(m_pDefaultShadowRenderTexture);
    if (m_pErrorTexture)
    {
        m_pErrorTexture->OnRelease();
    }
    SAFE_DELETE(m_pErrorTexture);
    if (m_pErrorTextureArray)
    {
        m_pErrorTextureArray->OnRelease();
    }
    SAFE_DELETE(m_pErrorTextureArray);
    if (m_pErrorTextureCube)
    {
        m_pErrorTextureCube->OnRelease();
    }
    SAFE_DELETE(m_pErrorTextureCube);
    if (m_pDefaultNormalTexture)
    {
        m_pDefaultNormalTexture->OnRelease();
    }
    SAFE_DELETE(m_pDefaultNormalTexture);

    SAFE_RELEASE(m_pEvnMap);
    m_mtxTexture.lock();
    if (!m_mapTexture.empty())
    {
        char errorMessage[128];
        snprintf(errorMessage, 128, "detect %d TextureResource not clean up", (uint32_t)m_mapTexture.size());
        KGLogPrintf(KGLOG_WARNING, "%s", errorMessage);
        if ((uint32_t)m_mapTexture.size() > 10)
        {
            ASSERT(errorMessage);
        }
        for (auto it = m_mapTexture.begin(), e = m_mapTexture.end(); it != e; ++it)
        {
            IKTexture* pTexture = it->second;
            KGLogPrintf(KGLOG_ERR, "texture %s 没正确释放", pTexture->GetResourceName());
            pTexture->OnRelease();
            SAFE_DELETE(pTexture);
        }
        m_mapTexture.clear();
    }
    m_mtxTexture.unlock();

    SAFE_DELETE(m_pResoureceGC);
}

int KTexturePool::GetTextureSize()
{
    return (int)m_mapTexture.size();
}

void KTexturePool::TakeResourceSnap(IFile* piFile)
{
    if (piFile)
    {
        std::string fResSize;
        std::string uResWidth;
        std::string uResHeight;
        std::string uFormat;
        std::string uRefCount;
        KUniqueStr  ustrResourceName;
        std::string strHead = "Texture Name\tWidth\tHeight\tFormat\tRefCount\tSize(KB)\n";
        piFile->Write(strHead.c_str(), (unsigned int)strHead.size());
        for (const auto& it : m_mapTexture)
        {
            ustrResourceName = it.second->GetRealResourceName();
            fResSize         = std::to_string((float)it.second->GetResourceSize() / 1024.0f);
            uResWidth        = std::to_string(it.second->GetWidth());
            uResHeight       = std::to_string(it.second->GetHeight());
            uFormat          = std::to_string(it.second->GetFormat());
            uRefCount        = std::to_string(it.second->GetRef());
            if (ustrResourceName.IsValid())
            {
                piFile->Write(ustrResourceName.Str(), (unsigned int)strlen(ustrResourceName.Str()));
            }
            else
            {
                piFile->Write(it.first.Str(), (unsigned int)strlen(it.first.Str()));
            }
            piFile->Write("\t", 1);
            piFile->Write(uResWidth.c_str(), (unsigned int)uResWidth.size());
            piFile->Write("\t", 1);
            piFile->Write(uResHeight.c_str(), (unsigned int)uResHeight.size());
            piFile->Write("\t", 1);
            piFile->Write(uFormat.c_str(), (unsigned int)uFormat.size());
            piFile->Write("\t", 1);
            piFile->Write(uRefCount.c_str(), (unsigned int)uRefCount.size());
            piFile->Write("\t", 1);
            piFile->Write(fResSize.c_str(), (unsigned int)fResSize.size());
            piFile->Write("\n", 1);
        }
    }
}

gfx::KGfxFileTexture* KTexturePool::GetBlankTexture()
{
    PROF_CPU();
    if (!m_pBlankTexture)
    {
        m_pBlankTexture = new gfx::KGFX_FileTexture();
        m_pBlankTexture->SetIsProcedural(TRUE);
        // 这个是特殊贴图，自己加载，不受texturepool管理
        if (m_pBlankTexture->LoadFromFile(BLANK_TEX_PATH, 0, false, false))
        {
            m_pBlankTexture->PostLoad();
        }
    }
    return m_pBlankTexture;
}

gfx::KGfxFileTexture* KTexturePool::GetBlackTexture()
{
    PROF_CPU_DEEP();
    if (!m_pBlackTexture)
    {
        m_pBlackTexture = new gfx::KGFX_FileTexture();
        m_pBlackTexture->SetIsProcedural(TRUE);
        // 这个是特殊贴图，自己加载，不受texturepool管理
        if (m_pBlackTexture->LoadFromFile(BLACK_TEX_PATH, 0, false, false))
        {
            m_pBlackTexture->PostLoad();
        }
    }
    return m_pBlackTexture;
}

gfx::KGfxFileTexture* KTexturePool::GetDefaultNormalTexture()
{
    PROF_CPU_DEEP();
    if (!m_pDefaultNormalTexture)
    {
        m_pDefaultNormalTexture = new gfx::KGFX_FileTexture();
        m_pDefaultNormalTexture->SetIsProcedural(TRUE);
        // 这个是特殊贴图，自己加载，不受texturepool管理
        if (m_pDefaultNormalTexture->LoadFromFile(DEFAULT_NORMALTEX_PATH, 0, false, false))
        {
            m_pDefaultNormalTexture->PostLoad();
        }
    }
    return m_pDefaultNormalTexture;
}

gfx::KGfxFileTexture* KTexturePool::GetErrorTexture()
{
    PROF_CPU_DEEP();
    if (!m_pErrorTexture)
    {
        m_pErrorTexture = new gfx::KGFX_FileTexture();
        m_pErrorTexture->SetIsProcedural(TRUE);
        // 这个是特殊贴图，自己加载，不受texturepool管理
        if (m_pErrorTexture->LoadFromFile(ERROR_TEX_PATH, 0, false, false))
        {
            m_pErrorTexture->PostLoad();
        }
    }
    return m_pErrorTexture;
}

gfx::KGfxFileTexture* KTexturePool::GetErrorTextureArray()
{
    PROF_CPU_DEEP();
    if (!m_pErrorTextureArray)
    {
        m_pErrorTextureArray = new gfx::KGFX_FileTexture();
        m_pErrorTextureArray->SetIsProcedural(TRUE);
        // 这个是特殊贴图，自己加载，不受texturepool管理
        if (m_pErrorTextureArray->LoadFromFile(ERROR_TEX_ARRAY_PATH, 0, false, false))
        {
            m_pErrorTextureArray->PostLoad();
        }
    }
    return m_pErrorTextureArray;
}

gfx::KGfxFileTexture* KTexturePool::GetErrorTextureCube()
{
    PROF_CPU_DEEP();
    if (!m_pErrorTextureCube)
    {
        m_pErrorTextureCube = new gfx::KGFX_FileTexture();
        m_pErrorTextureCube->SetIsProcedural(TRUE);
        // 这个是特殊贴图，自己加载，不受texturepool管理
        if (m_pErrorTextureCube->LoadFromFile(ERROR_TEX_CUBE_PATH, 0, false, false))
        {
            m_pErrorTextureCube->PostLoad();
        }
    }
    return m_pErrorTextureCube;
}

gfx::KRenderTarget* KTexturePool::GetDefaultShadowTex()
{
    if (m_pDefaultShadowRenderTexture == nullptr)
    {
        gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();
        CHECK_ASSERT(pKGFXGraphicDevice);

        gfx::KRenderTargetDesc rendertargetDesc = {};
        rendertargetDesc.uWidth = 4;
        rendertargetDesc.uHeight = 4;
        rendertargetDesc.uDepth = 1;
        rendertargetDesc.eFormat = DEFAULT_SHADOWMAP_FMT;
        rendertargetDesc.uMipLevels = 1;
        rendertargetDesc.uArraySize = 1;
        rendertargetDesc.eSampleCount = gfx::SAMPLE_COUNT_1_BIT;
        sprintf(rendertargetDesc.m_szRTName, "Default_DepthStencil_RT");
        BOOL bRetCode = pKGFXGraphicDevice->CreateRenderTarget(&m_pDefaultShadowRenderTexture, &rendertargetDesc, true, nullptr);
        KGLOG_ASSERT_EXIT(bRetCode);
        KGLOG_ASSERT_EXIT(m_pDefaultShadowRenderTexture);
        std::string name = "DefaultShadowMapRenderTex";
        m_pDefaultShadowRenderTexture->SetObjectName(name.c_str());
    }
Exit0:
    return m_pDefaultShadowRenderTexture;
}

IKTexture* KTexturePool::GetTextureByResPath(const char* pcszFilePath)
{
    KUniqueStr ustrResPath;

    KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
    ustrResPath = g_CachePathString(pcszFilePath, TRUE);
    KGLOG_ASSERT_EXIT(ustrResPath.IsValid());

    return _GetTextureByResPath(ustrResPath);
Exit0:
    return nullptr;
}

IKTexture* KTexturePool::_GetTextureByResPath(KUniqueStr ustrResPath, bool bNeedLock /* = true*/)
{
    IKTexture* pTex = nullptr;

    if (bNeedLock)
        m_mtxTexture.lock();

    const auto& it = m_mapTexture.find(ustrResPath);
    if (it != m_mapTexture.end())
    {
        pTex = it->second;
        pTex->AddRef();
    }

    if (bNeedLock)
        m_mtxTexture.unlock();

    return pTex;
}

IKTexture* KTexturePool::RequestEmptyTexture()
{
    IKTexture* pRet = new gfx::KGFX_MemTexture();
    return pRet;
}


IKTexture* KTexturePool::RequestEmptyFileTexture()
{
    IKTexture* pRet = new gfx::KGFX_FileTexture();
    return pRet;
}

BOOL KTexturePool::RemoveTexture(IKTexture* pTexture)
{
    // 线程1停在这里，线程2 从m_mapTexture中取走，然后Release从m_mapTexture中删除,线程1唤醒执行m_mapTexture就没有记录了
    // m_pResoureceGC->DelayDelete(pTexture); 也包在锁里，防止pTextture脱离m_mapTexture但是还没进入GC列表时，有新的线程request导致创建同路径的新贴图对象
    std::lock_guard<decltype(m_mtxTexture)> _lock(m_mtxTexture);
    if (pTexture->GetRef() > 0)
        return TRUE;

    if (pTexture->IsManagedTexture())
    {
        KUniqueStr ustrResPath = pTexture->GetResourceName();
        ASSERT(ustrResPath.IsValid());
        size_t stEraseCount = m_mapTexture.erase(ustrResPath);
        if (stEraseCount != 1)
        {
            KGLogPrintf(KGLOG_ERR, "remove texture %s error", ustrResPath.Str());
        }
        pTexture->SetIsManagedTexture(FALSE);
    }
    m_pResoureceGC->DelayDelete(pTexture);
    return TRUE;
}

IKTexture* KTexturePool::GetEvnMap()
{
    if (!m_pEvnMap)
    {
        m_pEvnMap = RequestTexture(EVN_SPECULAR_CUBEMAP_TEXTURE, TextureType::Cubemap);
    }
    return m_pEvnMap;
}

void KTexturePool::FrameMove(float fDeltaTime)
{
    PROF_CPU();
    m_pResoureceGC->GC(fDeltaTime);
}

gfx::KTextureMask* KTexturePool::CreateTextureMask()
{
    return new gfx::KTextureMask;
}

void KTexturePool::SetToHighLoadPriority(BOOL bHigh)
{
    m_bHighLoadPriority = bHigh;
}

IKTexture* KTexturePool::_RequestTextureImp(const char* szFileNames[], uint32_t uPathNameCount, TextureType eTextureType, uint32_t dwOption, uint32_t uOwnerModelFlags, BOOL bHighLoadPriority, BOOL bForceTextureArray)
{
    IKTexture*            pTex        = nullptr;
    gfx::KGfxFileTexture* pNewTexture = nullptr;

    BOOL                                                             bNewCreate  = FALSE;
    BOOL                                                             bThreadLoad = FALSE;
    BOOL                                                             bLoaded     = TRUE;
    KUniqueStr                                                       ustrResPath;
    decltype(m_mapTexture.insert(std::make_pair(ustrResPath, pTex))) pairInsert;
    uint64_t                                                         uHash = 0;
    KGLOG_PROCESS_ERROR(szFileNames && szFileNames[0] && szFileNames[0][0]);

    if (eTextureType == TextureType::MergedTexture2DArray)
    {
        std::string key;
        for (uint32_t i = 0; i < uPathNameCount; ++i)
        {
            key.append(szFileNames[i]);
            if (i < uPathNameCount - 1)
            {
                key.append(",");
            }
        }
        uHash                               = KSTR_HELPER::GetHashCodeForString64Bit(key.c_str());
        char szName[NSEngine::MAX_PATH_LEN] = "";
        snprintf(szName, countof(szName), "KGFX_MergedTexture2DArray[%llu]", uHash);
        ustrResPath = g_CachePathString(szName, TRUE);
    }
    else
    {
        ustrResPath = g_CachePathString(szFileNames[0], TRUE);
    }
    KGLOG_ASSERT_EXIT(ustrResPath.IsValid());

    pTex = _GetTextureByResPath(ustrResPath);
    KG_PROCESS_SUCCESS(pTex);

    // 这个范围内要锁，以支持多线程调用
    {
        std::lock_guard<std::mutex> _lock(m_mtxTexture);

        // 尝试从延迟删除列表中获取
        pTex = (IKTexture*)m_pResoureceGC->TryGetOut(ustrResPath);
        if (pTex)
        {
            pairInsert = m_mapTexture.insert(std::make_pair(ustrResPath, pTex));
            ASSERT(pairInsert.second);
            pTex->SetIsManagedTexture(TRUE);
            KG_PROCESS_SUCCESS(pTex);
        }

        // 再次尝试从已有列表中获取
        pTex = _GetTextureByResPath(ustrResPath, false);
        KG_PROCESS_SUCCESS(pTex);

        bNewCreate = TRUE;
        if (eTextureType == TextureType::MergedTexture2DArray)
        {
            pNewTexture = new gfx::KGFX_MergedTexture2DArray();
            ((gfx::KGFX_MergedTexture2DArray*)pNewTexture)->SetResourceName(ustrResPath);
            ((gfx::KGFX_MergedTexture2DArray*)pNewTexture)->SetNameHash(uHash);
        }
        else
        {
            pNewTexture = new gfx::KGFX_FileTexture();
            if (bForceTextureArray)
            {
                ((gfx::KGFX_FileTexture*)pNewTexture)->SetForceTextureArray(true);
            }
        }
        pairInsert = m_mapTexture.insert(std::make_pair(ustrResPath, pNewTexture));
        ASSERT(pairInsert.second);
        pNewTexture->SetIsManagedTexture(TRUE);
    }

    pNewTexture->SetRequestTextureType(eTextureType);

    bThreadLoad = ENABLE_RESOURCE_MULTITHREAD_LOAD && (dwOption & RESOURCE_LOAD_MULTITHREAD);
    if (eTextureType == TextureType::MergedTexture2DArray)
    {
        bLoaded = ((gfx::KGFX_MergedTexture2DArray*)pNewTexture)->LoadFromFiles(szFileNames, uPathNameCount, dwOption, uOwnerModelFlags, bHighLoadPriority);
    }
    else
    {
        bLoaded = pNewTexture->LoadFromFile(ustrResPath.Str(), dwOption, uOwnerModelFlags, bHighLoadPriority);
    }
    if (!bThreadLoad && !bLoaded)
    {
        SAFE_RELEASE(pNewTexture);
    }

    pTex = pNewTexture;
Exit1:
    // 不存在的贴图就不要再给出去了
    if (pTex && pTex->IsLoadFailed())
    {
        SAFE_RELEASE(pTex);
    }
    if (pTex && !bNewCreate)
    {
        pTex->AddOwnerModelFlags(uOwnerModelFlags);
    }
    if (bNewCreate && pTex)
    {
        // 性能监控
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        pPerfMonitor->m_sResource.Add(pTex);
    }
Exit0:
    return pTex;
}

IKTexture* KTexturePool::_RequestTextureImp(
    const KUniqueStr& ustrPackName, const KUniqueStr& ustrSubTexFileName,
    TextureType eTextureType, BOOL bThreadLoad,
    uint32_t uOwnerModelFlags /*= 0*/, BOOL bHighLoadPriority /*= false*/,
    BOOL bForceTextureArray /*= false */
)
{
    OPTICK_EVENT();

    BOOL                  bRetCode    = FALSE;
    IKTexture*            pTex        = nullptr;
    gfx::KGfxFileTexture* pNewTexture = nullptr;

    BOOL                                                             bNewCreate = FALSE;
    KUniqueStr                                                       ustrResPath;
    decltype(m_mapTexture.insert(std::make_pair(ustrResPath, pTex))) pairInsert;
    uint64_t                                                         uHash = 0;

    KGLOG_PROCESS_ERROR(ustrPackName.IsValid() && ustrSubTexFileName.IsValid());
    KGLOG_ASSERT_EXIT(eTextureType != TextureType::MergedTexture2DArray);

    {
        char szFileName[MAX_PATH] = "";
        snprintf(szFileName, countof(szFileName), "%s%s%s", ustrPackName.Str(), NSKBase::tagFileLocation::GetPackNameAndSubFileSeparatorStr(), ustrSubTexFileName.Str());
        szFileName[countof(szFileName) - 1] = 0;
        ustrResPath                         = g_CachePathString(szFileName, TRUE);
    }
    KGLOG_ASSERT_EXIT(ustrResPath.IsValid());

    pTex = _GetTextureByResPath(ustrResPath);
    KG_PROCESS_SUCCESS(pTex);

    // 这个范围内要锁，以支持多线程调用
    {
        std::lock_guard<std::mutex> _lock(m_mtxTexture);

        // 尝试从延迟删除列表中获取
        pTex = (IKTexture*)m_pResoureceGC->TryGetOut(ustrResPath);
        if (pTex)
        {
            pairInsert = m_mapTexture.insert(std::make_pair(ustrResPath, pTex));
            ASSERT(pairInsert.second);
            pTex->SetIsManagedTexture(TRUE);
            KG_PROCESS_SUCCESS(pTex);
        }

        // 再次尝试从已有列表中获取
        pTex = _GetTextureByResPath(ustrResPath, false);
        KG_PROCESS_SUCCESS(pTex);

        bNewCreate  = TRUE;
        pNewTexture = new gfx::KGFX_FileTexture();
        if (bForceTextureArray)
        {
            ((gfx::KGFX_FileTexture*)pNewTexture)->SetForceTextureArray(true);
        }

        pairInsert = m_mapTexture.insert(std::make_pair(ustrResPath, pNewTexture));
        ASSERT(pairInsert.second);
        pNewTexture->SetIsManagedTexture(TRUE);
    }

    pNewTexture->SetRequestTextureType(eTextureType);

    bRetCode = pNewTexture->LoadFromTexsetPack(ustrPackName, ustrSubTexFileName, bThreadLoad ? RESOURCE_LOAD_MULTITHREAD : 0, uOwnerModelFlags, bHighLoadPriority);
    if (!bRetCode)
    {
        SAFE_RELEASE(pNewTexture);
    }

    pTex = pNewTexture;
Exit1:
    // 不存在的贴图就不要再给出去了
    if (pTex && pTex->IsLoadFailed())
    {
        SAFE_RELEASE(pTex);
    }
    if (pTex && !bNewCreate)
    {
        pTex->AddOwnerModelFlags(uOwnerModelFlags);
    }
    if (bNewCreate && pTex)
    {
        // 性能监控
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        pPerfMonitor->m_sResource.Add(pTex);
    }
Exit0:
    return pTex;
}

BOOL KTexturePool::TextureResourceAdjust(KGIndexPkgFile* pIndexPkgFile, const char* pcszResName, char (&szResult)[MAX_PATH])
{
    if (!pcszResName || !pcszResName[0])
        return FALSE;

    BOOL bResult                = FALSE;
    BOOL bRetCode               = FALSE;
    char szDDSPath[MAX_PATH]    = "";
    char szKTXPath[MAX_PATH]    = "";
    char szAdjustPath[MAX_PATH] = "";

    std::vector<const char*> vecPriorityPath;

    IKGFileLibrary* pFileLibrary = GetFileLibraryInterface();
    KGLOG_ASSERT_EXIT(pFileLibrary);

#if DDS_HARDWARE
    g_StrCpyLen(szDDSPath, pcszResName, countof(szDDSPath));
    KSTR_HELPER::ChangeExtName(szDDSPath, ".dds");

    vecPriorityPath.emplace_back(szDDSPath);
    vecPriorityPath.emplace_back(pcszResName);
#else
    g_StrCpyLen(szKTXPath, pcszResName, countof(szKTXPath));
    KSTR_HELPER::ChangeExtName(szKTXPath, ".ktx");

    vecPriorityPath.emplace_back(szKTXPath);
    vecPriorityPath.emplace_back(pcszResName);
#endif

    for (auto pcszPath : vecPriorityPath)
    {
        if (!pIndexPkgFile)
        {
            bRetCode = KGFExist(pcszPath);
            if (!bRetCode)
                continue;
        }
        else
        {
            bRetCode = pFileLibrary->IndexPkgSubFileExist(pIndexPkgFile, pcszPath);
            if (!bRetCode)
                continue;
        }

        g_StrCpyLen(szResult, pcszPath, countof(szResult));
        bResult = TRUE;
        break;
    }

Exit0:
    return bResult;
}

BOOL KTexturePool::TextureResourceAdjust(const NSKBase::tagFileLocation& sInFileLoc)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;

    char szAdjustPath[MAX_PATH] = "";
    NSKBase::tagFileLocation sDDSFileLoc = sInFileLoc;
    NSKBase::tagFileLocation sKTXFileLoc = sInFileLoc;

    std::vector<NSKBase::tagFileLocation> vecPriorityPath;

    IKGFileLibrary* pFileLibrary = GetFileLibraryInterface();
    KGLOG_ASSERT_EXIT(pFileLibrary);

#if DDS_HARDWARE
    sDDSFileLoc.ChangeFileExt(".dds");

    vecPriorityPath.emplace_back(sDDSFileLoc);
    vecPriorityPath.emplace_back(sInFileLoc);
#else
    sDDSFileLoc.ChangeFileExt(".ktx");

    vecPriorityPath.emplace_back(sDDSFileLoc);
    vecPriorityPath.emplace_back(sInFileLoc);
#endif

    for (NSKBase::tagFileLocation& sFileLoc : vecPriorityPath)
    {
        if (!sFileLoc.FileExist())
        {
            continue;
        }

        bResult = TRUE;
        break;
    }

Exit0:
    return bResult;
}

BOOL KTexturePool::TextureFileExist(const char* pcszResName)
{
    BOOL bResult                 = FALSE;
    BOOL bRetCode                = FALSE;
    char szRealResName[MAX_PATH] = "";

    KG_PROCESS_ERROR(pcszResName && pcszResName[0]);

    bResult = TextureResourceAdjust(nullptr, pcszResName, szRealResName);
Exit0:
    return bResult;
}

BOOL KTexturePool::TextureFileExist(KGIndexPkgFile* pIndexPkgFile, const char* pcszResName)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    char szRealResName[MAX_PATH] = "";

    KG_PROCESS_ERROR(pcszResName && pcszResName[0]);

    bResult = TextureResourceAdjust(pIndexPkgFile, pcszResName, szRealResName);
Exit0:
    return bResult;
}

BOOL KTexturePool::TextureFileExist(const NSKBase::tagFileLocation& sFileLoc)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    char szRealResName[MAX_PATH] = "";

    KG_PROCESS_ERROR(sFileLoc.IsValid());

    bResult = TextureResourceAdjust(sFileLoc);
Exit0:
    return bResult;
}

BOOL KTexturePool::CreateMemTexture(
    gfx::KGfxMemTexture** ppRetMemoryTex,
    const uint32_t uWidth,
    const uint32_t uHeight,
    gfx::enumTextureFormat eTargetFormat,
    const void* cpInitBytes,
    uint32_t uBytes,
    BOOL bTile,
    BOOL bSupportSample,
    BOOL bSupportStorage
)
{
    PROF_CPU();
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    gfx::KGfxMemTexture* pMemTexture = nullptr;

    CHECK_ASSERT(ppRetMemoryTex);
    *ppRetMemoryTex = nullptr;

    pMemTexture = new gfx::KGFX_MemTexture;
    CHECK_ASSERT(pMemTexture);

    bRetCode = pMemTexture->Create(uWidth, uHeight, eTargetFormat, cpInitBytes, uBytes, bTile, bSupportSample, bSupportStorage);
    KGLOG_ASSERT_EXIT(bRetCode);

    *ppRetMemoryTex = pMemTexture;
    pMemTexture = nullptr;

    bResult = TRUE;
Exit0:
    if (!bResult)
    {
        SAFE_RELEASE(pMemTexture);
    }
    return bResult;
}

BOOL KTexturePool::CreateMemTexture(
    gfx::KGfxMemTexture** ppRetMemoryTex,
    const uint32_t uWidth,
    const uint32_t uHeight,
    const uint32_t uDepth,
    gfx::enumTextureFormat eTargetFormat,
    const void* cpInitBytes,
    uint32_t uBytes,
    BOOL bTile,
    BOOL bSupportSample,
    BOOL bSupportStorage
)
{
    PROF_CPU();
    BOOL bResult = false;
    BOOL bRetCode = FALSE;
    gfx::KGfxMemTexture* pMemTexture = nullptr;

    CHECK_ASSERT(ppRetMemoryTex);
    *ppRetMemoryTex = nullptr;

    pMemTexture = new gfx::KGFX_MemTexture;
    CHECK_ASSERT(pMemTexture);

    bRetCode = pMemTexture->Create(uWidth, uHeight, uDepth, eTargetFormat, cpInitBytes, uBytes, bTile, bSupportSample, bSupportStorage);
    KGLOG_ASSERT_EXIT(bRetCode);

    *ppRetMemoryTex = pMemTexture;
    pMemTexture = nullptr;

    bResult = true;
Exit0:
    return bResult;
}

BOOL KTexturePool::CreateMemTexture(
    gfx::KGfxMemTexture** ppRetMemoryTex,
    uint32_t uWidth,
    uint32_t uHeight,
    gfx::enumTextureFormat eTargetFormat,
    gfx::KRenderTarget* pSrcRT
)
{
    BOOL bResult = FALSE;
    BOOL bRetCode = FALSE;
    gfx::KGFX_MemTexture* pMemTexture = nullptr;

    KGLOG_ASSERT_EXIT(ppRetMemoryTex);

    pMemTexture = new gfx::KGFX_MemTexture();
    CHECK_ASSERT(pMemTexture);

    bRetCode = pMemTexture->Create(uWidth, uHeight, eTargetFormat, pSrcRT);
    KGLOG_ASSERT_EXIT(bRetCode);

    *ppRetMemoryTex = pMemTexture;
    pMemTexture = nullptr;

    bResult = TRUE;
Exit0:
    if (!bResult)
    {
        SAFE_RELEASE(pMemTexture);
    }
    return bResult;
}

IKTexture* KTexturePool::RequestTexture(const char* pcszResPath, TextureType eTextureType, uint32_t dwOption, uint32_t uOwnerModelFlags, BOOL bHighLoadPriority, BOOL bForceTextureArray)
{
    if (!pcszResPath || !pcszResPath[0])
        return nullptr;

    return _RequestTextureImp(&pcszResPath, 1, eTextureType, dwOption, uOwnerModelFlags, bHighLoadPriority, bForceTextureArray);
}

IKTexture* KTexturePool::RequestTexture(
    const KUniqueStr& ustrPackName, const KUniqueStr& ustrSubTexFileName,
    TextureType eTextureType /*= TextureType::Texture2D*/, BOOL bThreadLoad /*= FALSE*/,
    uint32_t uOwnerModelFlags /*= 0*/, BOOL bHighLoadPriority /*= false*/,
    BOOL bForceTextureArray /*= false */
)
{
    IKTexture* pTex = nullptr;

    if (!ustrPackName.IsValid() || !ustrSubTexFileName.IsValid())
        return nullptr;

    pTex = _RequestTextureImp(ustrPackName, ustrSubTexFileName, eTextureType, bThreadLoad, uOwnerModelFlags, bHighLoadPriority, false);
    return pTex;
}

IKTexture* KTexturePool::RequestMergedTexture2DArray(const char* pcszPathNames[], uint32_t uPathNameCount, TextureType eTextureType, uint32_t dwOption, uint32_t uOwnerModelFlags, BOOL bHighLoadPriority)
{
    IKTexture* pTex = nullptr;
    if (!pcszPathNames || !pcszPathNames[0])
        return nullptr;

    pTex = _RequestTextureImp(pcszPathNames, uPathNameCount, eTextureType, dwOption, uOwnerModelFlags, bHighLoadPriority, true);
    return pTex;
}

IKTexture* KTexturePool::RequestRawTexture(const char* pcszResName, uint32_t dwOption)
{
    gfx::KTextureRaw*                                                pTex        = nullptr;
    gfx::KTextureRaw*                                                pNewTexture = nullptr;
    BOOL                                                             bLoadFailed = FALSE;
    BOOL                                                             bLoaded     = TRUE;
    KUniqueStr                                                       ustrResPath;
    decltype(m_mapTexture.insert(std::make_pair(ustrResPath, pTex))) pairInsert;

    KGLOG_PROCESS_ERROR(pcszResName && pcszResName[0]);
    ustrResPath = g_CachePathString(pcszResName, TRUE);
    KGLOG_ASSERT_EXIT(ustrResPath.IsValid());

    pTex = (gfx::KTextureRaw*)_GetTextureByResPath(ustrResPath);
    KG_PROCESS_SUCCESS(pTex);

    // 这个范围内要锁，以支持多线程调用
    {
        std::lock_guard<std::mutex> _lock(m_mtxTexture);

        // 尝试从延迟删除列表中获取
        pTex = (gfx::KTextureRaw*)m_pResoureceGC->TryGetOut(ustrResPath);
        if (pTex)
        {
            pairInsert = m_mapTexture.insert(std::make_pair(ustrResPath, pTex));
            ASSERT(pairInsert.second);
            pTex->SetIsManagedTexture(TRUE);
            KG_PROCESS_SUCCESS(pTex);
        }

        // 再次尝试从已有列表中获取
        pTex = (gfx::KTextureRaw*)_GetTextureByResPath(ustrResPath, false);
        KG_PROCESS_SUCCESS(pTex);

        pNewTexture = new gfx::KTextureRaw();
        pairInsert  = m_mapTexture.insert(std::make_pair(ustrResPath, pNewTexture));
        ASSERT(pairInsert.second);
        pNewTexture->SetIsManagedTexture(TRUE);
        pNewTexture->SetResourceName(pcszResName);
        pNewTexture->SetResourceOption(dwOption);
    }

    if (ENABLE_RESOURCE_MULTITHREAD_LOAD && (dwOption & RESOURCE_LOAD_MULTITHREAD))
    {
        bLoaded = pNewTexture->LoadAsync(m_bHighLoadPriority ? NSKBase::EAsyncTaskPriority::High : NSKBase::EAsyncTaskPriority::Normal);
    }
    else
    {
        bLoaded = pNewTexture->LoadSync();
        if (!bLoaded)
            SAFE_RELEASE(pNewTexture);
    }

    pTex = pNewTexture;

Exit1:
    // 错误的贴图就不要再给出去了
    if (pTex && pTex->IsLoadFailed())
    {
        SAFE_RELEASE(pTex);
    }
Exit0:
    return pTex;
}

namespace NSEngine
{
    KTexturePool* pTexturePool = NULL;
    BOOL          CreateTexturePool()
    {
        if (!pTexturePool)
        {
            pTexturePool = new KTexturePool();
        }
        return TRUE;
    }

    BOOL DestroyTexturePool()
    {
        SAFE_DELETE(pTexturePool);
        return TRUE;
    }

    IKTexturePool* GetTexturePool()
    {
        return pTexturePool;
    }
} // namespace NSEngine

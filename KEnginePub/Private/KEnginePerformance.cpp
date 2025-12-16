#include "KEnginePub/Public/IKResource.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "KBase/Public/str/KStrSafe.h"
#include "KBase/Public/time/KTimer.h"
#include "KEngine/Private/resource/KResourcePool.h"
#include "KEnginePub/Private/loader/KTexturePool.h"
#include "KEngine/Public/KEngineCore.h"
#include "Engine/File.h"
#include "Engine/KGCRT.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEngine/Public/KEngine.h"
#include "KEngine/Public/Render/KEngineRender.h"

void KX3DDrawMonitor::TurnOnMainSceneDrawFrameCapture()
{
    m_bWantToCaptureMainSceneDrawFrame = true;
}

bool KX3DDrawMonitor::IsCapturingMainSceneDrawFrame()
{
    return m_bCapturingMainSceneDrawFrame;
}

const char* KX3DDrawMonitor::GetLastMainSceneDrawFrameCaptureLogFilePath()
{
    return m_strLastCaptureLogFilePath.c_str();
}

void KX3DDrawMonitor::AddMainSceneDrawEvent(KUniqueStr ustrMeshPath, KUniqueStr ustrMtlPath, KUniqueStr ustrMtlInstDef, int nInstCount, int nFaces, const char* pcszLauncher)
{
    tagDrawEvent sEvent;
    sEvent.ustrMeshPath   = ustrMeshPath;
    sEvent.ustrMtlPath    = ustrMtlPath;
    sEvent.ustrMtlInstDef = ustrMtlInstDef;
    sEvent.nInstCount     = nInstCount;
    sEvent.nFaces         = nFaces;
    if (pcszLauncher && pcszLauncher[0])
        sEvent.ustrLauncher = g_CachePathString(pcszLauncher, TRUE);

    m_vecMainSceneDrawEvent.emplace_back(sEvent);
}

void KX3DDrawMonitor::OnMainSceneDrawFrameBegin(const char* pcszSceneName)
{
    if (m_bWantToCaptureMainSceneDrawFrame)
    {
        m_bCapturingMainSceneDrawFrame = true;
        if (pcszSceneName && pcszSceneName[0])
            m_ustrMainSceneName = g_CachePathString(pcszSceneName, TRUE);

        m_bWantToCaptureMainSceneDrawFrame = false;
    }
}

void KX3DDrawMonitor::OnMainSceneDrawFrameEnd()
{
    IFile* piFile = nullptr;

    if (m_bCapturingMainSceneDrawFrame)
    { // 写入文件
        char szLogFilePath[MAX_PATH] = "";
        char szWriteLine[1024]       = "";
        int  nCurFrameCount          = NSEngine::GetRenderFrameMoveLoopCount();
        snprintf(szLogFilePath, countof(szLogFilePath) - 1, "DrawFrameCapture/%s_%d.txt", m_ustrMainSceneName.Str(), nCurFrameCount);

        piFile = g_CreateAloneFile(szLogFilePath);
        KGLOG_ASSERT_EXIT(piFile);
        m_strLastCaptureLogFilePath = szLogFilePath;

        snprintf(
            szWriteLine,
            countof(szWriteLine) - 1,
            "%s\r\n",
            "MeshPath\tMtlPath\tMtlInstDef\tInstCount\tFaces\tLauncher"
        );
        szWriteLine[countof(szWriteLine) - 1] = 0;
        piFile->Write(szWriteLine, (unsigned)strlen(szWriteLine));

        for (const tagDrawEvent& sEvent : m_vecMainSceneDrawEvent)
        {
            snprintf(
                szWriteLine,
                countof(szWriteLine) - 1,
                "%s\t%s\t%s\t%d\t%d\t%s\r\n",
                sEvent.ustrMeshPath.Str(),
                sEvent.ustrMtlPath.Str(),
                sEvent.ustrMtlInstDef.Str(),
                sEvent.nInstCount,
                sEvent.nFaces,
                sEvent.ustrLauncher.Str()
            );
            szWriteLine[countof(szWriteLine) - 1] = 0;

            piFile->Write(szWriteLine, (unsigned)strlen(szWriteLine));
        }
    }

Exit0:
    if (piFile)
    {
        SAFE_RELEASE(piFile);
    }
    m_vecMainSceneDrawEvent.clear();
    m_vecMainSceneDrawEvent.shrink_to_fit();
    m_bCapturingMainSceneDrawFrame = false;
}

uint32_t KX3DEngineMonitor::GetTextureNumber()
{
    uint32_t uTextureCount = 0;
    for (int i = KRESOURCETYPE::TEXTURE_TYPE; i <= KRESOURCETYPE::TEXTURE_MASK_TYPE; ++i)
    {
        uTextureCount += m_sResource.GetCountByType(static_cast<KRESOURCETYPE>(i));
    }
    for (int i = KRESOURCETYPE::MEM_TEXTURE_TYPE; i <= KRESOURCETYPE::RENDER_TEXTURE_TYPE; ++i)
    {
        uTextureCount += m_sResource.GetCountByType(static_cast<KRESOURCETYPE>(i));
    }
    return uTextureCount;
}
uint32_t KX3DEngineMonitor::GetMeshNumber()
{
    uint32_t uMeshCount = 0;
    for (int i = KRESOURCETYPE::KMESH_TYPE; i <= KRESOURCETYPE::BOX_MESH_VK_TYPE; ++i)
    {
        uMeshCount += m_sResource.GetCountByType(static_cast<KRESOURCETYPE>(i));
    }
    return uMeshCount;
}

KX3DResourceMonitor::KX3DResourceMonitor()
{
    for (int i = 0; i < KRESOURCETYPE::RESOURCE_COUNT; ++i)
    {
        m_mapType2Count[i] = 0;
    }
}

void KX3DResourceMonitor::Add(IKResource* piResource)
{
#ifndef KG_PUBLISH
    ASSERT(piResource);
    {
        std::lock_guard<std::mutex> _lock(m_mtxResource);
        ASSERT(m_setResource.find(piResource) == m_setResource.end());
        m_setResource.insert(piResource);
    }

    KRESOURCETYPE          eResType      = piResource->GetResourceType();
    std::atomic<uint32_t>& uResTypeCount = m_mapType2Count[(int)eResType];
    ++uResTypeCount;
#endif
}

void KX3DResourceMonitor::Remove(IKResource* piResource)
{
#ifndef KG_PUBLISH
    ASSERT(piResource);
    {
        std::lock_guard<std::mutex> _lock(m_mtxResource);
        if (m_setResource.find(piResource) == m_setResource.end())
            return;
        m_setResource.erase(piResource);
    }

    KRESOURCETYPE          eResType      = piResource->GetResourceType();
    std::atomic<uint32_t>& uResTypeCount = m_mapType2Count[(int)eResType];
    ASSERT(uResTypeCount > 0);
    --uResTypeCount;
#endif
}

// uint32_t KX3DResourceMonitor::GetCountByType(KRESOURCETYPE eResType)
//{
//	std::atomic<uint32_t>& uResTypeCount = m_mapType2Count[(int)eResType];
//	return uResTypeCount;
// }

namespace NSEngine
{
    static KX3DEngineMonitor  s_EngineMonitor;
    static KEnginePerformance s_CurrentFramePerformance;
    static KEnginePerformance s_LastFramePerformance;

    extern KX3DEngineMonitor* GetEngineMonitor()
    {
        return &s_EngineMonitor;
    }

    KEnginePerformance* GetEnginePerformance()
    {
        return &s_CurrentFramePerformance;
    }

    KEnginePerformance* GetLastFramePerformance()
    {
        return &s_LastFramePerformance;
    }

    void ResetEnginePerformance()
    {
        s_LastFramePerformance = s_CurrentFramePerformance;
        s_CurrentFramePerformance.Reset();
    }

    void CollectEnginePerformance()
    {
    }

    BOOL OutputResourePoolInfo()
    {
        BOOL bResult = FALSE;
#ifndef KG_PUBLISH
        KRESOURCETYPE sResourceTypes[] = {
            KMESH_TYPE, MDL_TYPE, CLIP_TYPE,
            MATERIAL_TYPE, SKELETON_TYPE, SOUND_TYPE, SPEEDTREE_TYPE, SRT_TYPE
        };

        std::vector<std::string> strResourceType{
            "KMESH_TYPE\tSize(KB)\n", "MDL_TYPE\tSize(KB)\n", "CLIP_TYPE\tSize(KB)\n",
            "MATERIAL_TYPE\tSize(KB)\n", "SKELETON_TYPE\tSize(KB)\n", "SOUND_TYPE\tSize(KB)\n", "SPEEDTREE_TYPE\tSize(KB)\n", "SRT_TYPE\tSize(KB)\n"
        };

        BOOL bRetCode             = FALSE;
        char szFileName[MAX_PATH] = "";

        IFile*                       piFile = nullptr;
        time_t                       tmtNow = 0;
        struct tm                    tmNow;
        std::vector<OutputDebugInfo> vectFiles;

        size_t siIndex = 0;

        tmtNow = time(NULL);

        localtime_r(&tmtNow, &tmNow);

        KStringCchPrintfA(szFileName, _countof(szFileName), "ResourcePool_%d%02d%02d_%02d%02d%02d.log", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

        piFile = g_OpenFile(szFileName, true, true);
        KG_PROCESS_ERROR(piFile);

        ((KTexturePool*)NSEngine::GetTexturePool())->TakeResourceSnap(piFile);

        for (siIndex = 0; siIndex < _countof(sResourceTypes); ++siIndex)
        {
            bRetCode = NSEngine::GetResourcePool()->GetResourceFileNames(sResourceTypes[siIndex], vectFiles);
            if (bRetCode)
            {
                piFile->Write(strResourceType[siIndex].c_str(), (unsigned int)strResourceType[siIndex].size());
                std::string resSize;
                for (const auto& it : vectFiles)
                {
                    resSize = std::to_string(it.uResMemSize / 1024.0f);
                    piFile->Write(it.strResName.c_str(), static_cast<unsigned int>(it.strResName.size()));
                    piFile->Write("\t", 1);
                    piFile->Write(resSize.c_str(), (unsigned int)resSize.size());
                    piFile->Write("\n", 1);
                }
            }
        }

        bResult = TRUE;
    Exit0:
        if (piFile)
        {
            piFile->Release();
            piFile = nullptr;
        }
#endif // KG_PUBLISH
        return bResult;
    }

    BOOL OutputVulkanMemoryAllocInfo()
    {
        BOOL bResult = FALSE;
#ifndef KG_PUBLISH
        BOOL                bRetCode             = FALSE;
        IFile*              piFile               = nullptr;
        char*               statsString          = nullptr;
        char                szFileName[MAX_PATH] = "";
        time_t              tmtNow               = 0;
        struct tm           tmNow;
        auto pGraphicsDevice = gfx::KGFX_GetGraphicDevice();

        tmtNow = time(NULL);
        localtime_r(&tmtNow, &tmNow);

        KStringCchPrintfA(szFileName, _countof(szFileName), "VulkanMemoryAllocationInfo_%d%02d%02d_%02d%02d%02d.log", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

        piFile = g_OpenFile(szFileName, true, true);
        KG_PROCESS_ERROR(piFile);

        if (pGraphicsDevice)
        {
            pGraphicsDevice->DumpDeviceMemoryInfo(
                [piFile](const char* pszMemoryInfo, uint32_t uMemoryInfoStringLength)
                {
                    piFile->Write(pszMemoryInfo, uMemoryInfoStringLength);
                }
            );
        }
        bResult = TRUE;
    Exit0:
        if (piFile)
        {
            piFile->Release();
            piFile = nullptr;
        }
#endif // KG_PUBLISH
        return bResult;
    }
} // namespace NSEngine

void KX3DVkBufferMonitor::UsageBufferCountInc(int nUsage)
{
    std::lock_guard<decltype(m_mtxBufferUsage2Count)> _lock(m_mtxBufferUsage2Count);
    ++m_mapBufferUsage2Count[nUsage];
}

void KX3DVkBufferMonitor::UsageBufferCountDec(int nUsage)
{
    std::lock_guard<decltype(m_mtxBufferUsage2Count)> _lock(m_mtxBufferUsage2Count);
    --m_mapBufferUsage2Count[nUsage];
}

void KX3DVkImageMonitor::UsageImageCountInc(int nUsage)
{
    std::lock_guard<decltype(m_mtxImageUsage2Count)> _lock(m_mtxImageUsage2Count);
    ++m_mapImageUsage2Count[nUsage];
}

void KX3DVkImageMonitor::UsageImageCountDec(int nUsage)
{
    std::lock_guard<decltype(m_mtxImageUsage2Count)> _lock(m_mtxImageUsage2Count);
    --m_mapImageUsage2Count[nUsage];
}

float KEnginePerformance::GetSpeedTreeRealTime()
{
    return NSEngine::g_sRenderTimer.GetPastTime();
}

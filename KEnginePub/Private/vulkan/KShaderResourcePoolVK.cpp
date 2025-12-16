#include "KShaderResourcePoolVK.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/str/KStrSafe.h"
#include "KBase/Public/io/KFile.h"
#include "Engine/KGLog.h"
#include <fstream>
#include <sstream>
#include "KMaterialSystem/Private/KMaterialSystem.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/KProfileTools.h"

void KShaderResourcePoolVK__ssplit(const char* s, std::vector<std::string>& list1)
{
    std::istringstream tmp_string(s);
    std::string        ss;
    while (getline(tmp_string, ss, ','))
    {
        ss = ss.erase(ss.find_last_not_of("\t\r\n") + 1);
        list1.push_back(ss);
    }
}

KShaderResourcePoolVK::KShaderResourcePoolVK()
{
#ifdef _WIN32
    m_pShaderPreBuildLogFile = fopen("pre_shader.txt", "w");
#endif
}

BOOL KShaderResourcePoolVK::Init()
{
#if 0
	if (DrvOption::GetRenderApi() == GFX_VULKAN_API)
	{
		KGFile* fp = KGFOpen("data/material/shader_mb/pre_shader.txt", "rb");
		if (fp)
		{
			std::vector<std::string> params;
			char                     szLine[NSEngine::MAX_PATH_LEN] = "";
			while (KGFGets(szLine, countof(szLine), fp))
			{
	//#if !defined(_WIN32) && !defined(__MACOS__)
	//			char* p = strstr(szLine, "PLATFORM=1");
	//			if (p)
	//			{
	//				uint32_t offset = strlen("PLATFORM=");
	//				p[offset]       = '2';
	//			}
	//#endif
				params.clear();
				KShaderResourcePoolVK__ssplit(szLine, params);
				if (params.size() == 4)
				{
					BOOL               loading         = false;
					KShaderResourceVK* pShaderResource = RequestShaderResource(params[0].c_str(), params[1].c_str(), params[2].c_str(), params[3].c_str(), loading, KEnumMtlTaskLevel::NORMAL_MTL_THREAD_LEVEL);
					m_vecPreLoadShaderResource.push_back(pShaderResource);
				}
			}
			KGFClose(fp);
		}
	}
#endif
    return true;
}

KShaderResourcePoolVK::~KShaderResourcePoolVK()
{
    for (auto it : m_vecPreLoadShaderResource)
    {
        KShaderResourceVK* pShaderResoure = it;
        SAFE_RELEASE(pShaderResoure);
    }
    m_vecPreLoadShaderResource.clear();

    if (!m_mapShaderResourceVK.empty())
    {
        KGLogPrintf(KGLOG_WARNING, "KShaderResourcePoolVK Detected not destroied resources, force destroy them!");
        if (!m_mapShaderResourceVK.empty())
        {
            m_shaderResourceLock.lock();
            KGLogPrintf(KGLOG_WARNING, "ShaderResource is not proper destroy");
            for (auto it : m_mapShaderResourceVK)
            {
                delete (it.second);
            }
            m_mapShaderResourceVK.clear();
            m_shaderResourceLock.unlock();
        }
    }

#ifdef _WIN32
    if (m_pShaderPreBuildLogFile)
    {
        fclose(m_pShaderPreBuildLogFile);
        m_pShaderPreBuildLogFile = nullptr;
    }
#endif
}

KShaderResourceVK* KShaderResourcePoolVK::RequestShaderResource(
    const char*                                pcszShaderSource,
    const NSKBase::tagFileLocation&            sIncludeShaderLoc,
    const char*                                pcszShaderDef,
    const char*                                pcszMacro,
    BOOL&                                      bLoading,
    KEnumMtlTaskLevel                          uThreadLevel,
    std::map<const_pool_str, gfx::KSamplerState>* mapSamplerState
)
{
    PROF_CPU();

    char               szKey[MAX_PATH * 2] = "";
    uint64_t           uHashCode           = 0;
    BOOL               bNeedRelease        = FALSE;
    KShaderResourceVK* pShaderResource     = nullptr;
    BOOL               bNewShaderResource  = FALSE;

    KGLOG_ASSERT_EXIT(pcszShaderSource && pcszShaderDef && pcszMacro);

    KStringCchPrintfA(szKey, _countof(szKey), "%s_%s_%s_%s", pcszShaderSource, sIncludeShaderLoc.GetFilePath().Str(), pcszShaderDef, pcszMacro);
    uHashCode = KSTR_HELPER::GetHashCodeForString64Bit(szKey, 0);

#if defined(_WIN32) && !defined(KG_PUBLISH)
    if (uThreadLevel == KEnumMtlTaskLevel::DISABLE_MTL_THREAD)
    {
        // windows平台下把一定要做同步加载的材质编译信息记录到client下的pre_shader.txt，然后复制这个文件到data/material/shader_mb/pre_shader.txt,引擎启动的时候首先同步预加载这些shader，减少后面的同步shader编译顿卡
        if (m_pShaderPreBuildLogFile && pcszShaderSource && sIncludeShaderLoc.IsValid() && pcszShaderDef && pcszMacro)
        {
            fprintf(m_pShaderPreBuildLogFile, "%s,%s,%s,%s\n", pcszShaderSource, sIncludeShaderLoc.GetFilePath().Str(), pcszShaderDef, pcszMacro);
            fflush(m_pShaderPreBuildLogFile);
        }
    }
#endif

    bLoading = FALSE;
    {
        std::lock_guard<std::mutex> lock(m_shaderResourceLock);
        auto                        it = m_mapShaderResourceVK.find(uHashCode);
        if (it != m_mapShaderResourceVK.end())
        {
            pShaderResource = it->second;
            if (pShaderResource->IsLoaded())
            {
                pShaderResource->AddRef();
            }
            else
            {
                bLoading        = TRUE;
                pShaderResource = nullptr;
            }
        }
        else
        {
            bNewShaderResource = TRUE;
            pShaderResource    = new KShaderResourceVK;
            if (mapSamplerState)
            {
                for (auto t : *mapSamplerState)
                {
                    pShaderResource->AddSamplerState(t.first, t.second);
                }
            }
            pShaderResource->SetHashCode(uHashCode);
            auto itInsert = m_mapShaderResourceVK.insert(std::make_pair<>(uHashCode, pShaderResource));
            ASSERT(itInsert.second);
        }
    }

    if (bNewShaderResource)
    {
        if (!pShaderResource->LoadFromFileVSFS(pcszShaderSource, sIncludeShaderLoc, pcszShaderDef, pcszMacro))
        {
            KGLogPrintf(KGLOG_ERR, "load resource :%s failed\n user shader:%s \n shaderDef:%s", pcszShaderSource, sIncludeShaderLoc.GetFilePath().Str(), pcszShaderDef);
            pShaderResource = nullptr;
        }
    }

Exit0:
    return pShaderResource;
}

void KShaderResourcePoolVK::RemoveShaderResource(KShaderResourceVK* pShaderResource)
{
    PROF_CPU();

    BOOL bEreased = false;

    if (pShaderResource->IsOrphan())
    {
        bEreased = true;
    }
    else
    {
        std::lock_guard<std::mutex> _lock(m_shaderResourceLock);
        if (pShaderResource->GetRef() == 0)
        {
            auto   uHashCode = pShaderResource->GetHashCode();
            size_t ret       = m_mapShaderResourceVK.erase(uHashCode);
            if (ret == 1)
            {
                bEreased = true;
            }
            ASSERT(bEreased);
        }
    }

    if (bEreased)
    {
        SAFE_DELETE(pShaderResource);
    }
}

KShaderResourcePoolVK* g_pShaderResourcePoolVK = nullptr;
BOOL                   NSEngine::CreateShaderResourcePoolVK()
{
    if (!g_pShaderResourcePoolVK)
    {
        g_pShaderResourcePoolVK = new KShaderResourcePoolVK;
    }
    return true;
}

BOOL NSEngine::InitShaderResourcePoolVK()
{
    BOOL bRet = false;
    if (g_pShaderResourcePoolVK)
    {
        bRet = g_pShaderResourcePoolVK->Init();
    }
    return bRet;
}

BOOL NSEngine::DestroyShaderResourcePoolVK()
{
    if (g_pShaderResourcePoolVK)
    {
        SAFE_DELETE(g_pShaderResourcePoolVK);
    }
    return true;
}

KShaderResourcePoolVK* NSEngine::GetShaderResroucePoolVK()
{
    return g_pShaderResourcePoolVK;
}

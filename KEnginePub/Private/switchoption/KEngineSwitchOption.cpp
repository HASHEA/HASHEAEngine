#include "KEnginePub/Public/switchoption/KEngineSwitchOption.h"
#include "Engine/KGLog.h"
#include "Engine/FileTypeBase.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KBase/Public/system/ISystem.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/thread/KThread.h"
#include "KEnginePub/Public/KEsDrv.h"

#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_PLATFORM        1
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_GPU             2
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_CPU             3
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_MODEL           4
#define X3D_ENGINE_SWITCH_OPTION_TAB_COL_THREAD_AFFINITY 5

namespace
{
    bool IsMatchSwitchValue(const std::string strCurVal, char szColValue[256])
    {
        bool                     bMatch = false;
        std::vector<std::string> vecSplitValue;

        if (szColValue[0] == '*')
        {
            bMatch = true;
            goto Exit0;
        }

        KSTR_HELPER::toLower(szColValue);
        KSTR_HELPER::StrSplit(szColValue, "|", vecSplitValue);
        for (auto strSplitVal : vecSplitValue)
        {
            if (strCurVal.find(strSplitVal) != std::string::npos)
            {
                // 找到了需要block
                bMatch = true;
                break;
            }
        }

    Exit0:
        return bMatch;
    }
} // namespace

namespace NSEngine
{

#define GET_SWITCH_OPTION_HOOK(MODULENAME)                                                \
    {                                                                                     \
        static KUniqueStr ustrModule##MODULENAME = g_CacheOriginalString(#MODULENAME);    \
        int               nRet                   = GetSwitchOpen(ustrModule##MODULENAME); \
        if (nRet != -1)                                                                   \
        {                                                                                 \
            return nRet;                                                                  \
        }                                                                                 \
    }

#define SET_SWITCH_OPTION_HOOK(MODULENAME)                                                \
    {                                                                                     \
        static KUniqueStr ustrModule##MODULENAME = g_CacheOriginalString(#MODULENAME);    \
        int               nRet                   = GetSwitchOpen(ustrModule##MODULENAME); \
        if (nRet != -1)                                                                   \
        {                                                                                 \
            return;                                                                       \
        }                                                                                 \
    }


#define GPU_SWITCH_OPTION_TAB_PATH "enginedata/GpuSwitchOptionTab.tab"

    static KEngineSwitchOption g_sEngineSwitchOption;

    KEngineSwitchOption* GetEngineSwitchOptions()
    {
        return &g_sEngineSwitchOption;
    }

    AnimationOption* GetAnimationOptions()
    {
        return g_sEngineSwitchOption.GetNativeAnimationOptions();
    }

    EngineModuleOption* GetEngineModuleOptions()
    {
        return g_sEngineSwitchOption.GetNativEngineModuleOptions();
    }

    PostRenderOption* GetPostRenderModuleOptions()
    {
        return g_sEngineSwitchOption.GetNativePostRenderOptions();
    }

    FoliageOption* GetFoliageOptions()
    {
        return g_sEngineSwitchOption.GetNativeFoliageOptions();
    }
} // namespace NSEngine

KEngineSwitchOption::KEngineSwitchOption()
{
    BOOL bLow = FALSE;

    // 初始化模块默认变量
    // engine

    // engineModuleOptions.bForceDynamicUBO = TRUE;

    engineModuleOptions.bRenderTerrain         = TRUE;
    engineModuleOptions.bRenderSfx             = TRUE;
    engineModuleOptions.bRenderModel           = TRUE;
    engineModuleOptions.bRenderSkinModel       = TRUE;
    engineModuleOptions.bRenderSceneActorModel = TRUE;
    engineModuleOptions.bRenderGameplayModel   = TRUE;
    engineModuleOptions.bRenderOfflineGI       = FALSE;
    engineModuleOptions.bRenderDecal           = TRUE;
    engineModuleOptions.bRenderPlant           = TRUE;
    engineModuleOptions.bRenderWater           = TRUE;
    engineModuleOptions.bEnableShadowMask      = TRUE;
    engineModuleOptions.bEnableSpotLights      = TRUE;
    engineModuleOptions.bDeferredSpecular      = TRUE;
    engineModuleOptions.bEnableSSS             = FALSE;
    engineModuleOptions.bEnableCameraLight     = TRUE;
    engineModuleOptions.bEnableOIT             = TRUE;
    engineModuleOptions.bRenderPointLight      = TRUE;
    engineModuleOptions.bEnableWeather         = TRUE;

    // post render
    postRenderOptions.bRenderShockWave  = TRUE;
    postRenderOptions.bEnableDof        = FALSE;
    postRenderOptions.bEnableBloom      = TRUE;
    postRenderOptions.bEnableVignette   = FALSE;
    postRenderOptions.bEnableDithering  = TRUE;
    postRenderOptions.bEnableFXAA       = FALSE;
    postRenderOptions.bEnableTAA        = TRUE;
    postRenderOptions.bEnableTAASharp   = FALSE;
    postRenderOptions.bEnableSSPR       = TRUE;
    postRenderOptions.bSimpleSSR        = TRUE;
    postRenderOptions.bEnableSSAO       = FALSE;
    postRenderOptions.bEnableSSGI       = FALSE;
    postRenderOptions.bEnableMSAA       = FALSE;
    postRenderOptions.bHeightFog        = TRUE;
    postRenderOptions.bEnablePostRender = TRUE;
    postRenderOptions.bEnableSSR        = FALSE;

#ifdef _WIN32
    postRenderOptions.bSimpleSSR = FALSE;
#endif

    // foliage
    foliageOptions.bRenderPlantTree  = TRUE;
    foliageOptions.bRenderPlantGrass = TRUE;

#if defined(__ANDROID__) || defined(__APPLE__)
    bLow = TRUE;
#endif
    if (bLow)
    {
        postRenderOptions.bEnableTAA   = FALSE;
        postRenderOptions.bEnableFXAA  = TRUE;
        postRenderOptions.bEnableSSAO  = FALSE;
        postRenderOptions.bEnableSSGI  = FALSE;
        engineModuleOptions.bEnableSSS = FALSE;
    }

    postRenderOptions.bLightOcclusion  = FALSE;
    postRenderOptions.bLightShaftBloom = TRUE;

    postRenderOptions.bShockWave                 = TRUE;
    postRenderOptions.bEnableCAS                 = FALSE;
    postRenderOptions.bEnableFSR                 = TRUE;
    postRenderOptions.bEnableGrain               = FALSE;
    postRenderOptions.bEnableChromaticAberration = FALSE;

    // Want 类型开关放到最后，和目标变量一致
    engineModuleOptions.bWantPointLights      = engineModuleOptions.bRenderPointLight;
    engineModuleOptions.bWantSpotLights       = engineModuleOptions.bEnableSpotLights;
    postRenderOptions.bWantEnableTAA          = postRenderOptions.bEnableTAA;
    postRenderOptions.bWantEnableSSAO         = postRenderOptions.bEnableSSAO;
    postRenderOptions.bWantEnableSSGI         = postRenderOptions.bEnableSSGI;
    engineModuleOptions.bWantEnableShadowMask = engineModuleOptions.bEnableShadowMask;
    postRenderOptions.bUseDeinterleave        = FALSE;
    postRenderOptions.bEnableRayMarchFog      = FALSE;
    postRenderOptions.bWantEnableSSR          = postRenderOptions.bEnableSSR;
}

BOOL KEngineSwitchOption::InitSwitchConfig(const char* pcszGpu)
{
    BOOL bResult  = FALSE;
    BOOL bRetCode = FALSE;

    ITabFile*                piTableFile         = nullptr;
    int                      nRowCount           = 0;
    int                      nColumnCount        = 0;
    int                      eThreadAffinityType = static_cast<int>(NSKBase::EThreadAffinityType::ONE_BIG_CORE);
    bool                     bSwitchOpen         = true;
    const char*              pcszSwitchName      = nullptr;
    char                     szTmpValue[256]     = "";
    int                      nValue              = 0;
    std::vector<std::string> vecSplitValue;

#ifdef _WIN32
    const std::string strPlatform = "win";
#elif defined(__OHOS__)
    const std::string strPlatform = "harmonyOS";
#elif defined(__ANDROID__)
    const std::string strPlatform = "android";
#elif defined(__MACOS__)
    const std::string strPlatform = "macos";
#elif defined(__APPLE__)
    const std::string strPlatform = "ios";
#else
    ASSERT(FALSE);
#endif

    KGLOG_PROCESS_ERROR(pcszGpu);

    // 获取gpu的名字
    g_StrCpyLen(szTmpValue, pcszGpu, countof(szTmpValue));
    KSTR_HELPER::toLower(szTmpValue);
    m_strGpuDevice = szTmpValue;
    KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] current gpu value: %s", m_strGpuDevice.c_str());

    // 获取cpu的名字
    {
        int bGetRet = NSKBase::g_X3DSystem->GetCPUInfo(szTmpValue, countof(szTmpValue));
        if (bGetRet)
        {
            KSTR_HELPER::toLower(szTmpValue);
            m_strCpuDevice = szTmpValue;
        }
        KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] current cpu value: %s", m_strCpuDevice.c_str());
    }

    // 获取device的名字
    {
        int bGetRet = NSKBase::g_X3DSystem->GetDeviceModel(szTmpValue, countof(szTmpValue));
        if (bGetRet)
        {
            KSTR_HELPER::toLower(szTmpValue);
            m_strDeviceModel = szTmpValue;
        }
    }
    KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] current device value: %s", m_strDeviceModel.c_str());

    // 加载配置表
    piTableFile = g_OpenTabFile(GPU_SWITCH_OPTION_TAB_PATH);
    KG_PROCESS_ERROR(piTableFile);

    piTableFile->SetErrorLog(false);

    // 对应的devices有限制，获取每一个模块，cache
    nRowCount    = piTableFile->GetHeight();
    nColumnCount = piTableFile->GetWidth();
    KG_PROCESS_ERROR(nRowCount >= 4);
    KG_PROCESS_ERROR(nColumnCount > 0);

    // 匹配对应的device
    for (int nLine = 4; nLine <= nRowCount; ++nLine)
    {
        // 匹配项目
        for (int nCol = 1; nCol <= nColumnCount; ++nCol)
        {
            // 第一列是平台，第二列GPU型号，第三列CPU型号，第四列为机型，第五列为主线程CPU亲合度，其余都是bool 模块是否开启
            if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_PLATFORM)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1) // 读取到数据，对特定型号判断，如果没有数据，GPU限制
                {
                    bool bMatch = IsMatchSwitchValue(strPlatform, szTmpValue);
                    if (!bMatch)   // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_GPU)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1)
                {
                    bool bMatch = IsMatchSwitchValue(m_strGpuDevice, szTmpValue);
                    if (!bMatch) // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_CPU)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1)
                {
                    bool bMatch = IsMatchSwitchValue(m_strCpuDevice, szTmpValue);
                    if (!bMatch) // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_MODEL)
            {
                bRetCode = piTableFile->GetString(nLine, nCol, "", szTmpValue, sizeof(szTmpValue));
                if (bRetCode == 1) // 读取到数据，对特定型号判断，如果没有数据，GPU限制
                {
                    bool bMatch = IsMatchSwitchValue(m_strDeviceModel, szTmpValue);
                    if (!bMatch)   // 匹配不成功，就不限制了
                    {
                        break;
                    }
                }
            }
            else if (nCol == X3D_ENGINE_SWITCH_OPTION_TAB_COL_THREAD_AFFINITY)
            {
                bRetCode = piTableFile->GetInteger(nLine, nCol, 0, &nValue);
                if (bRetCode == 1)
                {
                    eThreadAffinityType = nValue;
                }
            }
            else
            {
                pcszSwitchName = piTableFile->GetColName(nCol);
                bRetCode       = piTableFile->GetBoolean(nLine, nCol, true, &bSwitchOpen);
                if (bRetCode == 1 && pcszSwitchName && pcszSwitchName[0])
                {
                    std::lock_guard _lock(m_mtxSwitch);
                    m_mapSwitch.emplace(g_CacheOriginalString(pcszSwitchName), bSwitchOpen);

                    KGLogPrintf(KGLOG_INFO, "[EngineSwitchOption] match switch %s, %d", pcszSwitchName, bSwitchOpen ? 1 : 0);
                }
            }
        }
    }

    bResult = TRUE;
Exit0:
    KG_COM_RELEASE(piTableFile);
    NSKBase::Android_SetThreadProcessorAffinityType(static_cast<NSKBase::EThreadAffinityType>(eThreadAffinityType));
    KGLogPrintf(KGLOG_INFO, "gpu value: %s", m_strGpuDevice.c_str());
    KGLogPrintf(KGLOG_INFO, "cpu value: %s", m_strCpuDevice.c_str());
    KGLogPrintf(KGLOG_INFO, "szModel value %s", m_strDeviceModel.c_str());
    KGLogPrintf(KGLOG_INFO, "platform value %s", strPlatform.c_str());
    KGLogPrintf(KGLOG_INFO, "affinity type %d", eThreadAffinityType);
    return bResult;
}

int KEngineSwitchOption::GetSwitchOpen(KUniqueStr ustrSwitchName)
{
    int nRetCode = -1;
    KEngineOptions* pOptions = NSEngine::GetEngineOptions();

    std::lock_guard _lock(m_mtxSwitch);
    auto iter = m_mapSwitch.find(ustrSwitchName);
    if (iter != m_mapSwitch.end())
    {
        return iter->second;
    }

    return nRetCode;
}

BOOL KEngineSwitchOption::GetEnableAnimationFusion()
{
    GET_SWITCH_OPTION_HOOK(bEnableAnimationFusion);
    return animationOptions.bEnableAnimationFusion;
}

BOOL KEngineSwitchOption::GetEnablePositionSmooth()
{
    GET_SWITCH_OPTION_HOOK(bEnablePositionSmooth);
    return animationOptions.bEnablePositionSmooth;
}

BOOL KEngineSwitchOption::GetEnableRotationSmooth()
{
    GET_SWITCH_OPTION_HOOK(bEnableRotationSmooth);
    return animationOptions.bEnableRotationSmooth;
}

BOOL KEngineSwitchOption::GetRenderTerrain()
{
    // return false;
    GET_SWITCH_OPTION_HOOK(bRenderTerrain);
    return engineModuleOptions.bRenderTerrain;
}

BOOL KEngineSwitchOption::GetEnableCameraLight()
{
    GET_SWITCH_OPTION_HOOK(bEnableCameraLight);
    return engineModuleOptions.bEnableCameraLight;
}

BOOL KEngineSwitchOption::GetEnableBloom()
{
    GET_SWITCH_OPTION_HOOK(bEnableBloom);
    return postRenderOptions.bEnableBloom;
}

BOOL KEngineSwitchOption::GetEnableDof()
{
    GET_SWITCH_OPTION_HOOK(bEnableDof);
    return postRenderOptions.bEnableDof;
}

BOOL KEngineSwitchOption::GetEnableDofBokeh()
{
    GET_SWITCH_OPTION_HOOK(bEanbleDofBokeh);
    return postRenderOptions.bEanbleDofBokeh;
}

BOOL KEngineSwitchOption::GetEnableDepthPrePass()
{
    GET_SWITCH_OPTION_HOOK(bEnableDepthPrePass);
    return engineModuleOptions.bEnableDepthPrePass;
}

BOOL KEngineSwitchOption::GetEnableIGDRenderCmdQueue()
{
    GET_SWITCH_OPTION_HOOK(bEnableIGDRenderCmdQueue);
    return engineModuleOptions.bEnableIGDRenderCmdQueue;
}

BOOL KEngineSwitchOption::GetEnableVignette()
{
    GET_SWITCH_OPTION_HOOK(bEnableVignette);
    return postRenderOptions.bEnableVignette;
}

BOOL KEngineSwitchOption::GetEnableFSR()
{
    GET_SWITCH_OPTION_HOOK(bEnableFSR);
    return postRenderOptions.bEnableFSR;
}

BOOL KEngineSwitchOption::GetRenderPlant()
{
    GET_SWITCH_OPTION_HOOK(bRenderPlant);
    return engineModuleOptions.bRenderPlant;
}

BOOL KEngineSwitchOption::GetRenderWater()
{
    GET_SWITCH_OPTION_HOOK(bRenderWater);
    return engineModuleOptions.bRenderWater;
}

BOOL KEngineSwitchOption::GetRenderDecal()
{
    GET_SWITCH_OPTION_HOOK(bRenderDecal);
    return engineModuleOptions.bRenderDecal;
}

BOOL KEngineSwitchOption::GetRenderModel()
{
    GET_SWITCH_OPTION_HOOK(bRenderModel);
    return engineModuleOptions.bRenderModel;
}

BOOL KEngineSwitchOption::GetRenderSkinModel()
{
    GET_SWITCH_OPTION_HOOK(bRenderSkinModel);
    return engineModuleOptions.bRenderSkinModel;
}

BOOL KEngineSwitchOption::GetEnableSSS()
{
    GET_SWITCH_OPTION_HOOK(bEnableSSS);
    return TRUE; // engineModuleOptions.bEnableSSS;
}

BOOL KEngineSwitchOption::GetRenderSfx()
{
    GET_SWITCH_OPTION_HOOK(bRenderSfx);
    return engineModuleOptions.bRenderSfx;
}

BOOL KEngineSwitchOption::GetEnableMergeParticle()
{
    GET_SWITCH_OPTION_HOOK(bEnableMergeParticle);
    return engineModuleOptions.bEnableMergeParticle;
}

BOOL KEngineSwitchOption::GetEnableOIT()
{
    GET_SWITCH_OPTION_HOOK(bEnableOIT);
    return engineModuleOptions.bEnableOIT;
}

BOOL KEngineSwitchOption::GetEnableOfflineGI()
{
    GET_SWITCH_OPTION_HOOK(bRenderOfflineGI);
    return engineModuleOptions.bRenderOfflineGI;
}

BOOL KEngineSwitchOption::GetEnableOfflineGIDynamicRelease()
{
    GET_SWITCH_OPTION_HOOK(bRenderOfflineGI);
    return engineModuleOptions.bOfflineGIDynamicRelease;
}

BOOL KEngineSwitchOption::GetOpenSpotLightOITShadow()
{
    GET_SWITCH_OPTION_HOOK(bOpenSpotLightOITShadow);
    return engineModuleOptions.bOpenSpotLightOITShadow;
}

BOOL KEngineSwitchOption::GetOpenSpotLightOpaqueShadow()
{
    GET_SWITCH_OPTION_HOOK(bOpenSpotLightOpaqueShadow);
    return engineModuleOptions.bOpenSpotLightOpaqueShadow;
}

BOOL KEngineSwitchOption::GetEnableGpuCullWater()
{
    GET_SWITCH_OPTION_HOOK(bEnableGpuCullWater);
    return engineModuleOptions.bEnableGpuCullWater;
}

BOOL KEngineSwitchOption::GetEnableFoliageShadow()
{
    GET_SWITCH_OPTION_HOOK(bEnableFoliageShadow);
    return foliageOptions.bEnableFoliageShadow;
}

BOOL KEngineSwitchOption::GetRenderPlantTree()
{
    GET_SWITCH_OPTION_HOOK(bRenderPlantTree);
    return foliageOptions.bRenderPlantTree;
}

BOOL KEngineSwitchOption::GetRenderPlantGrass()
{
    GET_SWITCH_OPTION_HOOK(bRenderPlantGrass);
    return foliageOptions.bRenderPlantGrass;
}

BOOL KEngineSwitchOption::GetEnablePostRender()
{
    GET_SWITCH_OPTION_HOOK(bEnablePostRender);
    return postRenderOptions.bEnablePostRender;
}

BOOL KEngineSwitchOption::GetLightOcclusion()
{
    GET_SWITCH_OPTION_HOOK(bLightOcclusion);
    return postRenderOptions.bLightOcclusion;
}

BOOL KEngineSwitchOption::GetLightShaftBloom()
{
    GET_SWITCH_OPTION_HOOK(bLightShaftBloom);
    return postRenderOptions.bLightShaftBloom;
}

BOOL KEngineSwitchOption::GetRenderShockWave()
{
    GET_SWITCH_OPTION_HOOK(bRenderShockWave);
    return postRenderOptions.bRenderShockWave;
}

BOOL KEngineSwitchOption::GetHeightFog()
{
    GET_SWITCH_OPTION_HOOK(bHeightFog);
    return postRenderOptions.bHeightFog;
}

BOOL KEngineSwitchOption::GetEnableFXAA()
{
    GET_SWITCH_OPTION_HOOK(bEnableFXAA);
    return postRenderOptions.bEnableFXAA;
}

BOOL KEngineSwitchOption::GetEnableCAS()
{
    GET_SWITCH_OPTION_HOOK(bEnableCAS);
    return postRenderOptions.bEnableCAS;
}

BOOL KEngineSwitchOption::GetShockWave()
{
    GET_SWITCH_OPTION_HOOK(bShockWave);
    return postRenderOptions.bShockWave;
}

BOOL KEngineSwitchOption::GetEnableDithering()
{
    GET_SWITCH_OPTION_HOOK(bEnableDithering);
    return postRenderOptions.bEnableDithering;
}

BOOL KEngineSwitchOption::GetEnableGrain()
{
    GET_SWITCH_OPTION_HOOK(bEnableGrain);
    return postRenderOptions.bEnableGrain;
}

BOOL KEngineSwitchOption::GetEnableChromaticAberration()
{
    GET_SWITCH_OPTION_HOOK(bEnableChromaticAberration);
    return postRenderOptions.bEnableChromaticAberration;
}

BOOL KEngineSwitchOption::GetEnableRayMarchFog()
{
    GET_SWITCH_OPTION_HOOK(bEnableRayMarchFog);
    return postRenderOptions.bEnableRayMarchFog;
}

BOOL KEngineSwitchOption::GetEnableTAASharp()
{
    GET_SWITCH_OPTION_HOOK(bEnableTAASharp);
    return postRenderOptions.bEnableTAASharp;
}

BOOL KEngineSwitchOption::GetUseDeinterleave()
{
    GET_SWITCH_OPTION_HOOK(bUseDeinterleave);
    return postRenderOptions.bUseDeinterleave;
}

BOOL KEngineSwitchOption::GetEnableSecondOrderSmooth()
{
    GET_SWITCH_OPTION_HOOK(bEnableSecondOrderSmooth);
    return animationOptions.bEnableSecondOrderSmooth;
}

BOOL KEngineSwitchOption::GetEnableAnimationUpdateCallback()
{
    GET_SWITCH_OPTION_HOOK(bEnableAnimationUpdateCallback);
    return animationOptions.bEnableAnimationUpdateCallback;
}

BOOL KEngineSwitchOption::GetEnablePointLights()
{
    GET_SWITCH_OPTION_HOOK(bWantPointLights);
    return engineModuleOptions.bWantPointLights;
}

BOOL KEngineSwitchOption::GetEnableSpotLights()
{
    GET_SWITCH_OPTION_HOOK(bWantSpotLights);
    return engineModuleOptions.bWantSpotLights;
}


BOOL KEngineSwitchOption::GetEnableShadowMask()
{
    GET_SWITCH_OPTION_HOOK(bWantEnableShadowMask);
    return engineModuleOptions.bWantEnableShadowMask;
}

BOOL KEngineSwitchOption::GetEnableSSAO()
{
    GET_SWITCH_OPTION_HOOK(bWantEnableSSAO);
    return postRenderOptions.bWantEnableSSAO;
}

BOOL KEngineSwitchOption::isEnableForwardPlus()
{
    GET_SWITCH_OPTION_HOOK(bRenderPointLight);
    return engineModuleOptions.bRenderPointLight;
}

BOOL KEngineSwitchOption::GetEnableTAA()
{
    GET_SWITCH_OPTION_HOOK(bWantEnableTAA);
    return postRenderOptions.bWantEnableTAA;
}

BOOL KEngineSwitchOption::GetEnableSSGI()
{
    GET_SWITCH_OPTION_HOOK(bWantEnableSSGI);
    return postRenderOptions.bWantEnableSSGI;
}

BOOL KEngineSwitchOption::GetEnableForceDynamicUBO()
{
    GET_SWITCH_OPTION_HOOK(bEnableForceDynamicUBO);
    return engineModuleOptions.bEnableForceDynamicUBO;
}

//
// BOOL KEngineSwitchOption::GetEnableMSAA()
//{
//    return postRenderOptions.bEnableMSAA;
//}

BOOL KEngineSwitchOption::GetEnableSSPR()
{
    GET_SWITCH_OPTION_HOOK(bEnableSSPR);
    return postRenderOptions.bEnableSSPR;
}

BOOL KEngineSwitchOption::GetSimapleSSRFlag()
{
    GET_SWITCH_OPTION_HOOK(bSimpleSSR);
    return postRenderOptions.bSimpleSSR;
}

// 个人名片不再需要alpha了，盲盒挂件可能使用
// SnapShotFlow KEngineSwitchOption::GetPlayerSnapShotStatus()
// {
// 	return engineModuleOptions.eSnapShotPlay;
// }

BOOL KEngineSwitchOption::GetDeferredSpecular()
{
    GET_SWITCH_OPTION_HOOK(bDeferredSpecular);
    return engineModuleOptions.bDeferredSpecular;
}


BOOL KEngineSwitchOption::GetRenderSceneActorModel()
{
    GET_SWITCH_OPTION_HOOK(bRenderSceneActorModel);
    return engineModuleOptions.bRenderSceneActorModel;
}

BOOL KEngineSwitchOption::GetRenderGameplayModel()
{
    GET_SWITCH_OPTION_HOOK(bRenderGameplayModel);
    return engineModuleOptions.bRenderGameplayModel;
}

BOOL KEngineSwitchOption::GetEnableApexClothing()
{
    GET_SWITCH_OPTION_HOOK(bEnableApexClothing);
    return engineModuleOptions.bEnableApexClothing;
}

BOOL KEngineSwitchOption::GetUseStencilMask()
{
    GET_SWITCH_OPTION_HOOK(bUseStencilMask);
    return engineModuleOptions.bUseStencilMask;
}

BOOL KEngineSwitchOption::GetEnableMovieTexture()
{
    GET_SWITCH_OPTION_HOOK(bEnableMovieTexture);
    return false; // engineModuleOptions.bEnableMovieTexture;
}

BOOL KEngineSwitchOption::GetEnableCommonSpotLightDepth()
{
    GET_SWITCH_OPTION_HOOK(bEnableCommonSpotLightDepth);
    return engineModuleOptions.bEnableCommonSpotLightDepth;
}


BOOL KEngineSwitchOption::GetEnableSpecializeConst()
{
    GET_SWITCH_OPTION_HOOK(bEnableSpecializeConst);
    return engineModuleOptions.bEnableSpecializeConst;
}

BOOL KEngineSwitchOption::GetEnableWeather()
{
    GET_SWITCH_OPTION_HOOK(bEnableWeather);
    return engineModuleOptions.bEnableWeather;
}

BOOL KEngineSwitchOption::GetEnableWaterPhysics()
{
    GET_SWITCH_OPTION_HOOK(bEnableWaterPhysics);
    return engineModuleOptions.bEnableWaterPhysics;
}

void KEngineSwitchOption::SetEnableSSGI(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bWantEnableSSGI);
        postRenderOptions.bWantEnableSSGI = bValue;
    }
}

void KEngineSwitchOption::SetEnableSpotLights(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bWantSpotLights);
        engineModuleOptions.bWantSpotLights = bValue;
    }
}

void KEngineSwitchOption::SetEnableShadowMask(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bWantEnableShadowMask);
        engineModuleOptions.bWantEnableShadowMask = bValue;
    }
}

void KEngineSwitchOption::SetEnableFXAA(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableFXAA);
        postRenderOptions.bEnableFXAA = bValue;
    }
}

void KEngineSwitchOption::SetEnableCAS(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableCAS);
        postRenderOptions.bEnableCAS = bValue;
    }
}

void KEngineSwitchOption::SetEnableFSR(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableFSR);
        postRenderOptions.bEnableFSR = bValue;
        postRenderOptions.bEnableCAS = !bValue;
    }
}

void KEngineSwitchOption::SetEnableDepthPrePass(BOOL bValue)
{
    SET_SWITCH_OPTION_HOOK(bEnableDepthPrePass);
    engineModuleOptions.bEnableDepthPrePass = bValue;
}

void KEngineSwitchOption::SetEnableIDGRenderCmdQueue(BOOL bValue)
{
    SET_SWITCH_OPTION_HOOK(bEnableIGDRenderCmdQueue);
    engineModuleOptions.bEnableIGDRenderCmdQueue = bValue;
}

void KEngineSwitchOption::SetRenderTerrain(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderTerrain);
        engineModuleOptions.bRenderTerrain = bValue;
    }
}

void KEngineSwitchOption::SetRenderPlant(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderPlant);
        engineModuleOptions.bRenderPlant = bValue;
    }
}

void KEngineSwitchOption::SetRenderWater(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderWater);
        engineModuleOptions.bRenderWater = bValue;
    }
}

void KEngineSwitchOption::SetRenderDecal(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderDecal);
        engineModuleOptions.bRenderDecal = bValue;
    }
}

void KEngineSwitchOption::SetRenderModel(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderModel);
        engineModuleOptions.bRenderModel = bValue;
    }
}

void KEngineSwitchOption::SetRenderSkinModel(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderSkinModel);
        engineModuleOptions.bRenderSkinModel = bValue;
    }
}

void KEngineSwitchOption::SetRenderSceneActorModel(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderSceneActorModel);
        engineModuleOptions.bRenderSceneActorModel = bValue;
    }
}

void KEngineSwitchOption::SetRenderGameplayModel(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderGameplayModel);
        engineModuleOptions.bRenderGameplayModel = bValue;
    }
}


void KEngineSwitchOption::SetEnableCameraLight(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableCameraLight);
        engineModuleOptions.bEnableCameraLight = bValue;
    }
}


void KEngineSwitchOption::SetEnableSSS(BOOL bValue)
{
    // if (bNotRom)
    //{
    //     engineModuleOptions.bEnableSSS = bValue;
    // }
}

void KEngineSwitchOption::SetRenderSfx(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderSfx);
        engineModuleOptions.bRenderSfx = bValue;
    }
}

void KEngineSwitchOption::SetEnableOIT(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableOIT);
        engineModuleOptions.bEnableOIT = bValue;
    }
}

void KEngineSwitchOption::SetEnableOfflineGI(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderOfflineGI);
        engineModuleOptions.bRenderOfflineGI = bValue;
    }
}

void KEngineSwitchOption::SetEnableOfflineGIDynamicRelease(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderOfflineGI);
        engineModuleOptions.bOfflineGIDynamicRelease = bValue;
    }
}


void KEngineSwitchOption::SetEnableSpecializeConst(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableSpecializeConst);
        engineModuleOptions.bEnableSpecializeConst      = bValue;
        DrvOption::bMacroToSpicalizationConstantsEnable = bValue;
    }
}

void KEngineSwitchOption::SetOpenSpotLightOITShadow(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bOpenSpotLightOITShadow);
        engineModuleOptions.bOpenSpotLightOITShadow = bValue;
    }
}

void KEngineSwitchOption::SetOpenSpotLightOpaqueShadow(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bOpenSpotLightOpaqueShadow);
        engineModuleOptions.bOpenSpotLightOpaqueShadow = bValue;
    }
}

void KEngineSwitchOption::SetEnableGpuCullWater(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bValue);
        engineModuleOptions.bEnableGpuCullWater = bValue;
    }
}


void KEngineSwitchOption::SetEnableFoliageShadow(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableFoliageShadow);
        foliageOptions.bEnableFoliageShadow = bValue;
    }
}

void KEngineSwitchOption::SetRenderPlantTree(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderPlantTree);
        foliageOptions.bRenderPlantTree = bValue;
    }
}

void KEngineSwitchOption::SetRenderPlantGrass(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderPlantGrass);
        foliageOptions.bRenderPlantGrass = bValue;
    }
}


void KEngineSwitchOption::SetEnablePostRender(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnablePostRender);
        postRenderOptions.bEnablePostRender = bValue;
    }
}


void KEngineSwitchOption::SetEnableDof(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableDof);
        postRenderOptions.bEnableDof = bValue;
    }
}

void KEngineSwitchOption::SetEnableBloom(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableBloom);
        postRenderOptions.bEnableBloom = bValue;
    }
}

void KEngineSwitchOption::SetLightOcclusion(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bLightOcclusion);
        postRenderOptions.bLightOcclusion = bValue;
    }
}

void KEngineSwitchOption::SetLightShaftBloom(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bLightShaftBloom);
        postRenderOptions.bLightShaftBloom = bValue;
    }
}

void KEngineSwitchOption::SetRenderShockWave(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderShockWave);
        postRenderOptions.bRenderShockWave = bValue;
    }
}

void KEngineSwitchOption::SetHeightFog(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bHeightFog);
        postRenderOptions.bHeightFog = bValue;
    }
}

void KEngineSwitchOption::SetEnableVignette(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableVignette);
        postRenderOptions.bEnableVignette = bValue;
    }
}

void KEngineSwitchOption::SetEnableDithering(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableDithering);
        postRenderOptions.bEnableDithering = bValue;
    }
}

void KEngineSwitchOption::SetEnableGrain(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableGrain);
        postRenderOptions.bEnableGrain = bValue;
    }
}

void KEngineSwitchOption::SetEnableChromaticAberration(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableChromaticAberration);
        postRenderOptions.bEnableChromaticAberration = bValue;
    }
}

void KEngineSwitchOption::SetEnableRayMarchFog(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableRayMarchFog);
        postRenderOptions.bEnableRayMarchFog = bValue;
    }
}


void KEngineSwitchOption::SetEnableDofBokeh(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEanbleDofBokeh);
        postRenderOptions.bEanbleDofBokeh = bValue;
    }
}

void KEngineSwitchOption::SetEnableTAASharp(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableTAASharp);
        postRenderOptions.bEnableTAASharp = bValue;
    }
}

void KEngineSwitchOption::SetUseDeinterleave(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bUseDeinterleave);
        postRenderOptions.bUseDeinterleave = bValue;
    }
}

void KEngineSwitchOption::SetEnableSecondOrderSmooth(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableSecondOrderSmooth);
        animationOptions.bEnableSecondOrderSmooth = bValue;
    }
}

void KEngineSwitchOption::SetEnablePositionSmooth(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnablePositionSmooth);
        animationOptions.bEnablePositionSmooth = bValue;
    }
}

void KEngineSwitchOption::SetEnableRotationSmooth(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableRotationSmooth);
        animationOptions.bEnableRotationSmooth = bValue;
    }
}


void KEngineSwitchOption::SetEnableAnimationUpdateCallback(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableAnimationUpdateCallback);
        animationOptions.bEnableAnimationUpdateCallback = bValue;
    }
}

void KEngineSwitchOption::SetEnableAnimationFusion(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableAnimationFusion);
        animationOptions.bEnableAnimationFusion = bValue;
    }
}

void KEngineSwitchOption::SetEnablePointLights(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bWantPointLights);
        engineModuleOptions.bWantPointLights = bValue;
    }
}

void KEngineSwitchOption::SetEnableSSAO(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bWantEnableSSAO);
        postRenderOptions.bWantEnableSSAO = bValue;
    }
}

void KEngineSwitchOption::SetEnableTAA(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bWantEnableTAA);
        postRenderOptions.bWantEnableTAA = bValue;
    }
}


void KEngineSwitchOption::setEnableForwardPlus(bool isEnable)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bRenderPointLight);
        engineModuleOptions.bRenderPointLight = isEnable;
    }
}


void KEngineSwitchOption::SetSSPR(BOOL flag)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableSSPR);
        postRenderOptions.bEnableSSPR = flag;
    }
}

void KEngineSwitchOption::SetSimapleSSRFlag(BOOL flag)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bSimpleSSR);
        postRenderOptions.bSimpleSSR = flag;
    }
}

void KEngineSwitchOption::SetShockWave(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bShockWave);
        postRenderOptions.bShockWave = bValue;
    }
}

void KEngineSwitchOption::SetEnableApexClothing(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableApexClothing);
        engineModuleOptions.bEnableApexClothing = bValue;
    }
}

void KEngineSwitchOption::SetUseStencilMask(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bUseStencilMask);
        engineModuleOptions.bUseStencilMask = bValue;
    }
}

void KEngineSwitchOption::SetEnableMovieTexture(BOOL bValue)
{
    SET_SWITCH_OPTION_HOOK(bEnableMovieTexture);
    engineModuleOptions.bEnableMovieTexture = bValue;
}

void KEngineSwitchOption::SetEnableCommonSpotLightDepth(BOOL bValue)
{
    SET_SWITCH_OPTION_HOOK(bEnableCommonSpotLightDepth);
    engineModuleOptions.bEnableCommonSpotLightDepth = bValue;
}

void KEngineSwitchOption::SetEnableWeather(BOOL bValue)
{
    SET_SWITCH_OPTION_HOOK(bEnableWeather);
    engineModuleOptions.bEnableWeather = bValue;
}

void KEngineSwitchOption::SetEnableWaterPhysics(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableWaterPhysics);
        engineModuleOptions.bEnableWaterPhysics = bValue;
    }
}

void KEngineSwitchOption::SetEnableTerrainRealTimeRender(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableTerrainRealTimeRendder);
        engineModuleOptions.bEnableTerrainRealTimeRendder = bValue;
    }
}
BOOL KEngineSwitchOption::GetEnableTerrainRealTimeRender()
{
    GET_SWITCH_OPTION_HOOK(bEnableTerrainRealTimeRendder);
    return engineModuleOptions.bEnableTerrainRealTimeRendder;
}

void KEngineSwitchOption::SetEnableFluxWater(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableFluxWater);
        engineModuleOptions.bEnableFluxWater = bValue;
    }
}
BOOL KEngineSwitchOption::GetEnableFluxWater()
{
    GET_SWITCH_OPTION_HOOK(bEnableFluxWater);
    return engineModuleOptions.bEnableFluxWater;
}

void KEngineSwitchOption::SetEnableSSR(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bWantEnableSSGI);
        postRenderOptions.bWantEnableSSR = bValue;
    }
}

BOOL KEngineSwitchOption::GetEnableSSR()
{
    GET_SWITCH_OPTION_HOOK(bWantEnableSSR);
    return postRenderOptions.bWantEnableSSR;
}

void KEngineSwitchOption::SetEnableVT(BOOL bValue)
{
    if (bNotRom)
    {
        SET_SWITCH_OPTION_HOOK(bEnableVT);
        engineModuleOptions.bEnableVT = bValue;
    }
}

BOOL KEngineSwitchOption::GetEnableVT()
{
    GET_SWITCH_OPTION_HOOK(bEnableVT);
    return engineModuleOptions.bEnableVT;
}

BOOL KEngineSwitchOption::GetEnableSimpleJobSystem()
{
    GET_SWITCH_OPTION_HOOK(bEnableSimpleJobSystem);
    return engineModuleOptions.bEnableSimpleJobSystem;
}

BOOL KEngineSwitchOption::GetCompressMipTexture()
{
    GET_SWITCH_OPTION_HOOK(bCompressMipTexture);
    return engineModuleOptions.bCompressMipTexture;
}

BOOL KEngineSwitchOption::GetDisableUICmdSimul()
{
    GET_SWITCH_OPTION_HOOK(bDisableUICmdSimul);
    return engineModuleOptions.bDisableUICmdSimul;
}

BOOL KEngineSwitchOption::GetEnableTrimCommandPool()
{
    GET_SWITCH_OPTION_HOOK(bEnableTrimCommnadPool);
    return engineModuleOptions.bEnableTrimCommnadPool;
}

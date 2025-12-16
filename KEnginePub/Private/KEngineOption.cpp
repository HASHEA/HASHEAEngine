#include "KEnginePub/Public/IKEngineOption.h"
#include "KEnginePub/Public/IKResource.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "loader/image/etcpak/EtcPak.h"
#include <mutex>
#include "KGBaseDef/Public/core_base_macro.h"
#include "KBase/Public/time/KTimer.h"
#include "KBase/Public/thread/KThread.h"
#include "KEnginePub/Public/KEsDrv.h"
#include "recorder/KXGReporter.h"
#include "Engine/KGLog.h"
#include "KEnginePub/Public/IKInputKeyDefine.h"
#include "KMaterialSystem/Public/IKMaterialTypes.h"
#include "KEnginePub/Public/switchoption/KEngineSwitchOption.h"
#include "KBase/Public/async_task/KAsyncTaskManager.h"
#include "KEngine/Public/KEngineCore.h"
// #include "xgsdk/XGSDK_interface.h"
//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"

static const float g_cfStaticPlane[] = {500.0f, 1500.0f, 6000.0f, 10000.0f};

KEngineOptions::KEngineOptions()
{
#ifdef _WIN32
    bX3DClientEnableFpsLimit = TRUE;
#else
    bX3DClientEnableFpsLimit = TRUE;
#endif
    bEnablePhysx              = TRUE;
    bCameraShake              = TRUE;
    bLoadTrutTypeFontLib      = TRUE;
    bCheckLog                 = TRUE;
    bForcePvrLoad             = FALSE;
    bTerrainBakeHi16BitChanel = true;
    eTerrainRunTimeBakeRange  = EX3DTerrainRuntimeBakeRange::Range5x5;

    fSfxUpdateSpeed     = 50.0f;
    fTAAMaxCameraFactor = 8.0f;

    ePointCloudLevel         = EX3DPointCloudLevel::POINT_CLOUD_OFF;
    fPointCloudDistanceLimit = 100000.0f;

    fAnimationSpeedFactor = 1.0f;
    bForceEtc2Load        = TRUE;

    bSfxLowOptimize = FALSE;

    bResouceLogWrite = TRUE;

    bEnableTonemapping    = TRUE;
    fShockWaveBufferScale = 1.0f; // 这个参数在ios平台下必须为1否则会宕机
    fBlurBufferScale      = 0.5f;
    nAOBufferScale        = 1;
    fSSRBufferScale       = 1.0f;
    fPlantScale           = 1.4f;

    fCharacheterLightAdjust = 1.0f;

    fSSSDistance = 1000.0f;

    // 默认开启shadow模式
    eViewProbeType = EX3DViewProbeMask::Shadow;

    // 默认六边形光圈
    eApertureType = DofApertureType::Octagonal;

    // mali的gpu有很多兼容性问题，有些mali型号会出现这种情况
    bUniformReSubmitEveryDrawCall = FALSE;

    bEnableHSV  = FALSE;
    fHue        = 0.0f;
    fSaturation = 0.0f;
    fBrightVal  = 0.0f;

    bForceChangeLod    = FALSE;
    nForceLoadLodLevel = 0;

    bLoadSmallTexture = FALSE;
    bLoadHdFace       = FALSE;

    bNormalMapTex = TRUE;

    bEnableGPUTimeStamp = FALSE;

    bAlphaBlendRT0 = FALSE;
    bASTCCompress  = FALSE;

    BOOL bLow = FALSE;

    ShadowMapSize = 2048;

    nPbrDebugMask    = DebugPBRMask::Off;
    fRoughnessScale  = 1.0f;
    nDecalCountLimit = 40;

#if defined(__ANDROID__) || defined(__APPLE__)
    ShadowMapSize = 1024;
    // nShadowMapLevel = SM_OFF;//SM_OFF;//SM_HIGH_LEVEL; //0关闭，1圆片阴影， 2 低，只对人， 3 高，场景全部
    bLow          = TRUE;
#endif

    if (bLow)
    {
        uResolutionScaleOfPercent = 158;
    }

    fExposure = 0;

    uColorgradId             = 0;
    bEnableColorGrade        = FALSE;
    m_fColorGradeBlendFactor = 1.0f;
    bEnablePlayerBakeMtl     = FALSE;
    bEditorMode              = FALSE;

    bInfiniteReflect  = TRUE;
    bEnableCpuProfile = false;

    fFsrCasSharp           = 0.35f;
    nTAAResolutionInvScale = 3;

    // 有bug
    bOnlyBakeShadowMap = true;

    // SwitchOptions
    EngineModuleOption* pEngineModuleOptions = NSEngine::GetEngineModuleOptions();
    PostRenderOption*   pPostRenderOptions   = NSEngine::GetPostRenderModuleOptions();

    cGBufferMask |= pEngineModuleOptions->bRenderPointLight || pPostRenderOptions->bEnableSSAO || pPostRenderOptions->bEnableSSGI || pEngineModuleOptions->bEnableSpotLights || pPostRenderOptions->bEnableSSR ? (char)gfx::MRTMask::Normal : 0;
    cGBufferMask |= pEngineModuleOptions->bRenderPointLight || pPostRenderOptions->bEnableSSGI || pEngineModuleOptions->bEnableSpotLights || pPostRenderOptions->bEnableSSR ? (char)gfx::MRTMask::Albedo : 0;
    cGBufferMask |= pPostRenderOptions->bEnableTAA || pPostRenderOptions->bEnableSSGI ? (char)gfx::MRTMask::MotionVector : 0;
    cGBufferMask |= pEngineModuleOptions->bEnableShadowMask ? (char)gfx::MRTMask::SunLight : 0;

#if defined(__APPLE__) && !defined(__MACOS__)
    shaderFeatureMask = 0; // (uint8_t)ShaderFeatureMasks::float16;
#else
    shaderFeatureMask = 0; // (uint8_t)ShaderFeatureMasks::float16;
#endif

    bProcessorAffinityEnable         = PROCESSORAFFINITY_ENABLE;
    bProcessorAffinityEnableWIN32    = PROCESSORAFFINITY_ENABLE_WIN32;
    bPointLightEffectCharacterEnable = false;
    fShadowMaskDownSampler           = 1.3f;
    fShadowMaskBlurDistanceFactor    = 0.0f;
    fShadowMaskDepthBias             = 0.0002f;
    bOpenCompressShadowMask          = true;
    bOpenCompressShadowRealShadow    = true;

    bOpenLogMissingFile = FALSE;

    // Initialize Rust Component Debug Options
    bEnableRustDebug = FALSE;
    bEnableRustRemoteDebugger = FALSE;
    bEnableRustAnimationDebug = FALSE;
    bEnableRustPerfMonitor = FALSE;
}

NSKMath::KVec4 KEngineOptions::GetShadowMapRange()
{
    return NSKMath::KVec4(g_cfStaticPlane[0], g_cfStaticPlane[1], g_cfStaticPlane[2], g_cfStaticPlane[3]);
}

void KEngineOptions::SetTerrainRunTimeBakeLevel(EX3DTerrainRuntimeBakeLevel eLevel)
{
    if (eLevel > EX3DTerrainRuntimeBakeLevel::Level2048)
    {
        eLevel = EX3DTerrainRuntimeBakeLevel::Level2048;
    }
    if (eLevel < EX3DTerrainRuntimeBakeLevel::Off)
    {
        eLevel = EX3DTerrainRuntimeBakeLevel::Off;
    }
    eTerrainRunTimeBakeLevel = eLevel;
}

EX3DTerrainRuntimeBakeLevel KEngineOptions::GetTerrainRunTimeBakeLevel()
{
    return eTerrainRunTimeBakeLevel;
}

void KEngineOptions::SetShadowmapLevel(int level)
{
    eShadowMapLevel = (EX3DShadowMapLevel)level;
}

void KEngineOptions::SetContactShadow(bool bOpen)
{
    switch (eShadowMapLevel)
    {
    case EX3DShadowMapLevel::SM_EXTREME_HIGH_LEVEL:
        bOpenContactShadow = bOpen;
        break;
    case EX3DShadowMapLevel::SM_HIGH_LEVEL:
        bOpenContactShadow = bOpen;
        break;
    case EX3DShadowMapLevel::SM_MIDDLE_LEVEL:
        bOpenContactShadow = false;
        break;
    case EX3DShadowMapLevel::SM_LOW_LEVEL:
        bOpenContactShadow = false;
        break;
    case EX3DShadowMapLevel::SM_OFF:
        bOpenContactShadow = false;
        break;
    default:
        break;
    }
}

void KEngineOptions::SetCompressShadow(bool bOpenCompressShadow)
{
    bool bCompressShadowAvailable = true;

#if defined(__APPLE__) && !defined(__MACOS__)
    bCompressShadowAvailable = false;
#endif

    switch (eShadowMapLevel)
    {
    case EX3DShadowMapLevel::SM_EXTREME_HIGH_LEVEL:
        if (!bCompressShadowAvailable)
        {
            fShadowMaskDownSampler        = 1.3f;
            bOpenCompressShadowMask       = false;
            bOpenCompressShadowRealShadow = false;
            bOpenShadowMaskBlur           = false;
        }
        else
        {
            fShadowMaskDownSampler        = 1.3f;
            bOpenCompressShadowMask       = bOpenCompressShadow;
            bOpenCompressShadowRealShadow = bOpenCompressShadow;
            bOpenShadowMaskBlur           = bOpenCompressShadow;
        }
        break;
    case EX3DShadowMapLevel::SM_HIGH_LEVEL:
        if (!bCompressShadowAvailable)
        {
            fShadowMaskDownSampler        = 1.3f;
            bOpenCompressShadowMask       = false;
            bOpenCompressShadowRealShadow = false;
            bOpenShadowMaskBlur           = false;
        }
        else
        {
            fShadowMaskDownSampler        = 1.3f;
            bOpenCompressShadowMask       = bOpenCompressShadow;
            bOpenCompressShadowRealShadow = bOpenCompressShadow;
            bOpenShadowMaskBlur           = bOpenCompressShadow;
        }

        break;
    case EX3DShadowMapLevel::SM_MIDDLE_LEVEL:
        bOpenCompressShadowMask       = false;
        bOpenCompressShadowRealShadow = false;
        bOpenShadowMaskBlur           = false;

        break;
    case EX3DShadowMapLevel::SM_LOW_LEVEL:

        bOpenCompressShadowMask       = false;
        bOpenCompressShadowRealShadow = false;
        bOpenShadowMaskBlur           = false;

        break;
    case EX3DShadowMapLevel::SM_OFF:
        bOpenCompressShadowMask       = false;
        bOpenCompressShadowRealShadow = false;
        bOpenShadowMaskBlur           = false;

        break;
    default:
        break;
    }
}

void KEngineOptions::SetRealTimeStaticShadow(bool bOpenRealTimeShadow)
{
    bOpenRealTimeStaticShadow = bOpenRealTimeShadow;
    sIGD.bEnableShadow        = bOpenRealTimeShadow;
}

void KEngineOptions::SetQualityLevel(EX3DQualityLevel qualityLevel)
{
    eQualityLevel                            = qualityLevel;
    EngineModuleOption* pEngineModuleOptions = NSEngine::GetEngineModuleOptions();
    switch (eQualityLevel)
    {
    case EX3DQualityLevel::Low:
        // eTerrainRunTimeBakeRange                    = EX3DTerrainRuntimeBakeRange::Range3x3;
        m_sFoliage.uSRTProtectGrassDensityOfPercent = 50;
        pEngineModuleOptions->bWantEnableShadowMask = false;
        break;
    case EX3DQualityLevel::Mid:
        // eTerrainRunTimeBakeRange                    = EX3DTerrainRuntimeBakeRange::Range3x3;
        m_sFoliage.uSRTProtectGrassDensityOfPercent = 50;
        pEngineModuleOptions->bWantEnableShadowMask = false;
        break;
    case EX3DQualityLevel::High:
        // eTerrainRunTimeBakeRange                    = EX3DTerrainRuntimeBakeRange::Range3x3;
        m_sFoliage.uSRTProtectGrassDensityOfPercent = 60;
        pEngineModuleOptions->bWantEnableShadowMask = true;
        break;
    case EX3DQualityLevel::ExtremeHigh:
#ifdef __APPLE__
        // 苹果设备的内存是金子做的，实在是开不起
        // eTerrainRunTimeBakeRange = EX3DTerrainRuntimeBakeRange::Range3x3;
#else
        // eTerrainRunTimeBakeRange = EX3DTerrainRuntimeBakeRange::Range5x5;
#endif
        m_sFoliage.uSRTProtectGrassDensityOfPercent = 100; // 最高画质保持和编辑器内表现一致，不走密度裁剪
        pEngineModuleOptions->bWantEnableShadowMask = true;
        break;
    default:
        break;
    }
}

void KEngineOptions::SetUserChoiceQualityLevel(EX3DUserChoiceQualityLevel ImagequalityLevel)
{
    if (eUserChoiceQualityLevel != ImagequalityLevel)
    {
        switch (ImagequalityLevel)
        {
        case EX3DUserChoiceQualityLevel::Low:
            m_sFoliage.bEnableTreeWind = FALSE;

            break;
        case EX3DUserChoiceQualityLevel::Mid:
            m_sFoliage.bEnableTreeWind = FALSE;

            break;
        case EX3DUserChoiceQualityLevel::High:
            m_sFoliage.bEnableTreeWind = TRUE;

            break;
        case EX3DUserChoiceQualityLevel::ExtremeHigh:
            m_sFoliage.bEnableTreeWind = TRUE;
            break;
        default:
            break;
        }

        eUserChoiceQualityLevel = ImagequalityLevel;
    }
}

uint32_t KEngineOptions::GetAdjustFoliageDensityOfPercent(uint32_t uDist2AOI, uint32_t uMaxAabb, BOOL bTree /* = TRUE*/)
{
    // 大于重要性植被大小，则不密度裁剪
    if (uMaxAabb >= m_sFoliage.uFoliageImportantSize)
        return 100;

    // 内圈里的植被，密度裁剪
    if (uDist2AOI <= m_sFoliage.uFoliageProtectInnerRadius)
        return bTree ? 100 : m_sFoliage.uSRTProtectGrassDensityOfPercent;

    uint32_t uOutterDensityOfPercent = bTree ? m_sFoliage.uSRTTreeDensityOfPercent : m_sFoliage.uSRTGrassDensityOfPercent;
    // 其他情况，圈外植被受固定密度裁剪
    if (uDist2AOI >= m_sFoliage.uFoliageProtectOuterRadius)
        return uOutterDensityOfPercent;

    if (m_sFoliage.uFoliageProtectInnerRadius >= m_sFoliage.uFoliageProtectOuterRadius) // 避免后面除零
        return uOutterDensityOfPercent;

    uint32_t uInnerDensityOfPercent = bTree ? 100 : m_sFoliage.uSRTProtectGrassDensityOfPercent;
    uint32_t uPercentOffset         = uInnerDensityOfPercent > uOutterDensityOfPercent ? (uInnerDensityOfPercent - uOutterDensityOfPercent) : 0;
    // 其他情况，内圈和外圈之间的植被受线性衰减的密度裁剪
    uint32_t uDensityOfPercent      = uOutterDensityOfPercent + ((m_sFoliage.uFoliageProtectOuterRadius - uDist2AOI) * uPercentOffset) / (m_sFoliage.uFoliageProtectOuterRadius - m_sFoliage.uFoliageProtectInnerRadius);
    ASSERT(uDensityOfPercent <= 100);
    return uDensityOfPercent;
}

uint32_t KEngineOptions::GetFoliageDensityOutProtectRadius(uint32_t uMaxAabb, BOOL bTree)
{
    // 大于重要性植被大小，则不密度裁剪
    if (uMaxAabb >= m_sFoliage.uFoliageImportantSize)
        return 100;

    return bTree ? m_sFoliage.uSRTTreeDensityOfPercent : m_sFoliage.uSRTGrassDensityOfPercent;
}

uint32_t KEngineOptions::GetFoliageDensityInProtectRadius(BOOL bTree)
{
    if (bTree)
        return 100;

    return m_sFoliage.uSRTProtectGrassDensityOfPercent;
}

void KEngineOptions::ExcuteDebug()
{
    //
    ETC_PAK::TestCompress();
}

BOOL KEngineOptions::IsLoadShaderSPV()
{
    return false;
}

uint8_t KEngineOptions::GetMRTMask()
{
    return bAlphaBlendRT0 ? 0 : cGBufferMask;
}

void KEngineOptions::ResetCullAndLodParam()
{
    // 移动端才会读取美术同学配置的场景裁剪参数，才需要走这里的重置逻辑
    if (!DrvOption::IsDeferRender())
    {
        for (int i = 0; i < countof(m_auDefaultLodDistRange); ++i)
        {
            auMeshLodDistance[i] = m_auDefaultLodDistRange[i];
        }

        m_sFoliage.uFoliageHighDetailDistance = 10000;
        m_sFoliage.uFoliageLowDetailDistance  = 30000;
        m_sFoliage.bEnableCustomDistanceLOD   = 0;

        m_sFoliage.bEnableTreeAngleLOD = 1;
        m_sFoliage.fTreeLOD0Angle      = 0.25f;
        m_sFoliage.fTreeLOD1Angle      = 0.075f;
    }
}

KEngineOptions_mutex* GetEngineOptions_mutex()
{
    static KEngineOptions_mutex sInst;
    return &sInst;
}

KEngineOptions_FileConfig* GetEngineOptions_FileConfig()
{
    static KEngineOptions_FileConfig sInst;
    return &sInst;
}

void KEngineOptions::Init()
{
    // 为了兼容性，这里在移动端模式下强制设置bMobileMode
#if !defined(_WIN32) && !defined(__MACOS__)
//    ASSERT(bMobileMode);
//    bMobileMode = TRUE;
#endif

    m_sVG.bEnableVG = false;// DrvOption::IsDeferRender();
    m_sVG.DebugOptions.bForceRenderDirected = DrvOption::IsForceVGRenderDirected();
    m_sVSM.bEnableVSM = m_sVG.bEnableVG && DrvOption::IsDeferRender();

    static_assert(countof(auMeshLodDistance) == countof(m_auDefaultLodDistRange), "ERROR");
    static_assert(countof(m_auDefaultLodDistRange) == 3, "ERROR");
    if (!DrvOption::IsDeferRender())
    {
        m_sFoliage.fTreeLOD0Angle = 0.15f;
        m_sFoliage.fTreeLOD1Angle = 0.075f;

        fShadowCullAngleOfGameplayModel = 0.065f;
        nDist2UseSphereShadow           = 1500;

        m_auDefaultLodDistRange[0] = 10000;
        m_auDefaultLodDistRange[1] = 15000;
        m_auDefaultLodDistRange[2] = 20000;
    }
    else
    {
        m_sFoliage.fTreeLOD0Angle = 0.1f;
        m_sFoliage.fTreeLOD1Angle = 0.06f;

        fShadowCullAngleOfGameplayModel = 0.045f;
        nDist2UseSphereShadow           = 3200;

        m_auDefaultLodDistRange[0] = 15000;
        m_auDefaultLodDistRange[1] = 20000;
        m_auDefaultLodDistRange[2] = 25000;
    }

    for (int i = 0; i < countof(auMeshLodDistance); ++i)
    {
        auMeshLodDistance[i] = m_auDefaultLodDistRange[i];
    }
}

enum enumShaderOptionFlag
{
    Shader_Flag_bSimplifyUserShader_Bit = 0x1,
    Shader_Flag_bSimplifyPBR_Bit        = 0x2,
    Shader_Flag_bDisableAlphaTest_Bit   = 0x4,
    Shader_Flag_bDisableFragShader      = 0x08,
    shader_Flag_bLodDisplayLevel0_Bit   = 0x10,
    shader_Flag_bLodDisplayLevel1_Bit   = 0x20,
    shader_Flag_bLodDisplayLevel2_Bit   = 0x40,
    shader_Flag_bTriCluster_Bit         = 0x80,
    // uint8_t
};

enum enumMtlOptionFlag
{
    Mtl_Flag_bDisplayBakeModel_Bit = 0x1,
};

uint32_t KEngineOptions::GetShaderFlag(uint32_t uLodLevel /* = 0*/, BOOL bTriClusterDraw /* = FALSE*/)
{
    uint32_t uFlag = 0;
    if (bSimplifyUserShader)
    {
        uFlag |= Shader_Flag_bSimplifyUserShader_Bit;
    }
    if (bSimplifyPBR)
    {
        uFlag |= Shader_Flag_bSimplifyPBR_Bit;
    }
    if (bDisableAlphaTest)
    {
        uFlag |= Shader_Flag_bDisableAlphaTest_Bit;
    }
    if (bDisableFragShader)
    {
        uFlag |= Shader_Flag_bDisableFragShader;
    }
    if (bLodDisplay)
    {
        if (uLodLevel == 0)
        {
            uFlag |= shader_Flag_bLodDisplayLevel0_Bit;
        }
        else if (uLodLevel == 1)
        {
            uFlag |= shader_Flag_bLodDisplayLevel1_Bit;
        }
        else
        {
            uFlag |= shader_Flag_bLodDisplayLevel2_Bit;
        }
    }
    if (sIGD.bTriClusterDisplay && bTriClusterDraw)
    {
        uFlag |= shader_Flag_bTriCluster_Bit;
    }


    return uFlag;
}
uint8_t KEngineOptions::GetMtlFlag()
{
    uint8_t uFlag = 0;
    if (bBakeModelDisplay)
    {
        uFlag |= Mtl_Flag_bDisplayBakeModel_Bit;
    }
    return uFlag;
}


BOOL KEngineOptions::GetAllRenderParticle()
{
    return bRenderAllParticle;
}

char KEngineOptions::GetPbrDebugMask()
{
    char nFlags = (char)nPbrDebugMask;
    return nFlags;
}

void KEngineOptions::SetLowQuality()
{
    bGfxLowLevel = TRUE;

    // bEnableSSGI = FALSE;
    // bWantEnableSSGI = FALSE;
    // bEnableSSAO = FALSE;
    // bWantEnableSSAO = FALSE;
    // bEnableTAA = FALSE;
    // bWantEnableTAA = FALSE;
    // bRenderPointLight = FALSE;
    // bEnableFXAA = TRUE;

    // bEnableFSR = FALSE;
    // bEnableCAS = TRUE;

    // bEnableSSS = FALSE;
    // cGBufferMask = 0;
}



namespace NSRender
{
    KEngineOptions_Render* GetEngineOptions_Render()
    {
        static KEngineOptions_Render sInst;
        return &sInst;
    }
} // namespace NSRender

namespace NSEngine
{
    static KEngineOptions engineOption;
    char                  keys[(int)EInputKey::KeyCount];

    std::atomic<int> g_nLogicFrameCount{1};
    std::atomic<int> g_nRenderFrameCount{1};

    NSKBase::KTimer g_sLogicTimer;
    NSKBase::KTimer g_sRenderTimer;

    void SetKeyDown(EInputKey eKey, BOOL bDown)
    {
        ASSERT(IsValidKey((int)eKey));
        if (bDown)
            keys[(int)eKey] = 1;
        else
            keys[(int)eKey] = 0;
    }

    BOOL IsKeyDown(EInputKey eKey)
    {
        ASSERT(IsValidKey((int)eKey));
        if (keys[(int)eKey] == 1)
            return TRUE;
        else
            return FALSE;
    }

    void CopyKeys(char* pKeys)
    {
        memcpy(pKeys, keys, sizeof(char) * countof(keys));
    }

    void ResotreKeys(char* pKeys)
    {
        memcpy(keys, pKeys, sizeof(char) * countof(keys));
    }

    BOOL IsValidKey(int nKey)
    {
        return nKey >= (int)EInputKey::_None && nKey < (int)EInputKey::KeyCount;
    }

    KEngineOptions* GetEngineOptions()
    {
        return &engineOption;
    }

    void AddLogicFrameMoveLoopCount()
    {
        ASSERT(IsLogicThread());
        ++g_nLogicFrameCount;
    }

    int GetLogicFrameMoveLoopCount()
    {
        ASSERT(IsLogicThread() || NSEngine::IsJobSystemWorking());
        return g_nLogicFrameCount;
    }

    void AddRenderFrameMoveLoopCount()
    {
        ASSERT(IsMainThread());
        ++g_nRenderFrameCount;
    }

    int GetRenderFrameMoveLoopCount()
    {
        ASSERT(IsMainThread()); // 防止非渲染线程取这个
        return g_nRenderFrameCount;
    }

    extern "C" int GetFrameMoveLoopCount()
    {
        if (IsMainThread())
        {
            return GetRenderFrameMoveLoopCount();
        }
        else if (IsLogicThread())
        {
            return GetLogicFrameMoveLoopCount();
        }
        else
        {
            ASSERT(FALSE);
            return 0;
        }
    }

    int GetFps()
    {
        static KEnginePerformance* performance = NSEngine::GetEnginePerformance();
        return performance->nRenderFps;
    }

    void SetXGSDK(IXGSDKInterface* piXGSDK)
    {
        g_sX3DXGSDKReporter.SetXGSDK(piXGSDK);
    }

    KEngineOptions_Logic* GetEngineOptions_Logic()
    {
        static KEngineOptions_Logic sInst;
        return &sInst;
    }

} // namespace NSEngine

#define CHECK_INTERVAL 1.0f
#define GC_TIME        5.0f

struct KResourceGCData
{
    std::mutex                        m_mutex;
    std::map<KUniqueStr, IKResource*> m_mapResource;
    float                             m_fDeltaTime;
    KResourceGCData()
    {
        m_fDeltaTime = 0;
    }
    ~KResourceGCData()
    {
        m_mutex.lock();
        for (auto it = m_mapResource.begin(), e = m_mapResource.end(); it != e; ++it)
        {
            IKResource* pResourece = it->second;
            pResourece->OnRelease();
            SAFE_DELETE(pResourece);
        }
        m_mutex.unlock();
    }
};

KResourceGC::KResourceGC()
{
    m_pGCData = new KResourceGCData;
}

KResourceGC::~KResourceGC()
{
    SAFE_DELETE(m_pGCData);
}

void KResourceGC::GC(float fDeltaTime)
{
    PROF_CPU();

    auto pAsyncMgr = NSKBase::g_GetAsyncManager();
    if (m_pGCData->m_fDeltaTime > CHECK_INTERVAL)
    {
        m_pGCData->m_fDeltaTime = 0.0;
        std::lock_guard<decltype(m_pGCData->m_mutex)> _lock(m_pGCData->m_mutex);
        for (auto it = m_pGCData->m_mapResource.begin(); it != m_pGCData->m_mapResource.end();)
        {
            IKResource* pResource = it->second;
            float       fGcTime   = pResource->GetGCTime();
            if (fGcTime < GC_TIME)
            {
                pResource->AddGCTime(CHECK_INTERVAL);
                ++it;
            }
            else
            {
                // 性能监控
#ifndef KG_PUBLISH
                KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
                pPerfMonitor->m_sResource.Remove(pResource);
#endif

                if (pAsyncMgr)
                {
                    pAsyncMgr->AddTask(
                        NSKBase::EAsyncTaskPriority::Low,
                        true,
                        [pResource] {
                            if (pResource)
                            {
                                pResource->OnRelease();
                                delete pResource;
                            }
                        },
                        nullptr
                    );
                }
                else
                {
                    SAFE_DELETE(pResource);
                }
                it = m_pGCData->m_mapResource.erase(it);
            }
        }
    }
    m_pGCData->m_fDeltaTime += fDeltaTime;
}

void KResourceGC::DelayDelete(IKResource* pResource)
{
    // 多线程时，进到这里，可能已经被别的线程TryGetOut拿走了
    if (pResource->GetRef() > 0)
        return;
    if (pResource->IsProcedural())
    {
        ASSERT(!pResource->IsInGC());
        pResource->OnRelease();
        SAFE_DELETE(pResource);
        return;
    }

    // 多线程时，进到这里，可能已经被别的线程TryGetOut拿走，再DelayDelete了
    if (pResource->IsInGC())
        return;

    std::lock_guard<decltype(m_pGCData->m_mutex)> _lock(m_pGCData->m_mutex);
    KUniqueStr                                    ustrResoureceName = pResource->GetResourceName();
    auto                                          it                = m_pGCData->m_mapResource.find(ustrResoureceName);

    if (it != m_pGCData->m_mapResource.end())
    {
        KGLogPrintf(KGLOG_ERR, "Delay delete resource %s failed for duplicate name, thead:%d", ustrResoureceName.Str(), GetThreadId());
        ASSERT(0);
    }
    else
    {
        pResource->SetIsGC(TRUE);
        pResource->ClearGCTime();
        m_pGCData->m_mapResource.insert(std::pair<KUniqueStr, IKResource*>(ustrResoureceName, pResource));
    }
}

IKResource* KResourceGC::TryGetOut(KUniqueStr ustrResourceName)
{
    IKResource* pResource = nullptr;

    std::lock_guard<decltype(m_pGCData->m_mutex)> _lock(m_pGCData->m_mutex);
    auto                                          it = m_pGCData->m_mapResource.find(ustrResourceName);
    if (it == m_pGCData->m_mapResource.end())
        return nullptr;

    pResource = it->second;
    ASSERT(pResource->GetRef() == 0);

    pResource->SetIsGC(FALSE);
    pResource->AddRef();
    pResource->ClearGCTime();
    m_pGCData->m_mapResource.erase(it);

    return pResource;
}

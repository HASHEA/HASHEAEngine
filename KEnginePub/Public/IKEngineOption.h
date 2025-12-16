#pragma once

#include "IKHeader.h"
#include "IKInputKeyDefine.h"
#include <atomic>
#include <array>
#include "KBase/Public/time/KTimer.h"
#include "KEsDrv.h"
#include "KEngineOptionBase.h"
#include "EngineOption2.h"

#ifdef _WIN32
#define MACRO_X3D_MIN_ENTITY_ANGLE_LIMIT 0.01f
#else
#define MACRO_X3D_MIN_ENTITY_ANGLE_LIMIT 0.045f
#endif
#define MACRO_X3D_ORNAMENT_ENTITY_ANGLE_LIMIT (MACRO_X3D_MIN_ENTITY_ANGLE_LIMIT * 1.5f)

namespace NSKBase
{
    class KTimer;
}

class IXGSDKInterface;



//--------------------以上保留----------------------------


enum class EX3DShadowMapLevel
{
    SM_OFF,
    SM_LOW_LEVEL,
    SM_MIDDLE_LEVEL,
    SM_HIGH_LEVEL,
    SM_EXTREME_HIGH_LEVEL,
};

enum class EX3DPointCloudLevel
{
    POINT_CLOUD_OFF,
    POINT_CLOUD_LOW_VS_LEVEL,
    POINT_CLOUD_HIGH_FS_LEVEL
};

enum class EX3DTerrainRuntimeBakeLevel
{
    Off,
    Level512,
    Level1024,
    Level2048,
};

enum class EX3DTerrainRuntimeBakeRange
{
    Range3x3,
    Range5x5
};

enum class EX3DMeshLodLevel
{
    Lod0,
    Lod1,
    Lod2,
    Count,
};

enum class EX3DSrtMeshLodLevel
{
    Lod0,
    Lod1,
    Billboard,
    Count,
};

enum class DebugPBRMask : char
{
    Off = 0,
    Metal,
    Roughness,
    Albedo,
    Specular,
    Normal,
    Emissive,
    Cloth,
    AnisoAngle,
    Anisotropic,
    Bloom, // post process begin
    HBAO,
    SSGI,
    OverDraw,
    PreOIT,
    OIT0,
};

enum class EX3DViewProbeMask : char
{
    None = 0,
    Shadow = 0x1,
    Color = 0x2,
    All = 0x3,
    Count,
    TextureArrayShadowMask = 0x4
};

enum class EX3DQualityLevel : char
{
    None = 0,
    Low = 1,
    Mid = 2,
    High = 3,
    ExtremeHigh = 4,
    Count,
};

enum class EX3DResolutionLevel : char
{
    None = 0,
    Low = 1,
    Mid = 2,
    High = 3,
    ExtremeHigh = 4,
    TakePhoto = 5,
    Count,
};

enum class EX3DUserChoiceQualityLevel : char
{
    None = 0,
    Low = 1,
    Mid = 2,
    High = 3,
    ExtremeHigh = 4,
    Count,
};

enum class DofApertureType : char
{
    None = 0,
    Rectangular = 1,
    Circle = 2,
    Hexagonal = 3,
    Octagonal = 4,
    Count,
};

struct KG3D_WWISE_OPTION
{
    BOOL  bEnableWwise;
    BOOL  bEnableProfile;
    BOOL  bEnableReplaceFmod;
    float fReplaceFmodAR;
    char  szBasePath[MAX_PATH];
    char  szLanguage[100];
    KG3D_WWISE_OPTION()
    {
        bEnableWwise = TRUE;
        bEnableProfile = FALSE;
        bEnableReplaceFmod = FALSE;
        fReplaceFmodAR = FLT_MAX;
        szBasePath[0] = '\0';
        szLanguage[0] = '\0';
    }
};
typedef struct KG3D_SOUND_OPTION
{
    BOOL     bUseFMOD;
    BOOL     b3DSoundFadeMode;
    float    fMinDis;
    float    fMaxDis;
    float    fAdjustRange;
    float    fMinDisD3D;
    float    fMaxDisD3D;
    float    fMaxListenRangeD3D;
    uint32_t dwPlayFailedRecheckTimeSpan; // 播放失败的重新检测时间
    BOOL     bRenderSound;
    KG3D_SOUND_OPTION()
    {
        bUseFMOD = TRUE;
        b3DSoundFadeMode = TRUE;
        fMinDis = 1;
        fMaxDis = 68;
        fAdjustRange = 20;
        fMinDisD3D = 1;
        fMaxDisD3D = 10000;
        fMaxListenRangeD3D = 5000;
        dwPlayFailedRecheckTimeSpan = 5000;
        bRenderSound = FALSE;
    }
} KG3D_SOUND_OPTION;

enum class EX3DSaveDepth2File
{
    Disable,
    Wait2Capture,
    Wait2Save,
    Count,
};

enum class EX3DSystemMemSize
{
    Normal,
    Low,
    Count,
};

enum class EX3DDebugVisualMode : int
{
    None,

    VG_Overviews,
    VG_TriangleId,
    VG_ClusterId,
    VG_InstanceId,
    VG_MaterialDepth,

    VSM_Overviews,
    VSM_CachedPages,
    VSM_VirtualPageIds,
    VSM_PhyiscalPageIds,
    VSM_ClipmapLevels,
    VSM_ShadowResult,
};

struct KX3DEngineOption_IGD
{
    BOOL bStopCull = FALSE;
    BOOL bEnablHizOC = TRUE;
    BOOL bEnablClusterOC = TRUE;
    BOOL bEnableShadow = TRUE;
    BOOL bTriClusterDisplay = FALSE;
    BOOL bDrawClusterBox = FALSE;
    BOOL bEnableSkipCompute = TRUE;
};

struct KX3DEngineOption_Resource
{
    BOOL bEnableSceneEntityPlayDefaultAniCfg = FALSE;
};

struct KX3DEngineOption_Foliage
{
    float fCullerAngleLimitGrass = 0.055f;
    float fCullerAngleLimitTree = 0.045f;

    // Foliage
    /* 植被裁剪规则
    1、重要植被：AABB最长边超过16米的不做密度裁剪
    2、其他植被：焦点 半径50米内的植被不受密度裁剪，50米到100米密度裁剪从1.0线性衰减到0.1，100米外密度均为0.1
    4、以上数值均做成配置项
    */
    uint32_t uSRTGrassDensityOfPercent = 20;
    uint32_t uSRTTreeDensityOfPercent = 50;
    uint32_t uSRTProtectGrassDensityOfPercent = 50;
    uint32_t uFoliageImportantSize = 2400;
    uint32_t uFoliageProtectInnerRadius = 5000;
    uint32_t uFoliageProtectOuterRadius = 10000;

    // Foliage lod
    uint32_t uFoliageHighDetailDistance = 10000;
    uint32_t uFoliageLowDetailDistance = 30000;
    BOOL     bEnableCustomDistanceLOD = FALSE;

    BOOL  bEnableTreeAngleLOD = TRUE;
    float fTreeLOD0Angle = 0.15f;
    float fTreeLOD1Angle = 0.075f;

    int nTreeLODBias = 0; // 树LOD偏移
    int nGrassLODBias = 0; // 草LOD偏移

    BOOL bEnableTreeWind = TRUE;
    BOOL bRenderProceduralGrass = TRUE;
};

// 美术自定义参数，只影响移动端
struct KX3DEngineOption_Art
{
    BOOL  bEnable = FALSE;
    float fBrushSRTLoadCullRadius = 32000.0f;
    float fSceneEntityLoadCullRadius = 100000.0f;
    float fCullDistance = 200000.0f;
};

struct KX3DEngineOption_3rd
{
    // BOOL bEnableIRIS = FALSE; // pixelworks 的硬件插帧SDK
};

enum class EX3DRTResolutionClass : uint8_t
{
    Android_Phone = 0,
    Android_Pad,
    iPhone,
    iPad,
    OHOS_Phone,
    OHOS_Pad,
    Count,
};

enum class eSplashQuality : uint8_t
{
    NONE = 0,
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3
};



struct KG3DMobileEngineOption
{
    int nResolutionScaleOfPercent = 100;         // 动态分辨率百分比
    int nMaxTextureStreamingMip = 0;           // 材质贴图无缝切换最大Mips等级: 0级为1，1级为2,...,9级为512
    int nMaxSceneActorModelTextureMip = 0;           // 场景物件贴图最大Mip(2^9=512)

    float fCullerGrassDensity = 0.1f;  // 裁剪植被-草密度
    float fCullerTreeDensity = 0.1f;  // 裁剪植被-树密度
    int   nCullerFoliageImportSizeOutRadius = 2000;  // 圈外植被密度裁剪重要性植被大小
    int   nCullerFoliageProtectInnerRadius = 5000;  // 植被密度裁剪保护内圈半径
    int   nCullerFoliageProtectOuterRadius = 10000; // 植被密度裁剪保护外圈半径

    float fCullerAngleLimit = 0.045f;        // 裁剪角度限制(物件最长边/摄像机距离 < fCullerAngleLimit 会被裁剪剔除)
    float fCullerParticleAngleLimit = 0.02f;         // 特效的角度裁剪限制，因为特效比较麻烦所以单独整一个
    float fCullerDynamicModel = 0.04f;         // 动态物件的角度裁剪限制，动态物件也想单独调小点没辙，只能再加一个
    float fCullerAngleLimitTree = 0.045f;
    float fShadowCullerAngleLimit = 0.045f;        // 阴影裁剪角度限制(物件最长边/摄像机距离 < fCullerAngleLimit 会被裁剪剔除)
    float fSceneEntityLoadRadius = 40000.f;       // 加载半径
    float fBrushSRTLoadRadius = 40000.f;       // 画刷植被裁剪和加载半径

    bool bEnableLightShaft = false;              // 开启光束
    bool bEnableSSAO = false;              // 开启屏幕空间环境遮蔽
    bool bEnableBloom = true;               // 开启泛光
    bool bEnableDof = true;               // 开启景深，暂未开放参数
    bool bEnableHeightFog = true;
    bool bEnableShaftOcclusion = true;
    bool bEnableRayMarchFog = true;
    bool bEnableSSS = true;
    bool bEnablePointLight = true;
    bool bEnableSpotLights = true;
    bool bEnableShadowMask = true;
    bool bEnableSSGI = false;
    bool bEnableSSPR = true;
    bool bSimpleSSR = true;

    bool bEnableFXAA = false; // 开启FXAA
    bool bEnableTAA = false; // 开启TAA
    bool bEnableTAASharp = false; // 开启TAASharp
    bool bEnableFSR = false; // 开启FSR
    bool bEnableGSR = false; // 开启GSR	// FSR优化开关
    bool bEnableGI = false; // 开启GI
    bool bEnableGIDynamicRelease = true;  // 开启GI切画质动态创建或释放
    // bool bPublish       = false;                                                          // 是否是发布版本
    bool bRenderUIDebug = true;                                                                                  // 是否绘制Debug UI

    EX3DShadowMapLevel          eShadowLevel = EX3DShadowMapLevel::SM_HIGH_LEVEL;      // 阴影贴图等级 SM_OFF, SM_SPHERE_LEVEL, SM_LOW_LEVEL, SM_HIGH_LEVEL,
    EX3DTerrainRuntimeBakeLevel eTerrainBakeLevel = EX3DTerrainRuntimeBakeLevel::Level1024; // 地表贴图实时烘焙等级 Off, Level512, Level1024, Level2048,
    EX3DTerrainRuntimeBakeRange eTerrainRuntimeBakeRange = EX3DTerrainRuntimeBakeRange::Range3x3;
    int                         nSkinModelFrameMoveLimit = 5;                                      // 骨骼模型FrameMove数量限制
    int                         nMeshModelLODBias = 0;
    int                         nSRTLODBias = 0;
    int                         nBrushSRTLODBias = 0;
    int                         nClientSFXLimit = 30;
    int                         nClientOtherPlaySFXLimit = 10;
    int                         nClientSceneSFXLimit = 10;
    int                         nOITEnableDistance = 1600;

    float fOITEnableAngle = 0.25f;
    int nGhostCountLimit = 10;

    EX3DViewProbeMask eViewProbeType = EX3DViewProbeMask::Shadow;

    EX3DResolutionLevel eResolutionLevel = EX3DResolutionLevel::Low;
    int                 nQualityLevel = 0;
    bool                bCameraShake = true;
    int                 nUserChoiceQualityLevel = 0;

    bool bEnableApexClothing = false; // 开启布料
    bool bEnableCompressShadow = true;  // 开启压缩阴影
    bool bEnableRealTimeStaticShadow = true;  // 开启静态物件的实时阴影
    bool bEnableContactShadow = false;  // 开启植被的接触阴影
    KG3DMobileEngineOption() = default;
    int nHouseLoadCount = 4;

    bool bEnableWeather = true;
    bool bEnableScreenRainDrop = true;//天气：屏幕雨滴
    bool bEnableRainSurfaceWater = true;//天气：雨天地表积水潮湿效果
    float fRainSurfaceWaterMaxRange = 200000.0f;//天气：雨天地表积水潮湿效果的视距的上限,单位厘米（这个值美术会调大小，对应编辑器RoughnessFalloffEnd，但这里做一个上限限制到不同画质）;vk视距最大只有2km;优化效果依赖芯片驱动，不一样的芯片可能优化力度不一样;0不等于关闭，关闭使用上面bEnableRainSurfaceWater
    float fWeatherFallRanageScale = 1.0f;//天气：雨雪滴飘落效果的范围，0.1f~1.0f
    eSplashQuality eWeatherSplashQuality = eSplashQuality::HIGH;//天气：雨天雨滴落地涟漪效果 NONE是关闭，LOW\MEDIUM\HIGH是显示范围依次递增
    uint16_t uWeatherCoverMapObjMinSzie = 200;//天气：xz最长边超过这个值的物件才会被烘焙到遮挡关系图里面，才能档雨挡雪;切图后生效;单位厘米
    //uint16_t uWeatherCoverMapObjMinSzie_Homeland = 90;//天气:同上，家园参数；家园有1米乘1米的积木，建议小于100
};

struct KX3DEngineOption_Weather
{
    BOOL bEnableRenderWeatherTopDepth = TRUE;
    BOOL bEnableScreenRainDrop = TRUE;
    BOOL bEnableWeatherFall = TRUE;
    BOOL bEnableWeatherCover = TRUE;
    BOOL bEnableWeatherSplash = TRUE;

    BOOL            bEnableImguiChangeWeatherType = FALSE;
    uint16_t        uWeatherType = 0;
    float           fWeatherFallRangeScale = 1.0f;
    eSplashQuality  eWeatherSplashQuality = eSplashQuality::HIGH;
    // uint32_t uWeatherSplashInstanceLevel = 48;
    BOOL            bEnableRainSurfaceWater = TRUE;
    float           fRainSurfaceWaterMaxRange = 200000.0f; // vk视距最大只有2km
    uint32_t        nWeatherSnowCoverRange = 25000;
    NSKMath::KUint2 n2TopCameraUpdateCell = NSKMath::KUint2(5000, 20000);
    uint16_t        uWeatherCoverMapObjMinSzie = 200;
    // uint16_t uWeatherCoverMapObjMinSzie_Homeland = 90;
    uint32_t        nTopDepthDelayCullFrame = 10;
};

struct KX3DEngineOption_VirtualGeometry
{
    BOOL bEnableVG = false;
    BOOL bSkipImposters = false;
    BOOL bAutoConvertModelLoading = false;
    BOOL bAutoConvertPlayerOnly = false;
    int  nImposterMaxPixels = 32;
    int  nScreenSizeMinPixels = 16;
    int  nVSMSizeMinPixels = 10;
    BOOL bDisableMainTwoPassOcclusion = true;

    float nMaxPixelsPerEdge = 1.0f;
    float nMinPixelsPerEdgeHW = 18.0f;

    struct VIEW_PARAMS
    {
        float fMinBoundsRadius = 0.0f;
        float fLODScaleFactor = 1.0f;
        float fDepthBias = 0.0f;
        float fSlopeScaleDepthBias = 3.0f;
        float fMaxPixelsPerEdgeMultipler = 1.0f;
        BOOL  bEnableSecondaryBins = true;
        BOOL  bForceSecondaryBins = false;
    };

    VIEW_PARAMS MainViewParams;
    VIEW_PARAMS ShadowViewParams[5];

    struct STREAMING_CONFIG
    {
        BOOL bAsync = 0;   // g_VGStreamingAsync, 异步提交数据开关
        int  PoolSize = 512; // g_VGStreamingPoolSize, StreamingPages缓存池容量（MB）
        int  NumInitialRootPages = 512; // g_VGStreamingNumInitialRootPages, RootPages最小分配个数
        int  MaxPendingPages = 128; // g_VGStreamingMaxPendingPages, 等待上传StreamingPages的最大数量
        int  MaxPageInstallsPerFrame = 32;  // g_VGStreamingMaxPageInstallsPerFrame, 单帧最大上传Page数量
    } Streaming;

    struct DEBUG_OPTIONS
    {
        BOOL bShowVG = true;
        BOOL bEnableVGHiZ = true;
        BOOL bForceVGHWRaster = false;
        BOOL bShowVGSWRasterData = true;
        BOOL bEnableVGDebugMtl = false;
        BOOL bAllowHWRasterizeToVisBufferRTV = true;
        BOOL bUseCPUCullingResult = false;
        BOOL bPauseVGStreamingUpdateProcess = false;
        BOOL bForceFixedRasterizer = false;
        BOOL bDisableProgramableRasterizer = false;
        BOOL bForceRenderDirected = false;
    } DebugOptions;

    KX3DEngineOption_VirtualGeometry()
    {
        const float fDepthBias[5] = { 0.00001f, 0.00002f, 0.00004f, 0.00008f, 0.0001f };

        for (unsigned i = 0; i < 5; ++i)
        {
            ShadowViewParams[i].fMinBoundsRadius = 50.0f * (float)i;
            ShadowViewParams[i].fLODScaleFactor = 0.8f; // 0.2f * powf(0.8f, (float)i);
            ShadowViewParams[i].fDepthBias = fDepthBias[i];
            ShadowViewParams[i].fSlopeScaleDepthBias = 3.0f;
            ShadowViewParams[i].fMaxPixelsPerEdgeMultipler = 1.0f;
            ShadowViewParams[i].bEnableSecondaryBins = true;
            ShadowViewParams[i].bForceSecondaryBins = i > 1;
        }
    }
};

struct KX3DEngineOption_VirtualShadowMap
{
    BOOL bEnableVSM = false;
    BOOL bEnableVSMHiZ = false;
    BOOL bEnableVSMTwoPassOcclusion = false;
    BOOL bAllViewUncache = false;
    BOOL bEnableRayTracingShadow = false;

    struct DEBUG_OPTIONS
    {
        BOOL bSkipUpdating = false;
        BOOL bShowDirectionalLightVSM = true;
        BOOL bShowLocalSpotLightVSM = true;
        BOOL bShowLocalPointLightVSM = true;
    } DebugOptions;
};

struct KX3DEngineOption_VirtualTexture
{
    bool bEnableVTFeedback = true; // 是否启用虚拟纹理的反馈
    bool bEnableVTAsyncUpdateAndFetch = true; // 是否启用虚拟纹理的异步更新和获取
    BOOL bEnableVTDebugMode = false; // 是否启用虚拟纹理的调试模式
};
//-------------以上是子option
//--------------------------------------
//-------------以下是option_logic

// KEngineOptions 引擎专用，逻辑请使用KEngineOptions
struct KEngineOptions
{
private:
    uint32_t m_auDefaultLodDistRange[(int)EX3DMeshLodLevel::Count] = { 15000, 20000, 25000 };

public:
    KX3DEngineOption_IGD              sIGD;
    KX3DEngineOption_Foliage          m_sFoliage;
    KX3DEngineOption_Resource         m_sResource;
    KX3DEngineOption_3rd              m_s3rd;
    KX3DEngineOption_Weather          m_sWeather;
    KX3DEngineOption_VirtualGeometry  m_sVG;
    KX3DEngineOption_VirtualTexture   m_sVT;
    KX3DEngineOption_VirtualShadowMap m_sVSM;

    // only main scene
    KX3DEngineOption_Art m_sArtOption;

    KG3D_SOUND_OPTION SoundOptions;
    KG3D_WWISE_OPTION WwiseOptions;
    BOOL              bEditorMode;

    EX3DSystemMemSize eSystemMemSize = EX3DSystemMemSize::Normal;
    EX3DDebugVisualMode eDebugVisualMode = EX3DDebugVisualMode::None; // TODO: 调试模式的控制以后由SceneView独立控制

    // for engine
    BOOL     bX3DClientEnableFpsLimit = FALSE;
    BOOL     bEnablePhysx;
    BOOL     bCameraShake;
    BOOL     bX3DCameraControl = FALSE;
    BOOL     bLoadTrutTypeFontLib;
    BOOL     bCheckLog;
    BOOL     bForcePvrLoad;
    float    fAnimationSpeedFactor;
    BOOL     bForceEtc2Load;
    float    fSfxUpdateSpeed;
    BOOL     bResouceLogWrite;
    BOOL     bEnableTonemapping;
    BOOL     bUniformReSubmitEveryDrawCall;
    float    fShockWaveBufferScale;
    int      ShadowMapSize;
    int      ShadowMapMaxCascade = 3;
    int      nAOBufferScale;
    float    fSSRBufferScale;
    float    fBlurBufferScale;
    float    fPlantScale;
    float    fCharacheterLightAdjust;
    float    fSSSDistance;
    uint32_t uResolutionScaleOfPercent = 100;

    BOOL bEnableExtractedFrame = TRUE;
    BOOL bForceExtractedFrame = FALSE;

    BOOL bLoadBakeMesh = TRUE;

    BOOL bReflectAntiFlicker = TRUE;

    // for render

    EX3DPointCloudLevel ePointCloudLevel;
    float               fPointCloudDistanceLimit;

    BOOL bCheckFinite = FALSE;

    BOOL bSfxLowOptimize;

    BOOL bEnableMovieTexture = TRUE;

    BOOL bAlphaBlendRT0;
    BOOL bInfiniteReflect;

    BOOL    bEnableHSV;
    BOOL    bEnableColorGrade;
    BOOL    bEnableGPUTimeStamp;
    BOOL    bNormalMapTex;
    BOOL    bHDR = FALSE;
    float   fFsrCasSharp;
    uint8_t cGBufferMask = 0;
    uint8_t shaderFeatureMask = 0;

    unsigned        uColorgradId;
    float           m_fColorGradeBlendFactor;
    DofApertureType eApertureType;
    BOOL            bDoFBlurWithCoc = false;

    float fHue;
    float fSaturation;
    float fBrightVal;
    BOOL  bEnablePlayerBakeMtl;
    BOOL  bEnableCpuProfile;
    float fMainCameraFarSee = 200000.0f;
    // postrendereffect

    float fWaterIBLIntensity = 0.5f;
    // tonemapping
    float fExposure = 0.0f;

    float fMipLodBias = 0.0f;

    float fCullerAngleLimit = 0.045f;
    float fShadowCullerAngleLimit = 0.045f;
    float fCullerParticleAngleLimit = 0.02f;
    float fCullerDynamicModelLimit = 0.04f;
    float fShadowCullAngleOfGameplayModel = 0.065f;
    int   nDist2UseSphereShadow = 1500;

    float fRoughnessScale = 1.0f;
    int   nDecalCountLimit = 0;
    // debug
    BOOL  bLoadSmallTexture;

    EX3DShadowMapLevel eShadowMapLevel = EX3DShadowMapLevel::SM_EXTREME_HIGH_LEVEL;
    BOOL               bOnlyBakeShadowMap;

    // MB 限制每次framemove的骨骼模型数量（最近N个）
    int nSkinModelFrameMoveLimit = 5;

    BOOL bEnableLODSwitch = TRUE;
    int  nMaxLodLevel = (int)EX3DMeshLodLevel::Count - 1;
    int  nMeshModelLODBias = 0;
    int  nClientSFXLimit = 30;
    int  nClientSceneSFXLimit = 10;
    int  nClientOtherPlaySFXLimit = 10;

    DebugPBRMask nPbrDebugMask;

    int nSSGIQuality = 2;

    int nTextureStreamingMipBias = 0;
    int nMaxTextureStreamingMipLevel = 0;
    int nMinGamePlayModelTextureMipLevel = 8;  // 角色最小也给个9，512分辨率(256的没法看)
    int nMaxGamePlayModelTextureMipLevel = 11; // 角色最大限制个2048，2048分辨率
    int nMaxSceneActorModelTextureMipLevel = 0;  // 默认没有限制

    uint32_t auMeshLodDistance[(int)EX3DMeshLodLevel::Count]{ 10000, 15000, 20000 };

    KEngineOptions();

    BOOL bForceChangeLod;
    int  nForceLoadLodLevel; // 0-OriginModel 1-Lod1 2-Lod2 3-Lod3

    // for demo test
    BOOL bLoadHdFace;

    EX3DTerrainRuntimeBakeLevel eTerrainRunTimeBakeLevel = EX3DTerrainRuntimeBakeLevel::Level1024; // 0: off 1:512 2:1024 3:2048
    int                         bTerrainBakeHi16BitChanel;
    EX3DTerrainRuntimeBakeRange eTerrainRunTimeBakeRange = EX3DTerrainRuntimeBakeRange::Range5x5;

    BOOL     bDisableShaderErrorLog = FALSE;
    BOOL     bRenderTest = FALSE;
    uint32_t uRenderTestBufferCount = 0;
    uint32_t uRenderTestBufferSize = 0;

    BOOL        bRenderUIDebug = TRUE; // 控制 ImGui 和 引擎信息文字 是否显示
    BOOL        bRenderEngineStatusInfo = TRUE; // 控制 引擎状态信息文字 是否显示
    std::string strShaderCompileEventId;        // 上传至XGSDK的 shader compile id

    BOOL               bInitFaceMakerMeta = TRUE;
    EX3DSaveDepth2File eSaveSceneDepthStep = EX3DSaveDepth2File::Disable;

    // second order smooth
    float fSmoothFrequency = 2.0f;
    float fSmoothDamping = 1.0f;
    float fSmoothInitialResponse = 0.0f;

    EX3DQualityLevel           eQualityLevel = EX3DQualityLevel::High;
    EX3DResolutionLevel        eResolutionLevel = EX3DResolutionLevel::High;
    EX3DUserChoiceQualityLevel eUserChoiceQualityLevel = EX3DUserChoiceQualityLevel::ExtremeHigh;

    using RtResolutionArray = std::array<int, (int)EX3DResolutionLevel::Count>;

    std::array<RtResolutionArray, (int)EX3DRTResolutionClass::Count> arrRtResolutionByClass = {};

    BOOL bSimplifyUserShader = FALSE;
    BOOL bSimplifyPBR = FALSE;
    BOOL bDisableAlphaTest = FALSE;
    BOOL bDisableFragShader = FALSE;
    BOOL bLodDisplay = FALSE;
    BOOL bBakeModelDisplay = FALSE;

    BOOL  bASTCCompress = FALSE;
    int   nOITEnableDistance = 1600;
    float fOITEnableAngle = 0.25f;

    BOOL              bGfxLowLevel = FALSE;
    BOOL              bRenderAllParticle = FALSE;
    EX3DViewProbeMask eViewProbeType = EX3DViewProbeMask::None;

    float fMainCameraCascadeRadius = 80000.f; // 功能已屏蔽
    float fSceneEntityLoadCullRadius = 50000;
    float fBrushSRTLoadCullRadius = 100000;
    float fBrushGrassLoadCullRadius = 10000;

    float fShadowMaskDepthBias = 0.0;
    int   fShadowMaskPCF = 1;
    float fShadowMaskDownSampler = 1.0f;
    float fShadowMaskBlurSize = 1.0f;
    float fShadowMaskBlurDistanceFactor = 1.0f;

    float fTAAMaxCameraFactor = 8.0f;
    int   nTAAResolutionInvScale = 3;

    BOOL bOpenShadowMaskBlur = FALSE;
    BOOL bOpenDebugViewProbe = {};
    BOOL bOpenCompressShadowMask = {};   // 开启压缩阴影
    BOOL bOpenCompressShadowRealShadow = {};   // 开启实时阴影
    BOOL bOpenRealTimeStaticShadow = {};   // 开启静态物件的实时阴影
    BOOL bOpenContactShadow = true; // 开启接触阴影
    BOOL bProcessorAffinityEnable;
    BOOL bProcessorAffinityEnableWIN32;
    BOOL bPointLightEffectCharacterEnable;
    //   void SetRenderTerrain(BOOL bRenderTerrain);
    //   BOOL IsRenderTerrain();
    //__declspec(property(get = IsRenderTerrain, put = SetRenderTerrain)) BOOL bRenderTerrain;
    BOOL bEnableApexClothing = TRUE;
    int  nHouseLoadCount = 4;

    BOOL bOpenLogMissingFile = FALSE;
    BOOL bDrawPhysicsObstacle = FALSE;
    BOOL bUseMaskAlphaScale = TRUE;

    BOOL bDebugDeferredLightWireFrame = FALSE;
    BOOL bDebugDeferredLight = FALSE;

    int  nGhostCountLimit = 10;
    BOOL bEnableParallelJobSystem = true;
    bool bEnableTickJobSystem = true;

    // Rust Component Debug Options
    BOOL bEnableRustDebug = FALSE;
    BOOL bEnableRustRemoteDebugger = FALSE;
    BOOL bEnableRustAnimationDebug = FALSE;
    BOOL bEnableRustPerfMonitor = FALSE;

public:
    void Init();

    void SetShadowmapLevel(int level);
    void SetContactShadow(bool bOpen);
    void SetCompressShadow(bool bOpenCompressShadow);
    void SetRealTimeStaticShadow(bool bOpenRealTimeShadow);

    void SetQualityLevel(EX3DQualityLevel qualityLevel);
    void SetUserChoiceQualityLevel(EX3DUserChoiceQualityLevel ImagequalityLevel);

    void SetColorGrade(BOOL bEnable, uint32_t colorGradeId, float fblendFactor)
    {
        bEnableColorGrade = bEnable;
        m_fColorGradeBlendFactor = fblendFactor;
        uColorgradId = colorGradeId;
    }

    NSKMath::KVec4              GetShadowMapRange();
    void                        SetTerrainRunTimeBakeLevel(EX3DTerrainRuntimeBakeLevel eLevel);
    EX3DTerrainRuntimeBakeLevel GetTerrainRunTimeBakeLevel();

    uint32_t GetAdjustFoliageDensityOfPercent(uint32_t uDist2AOI, uint32_t uMaxAabb, BOOL bTree = FALSE);
    uint32_t GetFoliageDensityOutProtectRadius(uint32_t uMaxAabb, BOOL bTree);
    uint32_t GetFoliageDensityInProtectRadius(BOOL bTree);

    void ExcuteDebug();
    BOOL IsLoadShaderSPV();

    uint32_t GetShaderFlag(uint32_t uLodLevel = 0, BOOL bTriClusterDraw = FALSE);
    uint8_t  GetMtlFlag();
    BOOL     GetAllRenderParticle();
    char     GetPbrDebugMask();
    void     SetLowQuality();
    uint8_t  GetMRTMask();

    void ResetCullAndLodParam();

    BOOL bStopCaptionTexWrite = FALSE;
    BOOL bDisableCaptionRefreshSeting = FALSE;
};

//------------以上engineOption_logic





//--------------以下保留-----------------

namespace NSEngine
{
    extern "C" KEngineOptions* GetEngineOptions();

    extern "C" void CopyKeys(char* keys);
    extern "C" void ResotreKeys(char* keys);
    extern "C" BOOL IsValidKey(int nKey);
    extern "C" void SetKeyDown(EInputKey eKey, BOOL bDown);
    extern "C" BOOL IsKeyDown(EInputKey eKey);

    extern "C" void AddLogicFrameMoveLoopCount();
    extern "C" int  GetLogicFrameMoveLoopCount();

    extern "C" void AddRenderFrameMoveLoopCount();
    extern "C" int  GetRenderFrameMoveLoopCount();

    extern "C" int GetFrameMoveLoopCount();
    extern "C" int GetFps();

    extern "C" void SetXGSDK(IXGSDKInterface* piXGSDK);

    extern NSKBase::KTimer g_sLogicTimer;
    extern NSKBase::KTimer g_sRenderTimer;

    extern "C" KEngineOptions_Logic* GetEngineOptions_Logic();
}; // namespace NSEngine

namespace NSRender
{
    extern "C" KEngineOptions_Render* GetEngineOptions_Render();
}; // namespace NSRender

extern "C" KEngineOptions_mutex* GetEngineOptions_mutex();
extern "C" KEngineOptions_FileConfig* GetEngineOptions_FileConfig();

#ifdef _DEBUG
#define PROFILER_BEGIN(name) double _currentTime##name = KEnginePerformance::TimeGetTime()
#define PROFILER_END(name)   NSEngine::GetEnginePerformance()->name = static_cast<float>(KEnginePerformance::TimeGetTime() - _currentTime##name)
#define PROFILER_CLEAR(name) NSEngine::GetEnginePerformance()->name = 0;
#else
#define PROFILER_BEGIN(name)
#define PROFILER_END(name)
#define PROFILER_CLEAR(name)
#endif

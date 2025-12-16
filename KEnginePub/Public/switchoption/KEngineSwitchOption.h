#pragma once

#include <unordered_map>
#include <vector>
#include "Engine/KUniqueString.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "KEnginePub/Public/IKHeader.h"
#include <string>

// Options的定义
// Animation
struct AnimationOption
{
    BOOL bEnableSecondOrderSmooth       = FALSE;
    BOOL bEnablePositionSmooth          = FALSE;
    BOOL bEnableRotationSmooth          = FALSE;
    BOOL bEnableAnimationUpdateCallback = TRUE;
    BOOL bEnableAnimationFusion         = TRUE;
};

// Post Render
struct PostRenderOption
{
    BOOL bEnablePostRender = FALSE;
    BOOL bEnableDof        = FALSE;
    BOOL bEnableBloom      = FALSE;
    BOOL bLightOcclusion   = FALSE;
    BOOL bLightShaftBloom  = FALSE;

    BOOL bWantEnableSSAO = FALSE;
    BOOL bWantEnableSSGI = FALSE;
    BOOL bWantEnableTAA  = FALSE;
    BOOL bEanbleDofBokeh = FALSE;
    BOOL bWantEnableSSR  = FALSE;

    BOOL bEnableSSPR                = FALSE;
    BOOL bSimpleSSR                 = TRUE;
    BOOL bRenderShockWave           = FALSE;
    BOOL bHeightFog                 = FALSE;
    // bEnableTAA 期望在绘制Begin/End之间是一个稳定数值，逻辑随时修改bWantEnableTAA, KRenderVK::BeginRender再应用到bEnableTAA
    BOOL bEnableFXAA                = FALSE;
    BOOL bEnableCAS                 = FALSE;
    BOOL bEnableFSR                 = FALSE;
    BOOL bShockWave                 = FALSE;
    BOOL bEnableVignette            = FALSE;
    BOOL bEnableDithering           = FALSE;
    BOOL bEnableTAASharp            = FALSE;
    BOOL bEnableGrain               = FALSE;
    BOOL bEnableChromaticAberration = FALSE;
    BOOL bEnableRayMarchFog         = TRUE;
    BOOL bUseDeinterleave           = FALSE;

    // WantEnableXXX 赋值，内部使用，不提供Get/Set
    BOOL bEnableMSAA = FALSE; // 目前看MSAA 没用了
    BOOL bEnableTAA  = FALSE;
    BOOL bEnableSSAO = FALSE;
    BOOL bEnableSSGI = FALSE;
    BOOL bEnableSSR  = FALSE;
};

// 个人名片不再需要alpha了，盲盒挂件可能使用
// enum class SnapShotFlow : uint8_t
// {
//     Stop   = 0,
//     Start  = 1,
//     Snap   = 2,
//     Finish = 3,
// };

// EngineBaseOption
struct EngineModuleOption
{
    BOOL bEnableSimpleJobSystem = TRUE;
    BOOL bEnableVT                  = FALSE;
    BOOL bEnableDepthPrePass        = TRUE;
    BOOL bEnableIGDRenderCmdQueue   = TRUE;
    BOOL bRenderTerrain             = FALSE;
    BOOL bRenderPlant               = FALSE;
    BOOL bRenderWater               = FALSE;
    BOOL bRenderDecal               = FALSE;
    BOOL bRenderModel               = FALSE;
    BOOL bRenderSkinModel           = FALSE;
    BOOL bRenderSceneActorModel     = FALSE;
    BOOL bRenderGameplayModel       = FALSE;
    BOOL bEnableCameraLight         = FALSE;
    BOOL bEnableSSS                 = FALSE;
    BOOL bRenderSfx                 = FALSE;
    BOOL bEnableOIT                 = FALSE;
    BOOL bRenderOfflineGI           = FALSE;
    BOOL bOfflineGIDynamicRelease   = TRUE;
    BOOL bOpenSpotLightOITShadow    = TRUE;
    BOOL bOpenSpotLightOpaqueShadow = TRUE;
    BOOL bDeferredSpecular          = FALSE;
    // SnapShotFlow eSnapShotPlay              = SnapShotFlow::Stop;    // 个人名片不再需要alpha了，盲盒挂件可能使用

    BOOL bWantSpotLights       = FALSE;
    BOOL bWantPointLights      = FALSE;
    BOOL bWantEnableShadowMask = TRUE;

    // BOOL bOpenShadowMaskBlur = FALSE;

    // bWantEnableXXXX赋值，内部使用
    BOOL bRenderPointLight = FALSE;
    BOOL bEnableShadowMask = FALSE;
    BOOL bEnableSpotLights = FALSE;

    BOOL bEnableMergeParticle = TRUE;
    BOOL bEnableApexClothing  = FALSE;
    BOOL bEnableWaterPhysics  = FALSE;

    BOOL bEnableForceDynamicUBO = FALSE;
    BOOL bUseStencilMask        = TRUE;

    BOOL bEnableMovieTexture         = TRUE;
    BOOL bEnableCommonSpotLightDepth = TRUE;

    BOOL bEnableGpuCullWater = TRUE;

    BOOL bEnableTerrainRealTimeRendder = false;

    BOOL bEnableSpecializeConst = true;

    BOOL bEnableFluxWater = true;

    BOOL bCompressMipTexture    = true;
    BOOL bDisableUICmdSimul     = true;
    BOOL bEnableTrimCommnadPool = true;
    BOOL bEnableWeather         = TRUE;
};

// Foliage
struct FoliageOption
{
    // Foliage
    BOOL bEnableFoliageShadow = TRUE;
    BOOL bRenderPlantTree     = FALSE;
    BOOL bRenderPlantGrass    = FALSE;
};

// EngineOptions对外接口
class KEngineSwitchOption
{
public:
    KEngineSwitchOption();

    BOOL InitSwitchConfig(const char* pcszGpu); // 配置表只缓存本机型设备相关的，不相关的不加载
                                                // Get
    BOOL GetEnableAnimationFusion();
    BOOL GetEnablePositionSmooth();
    BOOL GetEnableRotationSmooth();
    BOOL GetRenderTerrain();
    BOOL GetEnableCameraLight();
    BOOL GetEnableBloom();
    BOOL GetEnableDof();
    BOOL GetEnableVignette();
    BOOL GetEnableFSR();
    // SnapShotFlow GetPlayerSnapShotStatus();  // 个人名片不再需要alpha了，盲盒挂件可能使用

    BOOL GetEnableDofBokeh();

    BOOL GetEnableDepthPrePass();
    BOOL GetEnableIGDRenderCmdQueue();
    BOOL GetRenderPlant();
    BOOL GetRenderWater();
    BOOL GetRenderDecal();
    BOOL GetRenderModel();
    BOOL GetRenderSkinModel();

    BOOL GetEnableSSS();
    BOOL GetRenderSfx();
    BOOL GetEnableMergeParticle();
    BOOL GetEnableOIT();
    BOOL GetEnableOfflineGI();
    BOOL GetEnableOfflineGIDynamicRelease();

    BOOL GetOpenSpotLightOITShadow();
    BOOL GetOpenSpotLightOpaqueShadow();

    BOOL GetEnableGpuCullWater();

    BOOL GetEnableFoliageShadow();
    BOOL GetRenderPlantTree();
    BOOL GetRenderPlantGrass();

    BOOL GetEnablePostRender();

    BOOL GetLightOcclusion();
    BOOL GetLightShaftBloom();
    BOOL GetRenderShockWave();
    BOOL GetHeightFog();
    BOOL GetEnableFXAA();
    BOOL GetEnableCAS();
    BOOL GetShockWave();
    BOOL GetEnableDithering();
    BOOL GetEnableGrain();
    BOOL GetEnableChromaticAberration();
    BOOL GetEnableRayMarchFog();
    BOOL GetEnableTAASharp();
    BOOL GetUseDeinterleave();

    BOOL GetEnableSecondOrderSmooth();
    BOOL GetEnableAnimationUpdateCallback();

    BOOL GetEnablePointLights();
    BOOL GetEnableSpotLights();
    BOOL GetEnableShadowMask();

    BOOL GetEnableSSAO();
    BOOL isEnableForwardPlus();
    BOOL GetEnableTAA();

    BOOL GetEnableSSGI();

    BOOL GetEnableForceDynamicUBO();

    BOOL GetEnableSSPR();
    BOOL GetSimapleSSRFlag();

    BOOL GetDeferredSpecular();

    BOOL GetRenderSceneActorModel();
    BOOL GetRenderGameplayModel();

    BOOL GetEnableApexClothing();
    BOOL GetEnableWaterPhysics();

    BOOL GetUseStencilMask();

    BOOL GetEnableMovieTexture();
    BOOL GetEnableCommonSpotLightDepth();

    BOOL GetEnableSpecializeConst();
    BOOL GetEnableWeather();

    // Set
    void SetEnableFXAA(BOOL bValue);
    void SetEnableCAS(BOOL bValue);
    void SetEnableFSR(BOOL bValue);

    void SetEnableDepthPrePass(BOOL bValue);
    void SetEnableIDGRenderCmdQueue(BOOL bValue);

    void SetRenderTerrain(BOOL bValue);

    void SetRenderPlant(BOOL bValue);

    void SetRenderWater(BOOL bValue);

    void SetRenderDecal(BOOL bValue);
    void SetRenderModel(BOOL bValue);
    void SetRenderSkinModel(BOOL bValue);

    void SetRenderSceneActorModel(BOOL bValue);
    void SetRenderGameplayModel(BOOL bValue);

    void SetEnableCameraLight(BOOL bValue);

    void SetEnableSSS(BOOL bValue);
    void SetRenderSfx(BOOL bValue);
    void SetEnableOIT(BOOL bValue);
    void SetEnableOfflineGI(BOOL bValue);
    void SetEnableOfflineGIDynamicRelease(BOOL bValue);
    void SetEnableSpecializeConst(BOOL bValue);

    void SetOpenSpotLightOITShadow(BOOL bValue);
    void SetOpenSpotLightOpaqueShadow(BOOL bValue);

    void SetEnableGpuCullWater(BOOL bValue);

    void SetEnableFoliageShadow(BOOL bValue);
    void SetRenderPlantTree(BOOL bValue);
    void SetRenderPlantGrass(BOOL bValue);

    void SetEnablePostRender(BOOL bValue);

    void SetEnableDof(BOOL bValue);
    void SetEnableBloom(BOOL bValue);
    void SetLightOcclusion(BOOL bValue);
    void SetLightShaftBloom(BOOL bValue);
    void SetRenderShockWave(BOOL bValue);
    void SetHeightFog(BOOL bValue);
    void SetEnableVignette(BOOL bValue);
    void SetEnableDithering(BOOL bValue);
    void SetEnableGrain(BOOL bValue);
    void SetEnableChromaticAberration(BOOL bValue);
    void SetEnableRayMarchFog(BOOL bValue);
    void SetEnableDofBokeh(BOOL bValue);

    void SetEnableTAASharp(BOOL bValue);

    void SetUseDeinterleave(BOOL bValue);

    void SetEnableSecondOrderSmooth(BOOL bValue);
    void SetEnablePositionSmooth(BOOL bValue);
    void SetEnableRotationSmooth(BOOL bValue);

    void SetEnableAnimationUpdateCallback(BOOL bValue);
    void SetEnableAnimationFusion(BOOL bValue);

    void SetEnablePointLights(BOOL bValue);
    void SetEnableSSAO(BOOL bValue);

    void SetEnableTAA(BOOL bValue);

    void SetEnableSSGI(BOOL bValue);

    void SetEnableSpotLights(BOOL bValue);
    void SetEnableShadowMask(BOOL bValue);

    void SetSSPR(BOOL flag);
    void SetSimapleSSRFlag(BOOL flag);

    void SetShockWave(BOOL bValue);

    // function
    void setEnableForwardPlus(bool isEnable);

    void SetEnableApexClothing(BOOL bValue);
    void SetEnableWaterPhysics(BOOL bValue);
    void SetUseStencilMask(BOOL bValue);

    void SetEnableMovieTexture(BOOL bValue);
    void SetEnableCommonSpotLightDepth(BOOL bValue);
    void SetEnableWeather(BOOL bValue);

    void SetEnableTerrainRealTimeRender(BOOL bValue);
    BOOL GetEnableTerrainRealTimeRender();

    BOOL NotRom() { return bNotRom; }

    void SetEnableSSR(BOOL bValue);
    BOOL GetEnableSSR();

    void SetEnableVT(BOOL bValue);
    BOOL GetEnableVT();

    BOOL GetEnableSimpleJobSystem();

    void SetEnableFluxWater(BOOL bValue);
    BOOL GetEnableFluxWater();

    BOOL GetCompressMipTexture();
    BOOL GetDisableUICmdSimul();
    BOOL GetEnableTrimCommandPool();

private:
    typedef std::unordered_map<KUniqueStr, bool> SwitchBlockMap;

    std::mutex                                   m_mtxSwitch;
    SwitchBlockMap                               m_mapSwitch;
    std::string                                  m_strDeviceModel;
    std::string                                  m_strCpuDevice;
    std::string                                  m_strGpuDevice;

private:
    // 变量部分，控制可以热更新的开关，功能性的
    BOOL bNotRom = TRUE; // 独立不分类

    AnimationOption                     animationOptions;
    PostRenderOption                    postRenderOptions;
    EngineModuleOption                  engineModuleOptions;
    FoliageOption                       foliageOptions;

public:
    AnimationOption*                     GetNativeAnimationOptions() { return &animationOptions; }
    PostRenderOption*                    GetNativePostRenderOptions() { return &postRenderOptions; }
    EngineModuleOption*                  GetNativEngineModuleOptions() { return &engineModuleOptions; }
    FoliageOption*                       GetNativeFoliageOptions() { return &foliageOptions; }

    void SetNotRom(BOOL bVal) { bNotRom = bVal; }
    BOOL GetNotRom() { return bNotRom; }

private:
    int GetSwitchOpen(KUniqueStr pcszSwitchName); // 已经缓存了设备相关的，所以直接level和modulename
};

namespace NSEngine
{
    extern "C" KEngineSwitchOption*                 GetEngineSwitchOptions();
    extern "C" AnimationOption*                     GetAnimationOptions();
    extern "C" EngineModuleOption*                  GetEngineModuleOptions();
    extern "C" PostRenderOption*                    GetPostRenderModuleOptions();
    extern "C" FoliageOption*                       GetFoliageOptions();
} // namespace NSEngine

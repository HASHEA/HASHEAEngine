#pragma once

#include <cstdint>
#include <algorithm>

enum class EX3DDeviceNotSupportCode
{
    None                                   = 0,
    MaxPerStageDescriptorSampledImagesLE16 = 1,
};

enum class GFX_API : int
{
    Unknown = 0,
    GFX_DX12_API,
	GFX_VULKAN_API,	
    GFX_PSGNM_API,
	GFX_METAL_API
};

enum class EPlatform : int
{
    Desktop = 1,
    Mobile = 2,
    Win = 4,
    Mac = 8,
    IOS = 16,
    Android = 32,
    Linux = 64,
    HarmonyOS = 128,
    PS4 = 256,
    PS5 = 512,
    PS6 = 1024,
    XBOX = 2048,
    SWITCH1 = 4096,
    SWITCH2 = 8192
};

struct DrvOption
{
    static char szPlatform[32];
    static char szProductor[128];
    static char szGPU[128];
    static int  bDebugShaderReflector;
    static int  bEnableSpirvCrossReflector;
        
    static int  nGfx_api_version_Part0;
    static int  nGfx_api_version_Part1;
    static int  nGfx_api_version_Part2;

    static int bVKRenderDocDebugMarkerON;
    static int bVKValidateEnable;
    static int unityPlugins;
    static int bIsInitRenderMode;
    static int bSupportB10G11R11;
    static int bSupportR32Blend;
    static int maxTexelBufferElements;
    static int bSupportR32Linear;
    static int bSupportFillModeNonSolid;
    static int bSupportPipelineStatisticsQuery;

    static int bIsMaliGPU;
    static int bIsIntelGPU;
    static int nDeviceNumber;
    static int nPlatformNo;
    static int bIsAdrenoGPU;
    static int bIsPvr;
    static int uMainCommandCount;
    static int bSupportDynamicUBO;
    static int bSupportDynamicSSBO;
    static int bDynamicVertexBuffer;
	static int bDynamicIndexBuffer;
    static int bForceEnableDebugUtileExtension;
    static int nSwapChainCount;
    static int bSupportStoreOpNone;

    static unsigned int vendorId;
    static unsigned int deviceId;
    static unsigned int driverVersion;

    static int uSwapchainMainCommandMutiple;
    static int bSwapChainSafeLoopRelease;

    static int bDiscreteGpu;
    static float fLocalGpuMemoryGB;
    
    static int bSupportHostCoherentCached;
    //for vma
	static int bSupportGetMemoryRequirement2;
	static int bSupportDedicatedAllocation;
	static int bSupportMemoryBudget;

    static int bReversePerspectiveDepthZ;
    static int bReverseOrthoDepthZ;
    static uint32_t uVulkanVersion;
    
#ifndef _WIN32
    static int nOriginalResolutionWidth;
    static int nOriginalResolutionHeight;
	static int nSwapchainWidth;
	static int nSwapchainHeight;
#endif

    static int bVertPosSingle;
    static int bLogShader;

    //显存分配器的选择，三选一，pc，android选第一个，pc1G独显选第二个，苹果设备选第三个
    static int bX3D_VK_USE_VMA;
    static int bX3D_VK_USE_CUSTOM_ALLOCATOR;
    static int bX3D_VK_USE_DEFAULT_ALLOCATOR;

    static int bCompressMipTexture;

    static int IsNeedGammaAdjust();
    static void SetProductor(const char * pProductor);
    static void Init();
    static int IsWin32IntegratedGPU();
    
    static int bSupportD24S8;
    static int bSupportD24S8OptimalLinear;

	static int bSupportD32S8;
	static int bSupportD32S8OptimalLinear;    

    static int bMacroToSpicalizationConstantsEnable;
    static int bWeatherOn;
    static int bHomeLandFix0;//temp 外网跑稳后去掉

    static int bEnableAndroidCamera;
    static int bCheckYCBCRSupported;
    
	static int bSupportSampledImageGreaterThan16;
    static int bEnableMSAA;
    static unsigned int uMSAAQulity;
    static int bPrinfDebugInfo;
    static int GetDeviceNotSupportCode();  
	static int bSupportBindless;
    static int bEnableBindless;
	static int bSupportDeviceAddress;
	static int bSupportRayTracing;
	static int bSupportRayQuery;
    static const char *GetApiName();
    static int bEnableRayTracing;
    static int bEnableRayTracingValidation;
    static int bEnableRayQuery;
    static int bEnableRayTracingReflection;
    static int bSupportShaderAtomicInt64;
    static int bHLSLFunction_NoInline;

    static int bEnableFrameGraph;
    static int bForceAsyncTaskManagerOneThread;

    static int bUseLandscapeRealTimeBake; // 是否启用地形实时烘焙渲染
    static int bUseLandscapeOfflineBakeData; // 开启使用离线烘焙数据，用于远景LOD
    static int bDynamicBufferFrameSliceLimit; //实验性的，默认是限制数量的，不确定手机swapchain imagebuffer数量超级多比如联发科10多个的情况下会不会有问题，如果有问题就得关闭这个
    static int bEnableShaderSamplerStateFix;
    static int bEnableAfterMathSpirvDebug;

    static bool bEnableVKValidateFeature;
    static bool bEnableNsightAftermath;
    static bool bEnableVKDebugPrintf;

    static bool bEnableGBufferUAV;
    static bool bEnableTestCsWriteToGBuffer;

    static void SetRenderApi(GFX_API api);
    static GFX_API GetRenderApi();

    static bool IsDeferRender();
    static bool IsForceVGRenderDirected();
    static bool IsRenderLandscapePC();
    static bool IsEnableInstanceGPUDriven();
};

#define MERGE_UNIFORM_BINDING  1

#define LOW_UNDER_1GB_VMEM 1.2f
#define LOW_UNDER_2GB_VMEM 2.2f
#define LOW_UNDER_4GB_VMEM 4.2f
#define LOW_UNDER_6GB_VMEM 6.2f
#define LOW_UNDER_8GB_VMEM 8.2f

#define MAX_SWAP_CHAIN_COUNT        5

#define DELAY_RELEASE_FRAME_COUNT   ((DrvOption::nSwapChainCount * 2 > 12)?(DrvOption::nSwapChainCount * 2):(12))

#define USE_VK_1_4 0

#define IS_RAY_TRACING_ENABLED (DrvOption::bEnableRayTracing && DrvOption::bSupportRayTracing)
#define IS_RAY_QUERY_ENABLED (DrvOption::bEnableRayQuery && DrvOption::bSupportRayQuery)
#define IS_BINDLESS_ENABLED (DrvOption::bSupportBindless && DrvOption::bEnableBindless)

inline int IsInverseV()
{
	//return DrvOption::bGLON;
//#ifdef _WIN32
//  return false;
//#else
//  return true;
//#endif
    return false;
}

inline int IsUnityPlugins()
{
    return DrvOption::unityPlugins;
}
int GetCurrentPlatform();

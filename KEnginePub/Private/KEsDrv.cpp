#include "KEnginePub/Public/KEsDrv.h"
#include "KBase/Public/io/KIni.h"
#include "KEnginePub/Public/IGFX_Public.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#ifndef VK_MAKE_VERSION
#define VK_MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))
#endif

// shader 调试的时候，可以自行开启，防止函数内联优化
int DrvOption::bHLSLFunction_NoInline = false;
// 测试FrameGraph开这个
int DrvOption::bEnableFrameGraph      = false;

namespace
{
    bool g_bRenderApiInited{ false };

    GFX_API g_eRenderApi{ GFX_API::Unknown };
    bool g_bIsDeferRender{ true };
    bool g_bForceVGRenderDirected{ false };
    bool g_bRenderLandscapePC{ true };
    bool g_bEnableInstanceGPUDriven{ false };

    void LoadDrvConfig()
    {
        bool bResult = false;
        KIni sConfigIni;

        GFX_API eRenderApi = GFX_API::Unknown;
        const char* pcszKey = nullptr;

        bool bRetCode = sConfigIni.OpenIniFile("config.ini");
        KGLOG_ASSERT_EXIT(bRetCode);

#if defined(_WIN32)
        pcszKey = "Win";
#elif defined(__ANDROID__)
        pcszKey = "Android";
#elif defined(__APPLE__)
        pcszKey = "iOS"
#elif defined(__OHOS__)
        pcszKey = "OHOS"
#endif
        KGLOG_ASSERT_EXIT(pcszKey);

        eRenderApi = (GFX_API)sConfigIni.ReadInt("Engine_RenderApi", pcszKey, (int)GFX_API::Unknown);
        KGLOG_ASSERT_EXIT(eRenderApi != GFX_API::Unknown);

        bResult = true;
    Exit0:
        DrvOption::SetRenderApi(bResult ? eRenderApi : GFX_API::GFX_VULKAN_API);
        sConfigIni.CloseIniFile();
        return;
    }
}

void DrvOption::SetRenderApi(GFX_API eRenderApi)
{
    ASSERT(!g_bRenderApiInited);
    ASSERT(g_eRenderApi == GFX_API::Unknown);
    if (g_eRenderApi != GFX_API::Unknown)
    {
        ASSERT(false);
        return;
    }

    switch (eRenderApi)
    {
    case GFX_API::GFX_DX12_API:
        {
    #if !defined(_WIN32)
            ASSERT(false);
            return;
    #endif
            g_bIsDeferRender = true;
            g_bEnableInstanceGPUDriven = false;
        }
        break;
    case GFX_API::GFX_VULKAN_API:
        {
            g_bEnableInstanceGPUDriven = true;
        }
        break;
    case GFX_API::GFX_PSGNM_API:
        {
            // TODO
            ASSERT(false);
            return;
        }
        break;
    case GFX_API::GFX_METAL_API:
        {
            // TODO
            ASSERT(false);
            return;
        }
        break;
    default:
        {
            ASSERT(false);
            return;
        }
        break;
    }

    g_bForceVGRenderDirected = (g_bIsDeferRender ? false : true);
    g_bRenderLandscapePC = g_bIsDeferRender ? true : false;

    g_eRenderApi = eRenderApi;
    g_bRenderApiInited = true;

    gfx::InitDefaultDepthStencilFormat();
}

GFX_API DrvOption::GetRenderApi()
{
    ASSERT(g_eRenderApi != GFX_API::Unknown);
    return g_eRenderApi;
}

bool DrvOption::IsDeferRender()
{
    ASSERT(g_bRenderApiInited);
    return g_bIsDeferRender;
}

bool DrvOption::IsForceVGRenderDirected()
{
    ASSERT(g_bRenderApiInited);
    return g_bForceVGRenderDirected;
}

bool DrvOption::IsRenderLandscapePC()
{
    ASSERT(g_bRenderApiInited);
    return g_bRenderLandscapePC;
}

bool DrvOption::IsEnableInstanceGPUDriven()
{
    ASSERT(g_bRenderApiInited);
    return g_bEnableInstanceGPUDriven;
}

int DrvOption::bDebugShaderReflector      = false;
int DrvOption::bEnableSpirvCrossReflector = true;

// printf 强制输出到控制台的一些临时代码的开关
int DrvOption::bPrinfDebugInfo = 1;

int DrvOption::nGfx_api_version_Part0 = 0;
int DrvOption::nGfx_api_version_Part1 = 0;
int DrvOption::nGfx_api_version_Part2 = 0;

int DrvOption::nDeviceNumber   = 0;
int DrvOption::nPlatformNo     = 0;
int DrvOption::nSwapChainCount = 3;

// 主要是mali 的gpu，基本都不支持，导致VK_POLYGON_MODE_POINT and VK_POLYGON_MODE_LINE这两个模式都无效
int DrvOption::bSupportFillModeNonSolid = true;

int DrvOption::bSupportPipelineStatisticsQuery = true;

#ifndef _WIN32
int DrvOption::nOriginalResolutionHeight = 0;
int DrvOption::nOriginalResolutionWidth  = 0;
int DrvOption::nSwapchainWidth           = 0;
int DrvOption::nSwapchainHeight          = 0;
#endif

int   DrvOption::bIsMaliGPU        = false;
int   DrvOption::bIsAdrenoGPU      = false;
int   DrvOption::bIsPvr            = false;
int   DrvOption::bIsIntelGPU       = false;
char  DrvOption::szProductor[]     = {0};
char  DrvOption::szGPU[]           = {0};
int   DrvOption::bDiscreteGpu      = false;
float DrvOption::fLocalGpuMemoryGB = 0.0f;

int DrvOption::bSupportHostCoherentCached    = false;
// for VMA
int DrvOption::bSupportGetMemoryRequirement2 = false;
int DrvOption::bSupportDedicatedAllocation   = false;
int DrvOption::bSupportMemoryBudget          = false;

unsigned int DrvOption::vendorId      = 0;
unsigned int DrvOption::deviceId      = 0;
unsigned int DrvOption::driverVersion = 0;

int DrvOption::bSupportD24S8              = true;
int DrvOption::bSupportD24S8OptimalLinear = true;

int DrvOption::bSupportD32S8              = true;
int DrvOption::bSupportD32S8OptimalLinear = true;

// 查多线程任务的时候设置为true,不然断点调试跑不了
int DrvOption::bForceAsyncTaskManagerOneThread = false;

int DrvOption::IsWin32IntegratedGPU()
{
#ifdef _WIN32
    return !DrvOption::bDiscreteGpu;
#else
    return false;
#endif
}

// 0是关闭maincommandcount的限制，一般开到3就够用了
#if 0
int DrvOption::uMainCommandCount = 3; //mail需要3个
#else
int DrvOption::uMainCommandCount = 0; // vks::vkWaitForFences会每隔3帧卡顿20~30多ms，所以修改成0
#endif

#ifdef _DEBUG
int DrvOption::bVKRenderDocDebugMarkerON = false;
#else
int DrvOption::bVKRenderDocDebugMarkerON = false;
#endif
int DrvOption::bSupportB10G11R11 = true;

int DrvOption::bSupportR32Blend  = false;
int DrvOption::bSupportR32Linear = true;

int DrvOption::bSupportDynamicUBO = true;

int DrvOption::bSupportDynamicSSBO = true;

int DrvOption::bDynamicVertexBuffer = false;
int DrvOption::bDynamicIndexBuffer  = false;

#ifdef _WIN32
int DrvOption::uSwapchainMainCommandMutiple = 2;
int DrvOption::bSwapChainSafeLoopRelease    = true;
#elif defined(__ANDROID__)
int DrvOption::uSwapchainMainCommandMutiple = 2;
int DrvOption::bSwapChainSafeLoopRelease    = true;
#elif defined(__APPLE__)
#if defined(__MACOS__)
int DrvOption::uSwapchainMainCommandMutiple = 2;
int DrvOption::bSwapChainSafeLoopRelease    = true;
#else
int DrvOption::uSwapchainMainCommandMutiple = 2;
int DrvOption::bSwapChainSafeLoopRelease    = true;
#endif
#elif defined(__OHOS__)
int DrvOption::uSwapchainMainCommandMutiple = 2;
int DrvOption::bSwapChainSafeLoopRelease    = true;
#else
int DrvOption::uSwapchainMainCommandMutiple = 2;
int DrvOption::bSwapChainSafeLoopRelease    = true;
#endif

int DrvOption::bReversePerspectiveDepthZ = true;
int DrvOption::bReverseOrthoDepthZ       = false;

uint32_t DrvOption::uVulkanVersion = VK_MAKE_VERSION(1, 1, 0);

int DrvOption::bVertPosSingle = true;

int DrvOption::bSupportStoreOpNone    = FALSE;
int DrvOption::maxTexelBufferElements = 1 << 27;

int DrvOption::bCompressMipTexture = true;

int DrvOption::bSupportBindless          = FALSE;
int DrvOption::bEnableBindless           = FALSE;

int DrvOption::bSupportDeviceAddress     = TRUE;
int DrvOption::bSupportRayTracing        = TRUE;
int DrvOption::bSupportRayQuery          = TRUE;
int DrvOption::bEnableRayTracing         = TRUE;
int DrvOption::bEnableRayQuery           = TRUE;
int DrvOption::bEnableRayTracingReflection = TRUE;
int DrvOption::bEnableRayTracingValidation = FALSE;
int DrvOption::bSupportShaderAtomicInt64 = FALSE;

int DrvOption::bUseLandscapeRealTimeBake = FALSE;
int DrvOption::bUseLandscapeOfflineBakeData = FALSE;

int DrvOption::bDynamicBufferFrameSliceLimit = TRUE;
#ifdef __APPLE__
// 苹果平台用默认metal分配器就可以了，无需额外的分配器
int DrvOption::bX3D_VK_USE_VMA               = false;
int DrvOption::bX3D_VK_USE_CUSTOM_ALLOCATOR  = false;
int DrvOption::bX3D_VK_USE_DEFAULT_ALLOCATOR = true;
#else
// 非苹果平台默认选用VMA，但PC 1G独显得用bVMEM_UseCustomAllocator，会在初始化device的时候进行修改
int DrvOption::bX3D_VK_USE_VMA               = true;
int DrvOption::bX3D_VK_USE_CUSTOM_ALLOCATOR  = false;
int DrvOption::bX3D_VK_USE_DEFAULT_ALLOCATOR = false;
#endif

// 开启这个验证会降低一些性能，但建议debug下还是开启来，方便vulkan的bug
#if defined(_DEBUG)
int DrvOption::bVKValidateEnable               = true;
// 低配不支持vulkan1.1的设备debug下可能会宕机，那么这个要关掉
int DrvOption::bForceEnableDebugUtileExtension = true;
int DrvOption::bLogShader                      = true;
#else
int DrvOption::bVKValidateEnable               = false;
int DrvOption::bForceEnableDebugUtileExtension = false;
int DrvOption::bLogShader                      = false;
#endif

bool DrvOption::bEnableVKValidateFeature = false;
bool DrvOption::bEnableVKDebugPrintf = false;  // DrvOption::bVKValidateEnable 启用后此选项才有效

#if defined(_WIN32) && defined(_DEBUG)
int DrvOption::bEnableAfterMathSpirvDebug = true;
bool DrvOption::bEnableNsightAftermath = false;
#else
int DrvOption::bEnableAfterMathSpirvDebug = false;
bool DrvOption::bEnableNsightAftermath = false;
#endif

#if UNITY_PLUGINS
int DrvOption::unityPlugins = 1;
#else
int DrvOption::unityPlugins = 0;
#endif

int          DrvOption::bMacroToSpicalizationConstantsEnable = true;
int          DrvOption::bWeatherOn                           = false;
int          DrvOption::bHomeLandFix0                        = TRUE;
int          DrvOption::bEnableMSAA                          = false;
unsigned int DrvOption::uMSAAQulity                          = 0;

#if defined(__ANDROID__)
int DrvOption::bEnableAndroidCamera = true;
#else
int DrvOption::bEnableAndroidCamera = false;
#endif

int DrvOption::bSupportSampledImageGreaterThan16 = true;
int DrvOption::bCheckYCBCRSupported              = true;
int DrvOption::bEnableShaderSamplerStateFix = false;

bool DrvOption::bEnableTestCsWriteToGBuffer = false;

bool DrvOption::bEnableGBufferUAV = false || DrvOption::bEnableTestCsWriteToGBuffer;

int DrvOption::IsNeedGammaAdjust()
{
    static int needAdjust = -1;
    if (needAdjust == -1)
    {
        needAdjust = false;
        if (strcmp(DrvOption::szProductor, "oculus") == 0)
        {
            needAdjust = true;
        }
    }
    return needAdjust;
}

void DrvOption::SetProductor(const char* pProductor)
{
    if (pProductor)
    {
        strncpy(szProductor, pProductor, 128);
    }
}

void DrvOption::Init()
{
    // 读配置表，设置渲染Api 等相关选项

    LoadDrvConfig();

#ifdef __ANDROID__
    char version[16];
    version[0] = '\0';
    __system_property_get("ro.build.version.release", version);
    if (version[0])
    {
        strcat(DrvOption::szPlatform, version);
        uint32_t ulen     = (uint32_t)strlen(DrvOption::szPlatform);
        int      deviceNo = 0;
        for (uint32_t i = 0; i < ulen; ++i)
        {
            if (DrvOption::szPlatform[i] >= '0' && DrvOption::szPlatform[i] <= '9')
            {
                DrvOption::nPlatformNo = atoi(&DrvOption::szPlatform[i]);
                break;
            }
        }
    }
#endif
}


int DrvOption::GetDeviceNotSupportCode()
{
    if (!bSupportSampledImageGreaterThan16)
    {
        // 很抱歉，当前设备不支持运行《剑网3无界》（错误码：%d）
        return (int)EX3DDeviceNotSupportCode::MaxPerStageDescriptorSampledImagesLE16;
    }

    return (int)EX3DDeviceNotSupportCode::None;
}

const char* DrvOption::GetApiName()
{
    const char* pApi = "unknown";
    GFX_API    render_api = DrvOption::GetRenderApi();
    if (render_api == GFX_API::GFX_DX12_API)
    {
        pApi = "dx";
    }
    else if (render_api == GFX_API::GFX_VULKAN_API)
    {
        pApi = "vk";
    }
    return pApi;
}

#ifdef _WIN32
char DrvOption::szPlatform[] = "win";
#elif defined(__ANDROID__) || defined(ANDROID)
char DrvOption::szPlatform[] = "android";
#elif defined(__MACOS__)
char DrvOption::szPlatform[] = "macos";
#elif defined(__APPLE__)
char DrvOption::szPlatform[] = "ios";
#elif defined(__OHOS__)
char DrvOption::szPlatform[] = "harmonyOS";
#elif defined(__linux__)
char DrvOption::szPlatform[] = "linux";
#else
char DrvOption::szPlatform[] = "unknow";
#endif


int GetCurrentPlatform()
{
    int nPlatform = 0;
#if defined(_WIN32)
    nPlatform = (int)EPlatform::Desktop | (int)EPlatform::Win;
#elif defined(__MACOS__)
    nPlatform = (int)EPlatform::Desktop | (int)EPlatform::Mac;
#elif defined(__ANDROID__) || defined(__OHOS__)
    nPlatform = (int)EPlatform::Mobile | (int)EPlatform::Android;
#elif defined(__APPLE__) && !defined(__MACOS__)
    nPlatform = (int)EPlatform::Mobile | (int)EPlatform::IOS;
#else
    nPlatform = (int)EPlatform::Mobile;
#endif // #if defined(_WIN32) || defined(__MACOS__)
    return nPlatform;
}


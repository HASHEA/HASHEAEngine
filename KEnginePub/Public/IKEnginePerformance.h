#pragma once

#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <list>
#include "Engine/KUniqueString.h"
#include "KBase/Public/time/KTimer.h"

// IKResource.h
class IKResource;
enum KRESOURCETYPE : uint32_t;

class KX3DKParticleInfo
{
public:
    uint32_t uPssSize               = 0; // 特效数量
    uint32_t uLauncherSize          = 0; // 普通发射器数量
    uint32_t uMeshLauncherSize      = 0; // 模型粒子发射器数量
    uint32_t uMeshQuoteLauncherSize = 0; // 模型引用发射器数量
    uint32_t uPartileCount          = 0; // 粒子数
    uint32_t uMeshCount             = 0; // 模型粒子数
    uint32_t uMeshVerCount          = 0; // 模型粒子顶点数
    uint32_t uMeshQuoteVerCount     = 0; // 模型引用顶点数
};

struct KGVG_STATS
{
    uint32_t NumTris                    = 0;
    uint32_t NumVerts                   = 0;
    uint32_t NumMainInstancesPreCull    = 0;
    uint32_t NumMainInstancesPostCull   = 0;
    uint32_t NumMainVisitedNodes        = 0;
    uint32_t NumMainCandidateClusters   = 0;
    uint32_t NumPostInstancesPreCull    = 0;
    uint32_t NumPostInstancesPostCull   = 0;
    uint32_t NumPostVisitedNodes        = 0;
    uint32_t NumPostCandidateClusters   = 0;
    uint32_t NumLargePageRectClusters   = 0;
    uint32_t NumMainHWClusters          = 0;
    uint32_t NumMainSWClusters          = 0;
    uint32_t NumPostHWClusters          = 0;
    uint32_t NumPostSWClusters          = 0;
    uint32_t NumTotalDrawImposters      = 0;
    uint32_t NumPrimaryViews            = 0;
    uint32_t NumTotalViews              = 0;
    uint32_t DebugSlot0                 = 0;
    uint32_t DebugSlot1                 = 0;
    NSKMath::KVec4 DebugSlot2;
    NSKMath::KVec4 DebugSlot3;

    void Zero()
    {
        NumTris                         = 0;
        NumVerts                        = 0;
        NumTotalDrawImposters           = 0;

        NumMainInstancesPreCull         = 0;
        NumMainInstancesPostCull        = 0;
        NumMainVisitedNodes             = 0;
        NumMainCandidateClusters        = 0;
        NumMainHWClusters               = 0;
        NumMainSWClusters               = 0;

        NumPostInstancesPreCull         = 0;
        NumPostInstancesPostCull        = 0;
        NumPostVisitedNodes             = 0;
        NumPostCandidateClusters        = 0;
        NumPostHWClusters               = 0;
        NumPostSWClusters               = 0;

        NumPrimaryViews = 0;
        NumTotalViews   = 0;
    }

    void Add(const KGVG_STATS& Others)
    {
        NumTris                     += Others.NumTris;
        NumVerts                    += Others.NumVerts;
        NumTotalDrawImposters       += Others.NumTotalDrawImposters;

        NumMainInstancesPreCull     += Others.NumMainInstancesPreCull;
        NumMainInstancesPostCull    += Others.NumMainInstancesPostCull;
        NumMainVisitedNodes         += Others.NumMainVisitedNodes;
        NumMainCandidateClusters    += Others.NumMainCandidateClusters;
        NumMainHWClusters           += Others.NumMainHWClusters;
        NumMainSWClusters           += Others.NumMainSWClusters;

        NumPostInstancesPreCull     += Others.NumPostInstancesPreCull;
        NumPostInstancesPostCull    += Others.NumPostInstancesPostCull;
        NumPostVisitedNodes         += Others.NumPostVisitedNodes;
        NumPostCandidateClusters    += Others.NumPostCandidateClusters;
        NumPostHWClusters           += Others.NumPostHWClusters;
        NumPostSWClusters           += Others.NumPostSWClusters;

        NumPrimaryViews             += Others.NumPrimaryViews;
        NumTotalViews               += Others.NumTotalViews;
    }
};

struct KGVG_PASS_STATS
{
    uint32_t NumHWRasterPasses = 0;
    uint32_t NumSWRasterPasses = 0;
    uint32_t NumShadingPasses  = 0;

    void Zero()
    {
        NumHWRasterPasses = 0;
        NumSWRasterPasses = 0;
        NumShadingPasses  = 0;
    }

    void Add(const KGVG_PASS_STATS& Others)
    {
        NumHWRasterPasses += Others.NumHWRasterPasses;
        NumSWRasterPasses += Others.NumSWRasterPasses;
        NumShadingPasses  += Others.NumShadingPasses;
    }
};

struct KGVSM_STATS
{
    uint32_t RequestedThisFramePages = 0;
    uint32_t StaticCachedPages = 0;
    uint32_t StaticInvalidatedPages = 0;
    uint32_t DynamicCachedPages = 0;
    uint32_t DynamicInvalidatedPages = 0;
    uint32_t EmptyPages = 0;
    uint32_t NonNaniteInstancesTotal = 0;
    uint32_t NonNaniteInstancesDrawn = 0;
    uint32_t NonNaniteInstancesHzbCulled = 0;
    uint32_t NonNaniteInstancesPageMaskCulled = 0;
    uint32_t NonNaniteInstancesEmptyRectCulled = 0;
    uint32_t NonNaniteInstancesFrustumCulled = 0;
    uint32_t NumPagesToMerge = 0;
    uint32_t NumPagesToClear = 0;
    uint32_t NumHzbPagesBuilt = 0;
    uint32_t AllocatedNew = 0;

    void Zero()
    {
        RequestedThisFramePages = 0;
        StaticCachedPages = 0;
        StaticInvalidatedPages = 0;
        DynamicCachedPages = 0;
        DynamicInvalidatedPages = 0;
        EmptyPages = 0;
        NonNaniteInstancesTotal = 0;
        NonNaniteInstancesDrawn = 0;
        NonNaniteInstancesHzbCulled = 0;
        NonNaniteInstancesPageMaskCulled = 0;
        NonNaniteInstancesEmptyRectCulled = 0;
        NonNaniteInstancesFrustumCulled = 0;
        NumPagesToMerge = 0;
        NumPagesToClear = 0;
        NumHzbPagesBuilt = 0;
        AllocatedNew = 0;
    }

    void Add(const KGVSM_STATS& rhs)
    {
        RequestedThisFramePages += rhs.RequestedThisFramePages;
        StaticCachedPages += rhs.StaticCachedPages;
        StaticInvalidatedPages += rhs.StaticInvalidatedPages;
        DynamicCachedPages += rhs.DynamicCachedPages;
        DynamicInvalidatedPages += rhs.DynamicInvalidatedPages;
        EmptyPages += rhs.EmptyPages;
        NonNaniteInstancesTotal += rhs.NonNaniteInstancesTotal;
        NonNaniteInstancesDrawn += rhs.NonNaniteInstancesDrawn;
        NonNaniteInstancesHzbCulled += rhs.NonNaniteInstancesHzbCulled;
        NonNaniteInstancesPageMaskCulled += rhs.NonNaniteInstancesPageMaskCulled;
        NonNaniteInstancesEmptyRectCulled += rhs.NonNaniteInstancesEmptyRectCulled;
        NonNaniteInstancesFrustumCulled += rhs.NonNaniteInstancesFrustumCulled;
        NumPagesToMerge += rhs.NumPagesToMerge;
        NumPagesToClear += rhs.NumPagesToClear;
        NumHzbPagesBuilt += rhs.NumHzbPagesBuilt;
        AllocatedNew += rhs.AllocatedNew;
    }
};

class KEnginePerformance
{
public:
    int      nLogicFps;
    int      nRenderFps;
    uint32_t dwLoadedModels;
    uint32_t dwVisableModels;
    uint32_t dwDrawCallUICount;
    uint32_t dwOccludeQuerry;
    uint32_t dwOccluded;
    uint32_t dwDrawCallCount;
    uint32_t dwDrawBatchCount;
    uint32_t dwComputeCallCount;
    uint32_t dwDrawCallElement;
    uint32_t dwDrawCallArray;
    uint32_t dwDrawCallElmentInstance;
    uint32_t dwDrawCallArrayInstance;
    uint32_t dwDrawFacesindics;
    int      nSetPassCount;
    int      nGLnSetPassCount;
    int      nShockWavePassCount;

    float postRenderCost;
    float forwardRenderCost;
    float oitCost;
    float shockWaveCost;
    float frameMoveCost;
    float shaderLoadCost;

    // culling result
    uint32_t dwFontCanvas;


    // for autotest
    uint32_t          dwFrameDrawCallCnt;
    uint32_t          dwFrameFaceCnt;
    uint32_t          dwRenderTarget2DSize;
    uint32_t          dwRenderTarget2DCnt;
    uint32_t          dwAllErrorShaderCount;
    uint32_t          dwAllMissingMaterialDefCount;
    float             fGpuUsage;
    bool              bSupportGpuUsage;
    KX3DKParticleInfo particleInfo;

    KEnginePerformance()
    {
        Reset();
        nLogicFps  = 0;
        nRenderFps = 0;

        postRenderCost    = 0;
        forwardRenderCost = 0;
        oitCost           = 0;
        shockWaveCost     = 0;
        frameMoveCost     = 0;

        shaderLoadCost = 0;

        dwFrameDrawCallCnt           = 0;
        dwFrameFaceCnt               = 0;
        dwRenderTarget2DSize         = 0;
        dwRenderTarget2DCnt          = 0;
        dwAllErrorShaderCount        = 0;
        dwAllMissingMaterialDefCount = 0;
        fGpuUsage                    = 0.0f;
        bSupportGpuUsage             = false;

        particleInfo.uPssSize               = 0;
        particleInfo.uLauncherSize          = 0;
        particleInfo.uMeshLauncherSize      = 0;
        particleInfo.uMeshQuoteLauncherSize = 0;
        particleInfo.uPartileCount          = 0;
        particleInfo.uMeshCount             = 0;
        particleInfo.uMeshVerCount          = 0;
        particleInfo.uMeshQuoteVerCount     = 0;
    }

    KEnginePerformance& operator=(const KEnginePerformance& copy)
    {
        nLogicFps                = copy.nLogicFps;
        nRenderFps               = copy.nRenderFps;
        dwLoadedModels           = copy.dwLoadedModels;
        dwVisableModels          = copy.dwVisableModels;
        dwDrawCallUICount        = copy.dwDrawCallUICount;
        dwOccludeQuerry          = copy.dwOccludeQuerry;
        dwOccluded               = copy.dwOccluded;
        dwDrawCallCount          = copy.dwDrawCallCount;
        dwDrawBatchCount         = copy.dwDrawBatchCount;
        dwComputeCallCount       = copy.dwComputeCallCount;
        dwDrawCallElement        = copy.dwDrawCallElement;
        dwDrawCallArray          = copy.dwDrawCallArray;
        dwDrawCallElmentInstance = copy.dwDrawCallElmentInstance;
        dwDrawCallArrayInstance  = copy.dwDrawCallArrayInstance;
        dwDrawFacesindics        = copy.dwDrawFacesindics;
        nSetPassCount            = copy.nSetPassCount;
        nGLnSetPassCount         = copy.nGLnSetPassCount;
        nShockWavePassCount      = copy.nShockWavePassCount;

        postRenderCost    = copy.postRenderCost;
        forwardRenderCost = copy.forwardRenderCost;
        oitCost           = copy.oitCost;
        shockWaveCost     = copy.shockWaveCost;
        frameMoveCost     = copy.frameMoveCost;

        // culling result
        dwFontCanvas = copy.dwFontCanvas;

        dwFrameDrawCallCnt           = copy.dwFrameDrawCallCnt;
        dwFrameFaceCnt               = copy.dwFrameFaceCnt;
        dwRenderTarget2DSize         = copy.dwRenderTarget2DSize;
        dwRenderTarget2DCnt          = copy.dwRenderTarget2DCnt;
        dwAllErrorShaderCount        = copy.dwAllErrorShaderCount;
        dwAllMissingMaterialDefCount = copy.dwAllMissingMaterialDefCount;
        fGpuUsage                    = copy.fGpuUsage;
        bSupportGpuUsage             = copy.bSupportGpuUsage;

        particleInfo.uPssSize               = copy.particleInfo.uPssSize;
        particleInfo.uLauncherSize          = copy.particleInfo.uLauncherSize;
        particleInfo.uMeshLauncherSize      = copy.particleInfo.uMeshLauncherSize;
        particleInfo.uMeshQuoteLauncherSize = copy.particleInfo.uMeshQuoteLauncherSize;
        particleInfo.uPartileCount          = copy.particleInfo.uPartileCount;
        particleInfo.uMeshCount             = copy.particleInfo.uMeshCount;
        particleInfo.uMeshVerCount          = copy.particleInfo.uMeshVerCount;
        particleInfo.uMeshQuoteVerCount     = copy.particleInfo.uMeshQuoteVerCount;

        return *this;
    }

    void Reset()
    {
        dwLoadedModels           = 0;
        dwVisableModels          = 0;
        dwDrawCallUICount        = 0;
        dwDrawCallCount          = 0;
        dwDrawBatchCount         = 0;
        dwComputeCallCount       = 0;
        dwDrawCallElement        = 0;
        dwDrawCallArray          = 0;
        dwDrawCallElmentInstance = 0;
        dwDrawCallArrayInstance  = 0;
        dwDrawFacesindics        = 0;
        dwFontCanvas             = 0;
        dwOccludeQuerry          = 0;
        dwOccluded               = 0;
        nSetPassCount            = 0;
        nGLnSetPassCount         = 0;
        nShockWavePassCount      = 0;

        particleInfo.uPssSize               = 0;
        particleInfo.uLauncherSize          = 0;
        particleInfo.uMeshLauncherSize      = 0;
        particleInfo.uMeshQuoteLauncherSize = 0;
        particleInfo.uPartileCount          = 0;
        particleInfo.uMeshCount             = 0;
        particleInfo.uMeshVerCount          = 0;
        particleInfo.uMeshQuoteVerCount     = 0;
    }

    void ClearResourceCounter()
    {
    }

    static double TimeGetTime()
    {
#ifdef _WIN32
        FILETIME ft;
        double   t;
        GetSystemTimeAsFileTime(&ft);
        /* Windows file time (time since January 1, 1601 (UTC)) */
        t = ft.dwLowDateTime / 1.0e7 + ft.dwHighDateTime * (4294967296.0 / 1.0e7);
        /* convert to Unix Epoch time (time since January 1, 1970 (UTC)) */
        return (t - 11644473600.0);
#else

        struct timeval v;
        gettimeofday(&v, (struct timezone*)NULL);
        /* Unix Epoch time (time since January 1, 1970 (UTC)) */
        return v.tv_sec + v.tv_usec / 1.0e6;
#endif
    }

    static float GetSpeedTreeRealTime();
};


class KX3DResourceMonitor
{
public:
    KX3DResourceMonitor();

    void     Add(IKResource* piResource);
    void     Remove(IKResource* piResource);
    uint32_t GetCountByType(KRESOURCETYPE eResType)
    {
        std::atomic<uint32_t>& uResTypeCount = m_mapType2Count[(int)eResType];
        return uResTypeCount;
    }

private:
    std::mutex                                     m_mtxResource;
    std::unordered_set<IKResource*>                m_setResource;
    std::unordered_map<int, std::atomic<uint32_t>> m_mapType2Count;
};

class KX3DVkBufferMonitor
{
public:
    void UsageBufferCountInc(int nUsage);
    void UsageBufferCountDec(int nUsage);

public:
    std::atomic<int32_t> nVkBufferCount;
    std::atomic<int32_t> nVkAllocBufferSize;

    std::mutex                   m_mtxBufferUsage2Count;
    std::unordered_map<int, int> m_mapBufferUsage2Count;
};

class KX3DVkImageMonitor
{
public:
    void UsageImageCountInc(int nUsage);
    void UsageImageCountDec(int nUsage);

public:
    std::atomic<int32_t> nVkImageCount{0};

    std::mutex                   m_mtxImageUsage2Count;
    std::unordered_map<int, int> m_mapImageUsage2Count;
};

class KX3DCullMonitor
{
public:
    std::atomic<int32_t> nVisitRegionCount{0};

    std::atomic<int32_t> nVisitModelCount{0};
    std::atomic<int32_t> nVisibleModelCount{0};
    std::atomic<int32_t> nVisitFoliageCount{0};
    std::atomic<int32_t> nVisibleFoliageCount{0};

    std::atomic<int32_t> nVisitModelTimeInMS{0};
    std::atomic<int32_t> nVisitFoliageTimeInMS{0};
    std::atomic<int32_t> nVisitNormalTimeInMS{0};
    std::atomic<int32_t> nVisitShadowTimeInMS{0};

    std::atomic<int32_t> nAddedSceneActorCount{0};
    std::atomic<int32_t> nAddedModelCount{0};
    std::atomic<int32_t> nAddedFoliageInstCount{0};

    int32_t nGPUCullCostTimeInMS = 0;

    int32_t nVisibleSFXCount         = 0;
    int32_t nVisibleSimpleModelCount = 0;
    int32_t nVisibleSkinModelCount   = 0;
    int32_t nVisibleSpeedTreeCount   = 0;
    int32_t nVisibleModelSTCount     = 0;
    int32_t nVisiblePointLightCount  = 0;
    int32_t nVisibleAnimModelCount   = 0;

    int32_t nStaticRenderModelCount   = 0;
    int32_t nStaticCullModelCount     = 0;
    int32_t nStaticRenderFoliageCount = 0;
    int32_t nStaticCullFoliageCount   = 0;

    int32_t nMainThreadFrameTimeInMS = 0;

    std::atomic<int32_t> nModelRenderGroupCount{0};
    std::atomic<int32_t> nSubsetRenderGroupCount{0};
    std::atomic<int32_t> nSubsetRenderUnitCount{0};
};

class KX3DSceneMonitor
{
public:
    std::atomic<int32_t> nTerrainLoadSectionCount{0};
    std::atomic<int32_t> nTerrainLoadZoneCount{0};

    std::atomic<int32_t> nActorCount{0};
    std::atomic<int32_t> nActorComponentCount{0};
    std::atomic<int32_t> nRenderProxyCount{0};

    std::atomic<int32_t> nMeshLodDataCount{0};
    std::atomic<int32_t> nVGSceneInstanceProxyCount{0};
    std::atomic<int32_t> nKGFX_MaterialCount{0};

    std::atomic<int32_t> nKCollisionMeshCount{0};
    std::atomic<int32_t> nKMeshCount{0};

    std::atomic<int32_t> nFoliageInstCount{0};
    std::atomic<int32_t> nFoliageInstRefCount{0};

    std::atomic<int32_t> nLoadedPointCloudSectionCount{0};
    std::atomic<int32_t> nRenderPointCloudSectionCount{0};
    std::atomic<int32_t> nLoadedPointCloudMemBytes{0};

    std::atomic<int32_t> nKParticleCount{0};
    std::atomic<int32_t> nKParticleLauncherCount{0};
    std::atomic<int32_t> nKParticleMeshLauncherCount{0};
    std::atomic<int32_t> nKParticleMeshQuoteLauncherCount{0};

    std::atomic<int32_t> nKFlexibleBodyCount{0};
    std::atomic<int32_t> nKBipCount{0};
};

class KX3DAnimationEventMonitor
{
public:
    std::atomic<int32_t> nKG3DAnimationEventHandlerAdapterCount{0};
    std::atomic<int32_t> nSFXTagLifeRangeEventHandlerCount{0};
    std::atomic<int32_t> nSFXTagSceneLifeRangeEventHandlerCount{0};
    std::atomic<int32_t> nTagSFXMotionSwitchEventHandlerCount{0};
    std::atomic<int32_t> nFaceMotionDefaultHandlerCount{0};
    std::atomic<int32_t> nFaceMotionLifeRangeHandlerCount{0};
};

class KX3DKMUIMonitor
{
public:
    uint32_t uDrawCallCount        = 0;
    uint32_t uDrawVertexCount      = 0;
    uint32_t uBakeCmdBufferCount   = 0;
    uint32_t uUnBakeCmdBufferCount = 0;

    uint32_t uCCNodeCount            = 0;
    uint32_t uCCProgramStateCount    = 0;
    uint32_t uVkPipelineCount        = 0;
    uint32_t uVkSecondCmdBufferCount = 0;

    uint32_t uVkDescriptorPoolCount      = 0;
    uint32_t uVkDescriptorSetCount       = 0;
    uint32_t uVkDescriptorSetLayoutCount = 0;

    uint32_t uPipelineLayoutCount = 0;

    KX3DVkBufferMonitor VkBufferMonitor;
    KX3DVkImageMonitor  VkImageMonitor;
};

class KX3DVGMonitor
{
public:
    KGVG_STATS      MainStats;
    KGVG_PASS_STATS MainPassStats;
    KGVG_PASS_STATS OITFirstLayerPassStats;
    KGVG_PASS_STATS OITTransparentLayerPassStats;

    KGVG_STATS      ShadowStats;
    KGVG_PASS_STATS ShadowPassStats[5];
};

class KX3DVSMMonitor
{
public:
    KGVSM_STATS Stats;
};

class KX3DVTMonitor
{
public:
    std::atomic<int32_t> nFVTCount;
};

class KX3DIGDMonitor
{
public:
    std::atomic<int32_t> nThreadWorkerInstanceCount;

    uint32_t uMaxMeshSubsetCount  = 0;
    uint32_t uMeshSubsetLE8Count  = 0;
    uint32_t uMeshSubsetLE16Count = 0;
    uint32_t uMeshSubsetG16Count  = 0;

    uint32_t uTotalSceneObjectAllocCount     = 0;
    uint32_t uTotalSceneViewObjectAllocCount = 0;

    uint32_t uInstanceSceneObjectCount       = 0;
    uint32_t uTriClusterSceneObjectCount     = 0;
    uint32_t uDynamicClusterSceneObjectCount = 0;

    uint32_t uInstanceNormalSceneViewBatchCount           = 0;
    uint32_t uInstanceNormalSceneViewGPUInstanceCount     = 0;
    uint32_t uInstanceNormalSceneViewDrawFacesCount       = 0;
    uint32_t uInstanceNormalSceneViewHizOCCount           = 0;
    uint32_t uInstanceNormalSceneViewRenderBatchCount     = 0;
    uint32_t uInstanceNormalSceneViewSkipRenderBatchCount = 0;

    uint32_t uInstanceShadowSceneViewBatchCount           = 0;
    uint32_t uInstanceShadowSceneViewGPUInstanceCount     = 0;
    uint32_t uInstanceShadowSceneViewDrawFacesCount       = 0;
    uint32_t uInstanceShadowSceneViewHizOCCount           = 0;
    uint32_t uInstanceShadowSceneViewRenderBatchCount     = 0;
    uint32_t uInstanceShadowSceneViewSkipRenderBatchCount = 0;

    uint32_t uInstanceTopSceneViewBatchCount       = 0;
    uint32_t uInstanceTopSceneViewGPUInstanceCount = 0;
    uint32_t uInstanceTopSceneViewDrawFacesCount   = 0;
    uint32_t uInstanceTopSceneViewRenderBatchCount = 0;

    uint32_t uTriClusterNormalSceneViewBatchCount           = 0;
    uint32_t uTriClusterNormalSceneViewGPUInstanceCount     = 0;
    uint32_t uTriClusterNormalSceneViewDrawFacesCount       = 0;
    uint32_t uTriClusterNormalSceneViewHizOCCount           = 0;
    uint32_t uTriClusterNormalSceneViewRenderBatchCount     = 0;
    uint32_t uTriClusterNormalSceneViewSkipRenderBatchCount = 0;

    uint32_t uTriClusterShadowSceneViewBatchCount           = 0;
    uint32_t uTriClusterShadowSceneViewGPUInstanceCount     = 0;
    uint32_t uTriClusterShadowSceneViewDrawFacesCount       = 0;
    uint32_t uTriClusterShadowSceneViewHizOCCount           = 0;
    uint32_t uTriClusterShadowSceneViewRenderBatchCount     = 0;
    uint32_t uTriClusterShadowSceneViewSkipRenderBatchCount = 0;

    uint32_t uDynamicClusterNormalSceneViewObjectCount      = 0;
    uint32_t uDynamicClusterNormalSceneViewFrameObjectCount = 0;
    uint32_t uDynamicClusterNormalSceneViewDrawObjectCount  = 0;

    uint32_t uSceneBlockNormalSceneViewFrameObjectCount    = 0;
    uint32_t uSceneBlockNormalSceneViewPassCullObjectCount = 0;
    uint32_t uSceneBlockNormalSceneViewDrawZoneCount       = 0;
    uint32_t uSceneBlockNormalSceneViewDrawSectionCount    = 0;
    uint32_t uSceneBlockNormalSceneViewDrawRegionCount     = 0;
    uint32_t uSceneBlockNormalSceneViewDispatchXCount      = 0;
    uint32_t uSceneBlockNormalSceneViewDispatchYCount      = 0;

    void FrameResetInstanceNormal()
    {
        uInstanceNormalSceneViewDrawFacesCount       = 0;
        // uInstanceNormalSceneViewHizOCCount           = 0;
        uInstanceNormalSceneViewRenderBatchCount     = 0;
        uInstanceNormalSceneViewSkipRenderBatchCount = 0;
    }

    void FrameResetInstanceShadow()
    {
        uInstanceShadowSceneViewDrawFacesCount       = 0;
        // uInstanceShadowSceneViewHizOCCount           = 0;
        uInstanceShadowSceneViewRenderBatchCount     = 0;
        uInstanceShadowSceneViewSkipRenderBatchCount = 0;
    }

    void FrameResetTriClusterNormal()
    {
        uTriClusterNormalSceneViewDrawFacesCount       = 0;
        // uTriClusterNormalSceneViewHizOCCount           = 0;
        uTriClusterNormalSceneViewRenderBatchCount     = 0;
        uTriClusterNormalSceneViewSkipRenderBatchCount = 0;
    }

    void FrameResetTriClusterShadow()
    {
        uTriClusterShadowSceneViewDrawFacesCount       = 0;
        // uTriClusterShadowSceneViewHizOCCount           = 0;
        uTriClusterShadowSceneViewRenderBatchCount     = 0;
        uTriClusterShadowSceneViewSkipRenderBatchCount = 0;
    }

    void FrameResetDynamicNormal()
    {
        uDynamicClusterNormalSceneViewObjectCount      = 0;
        uDynamicClusterNormalSceneViewFrameObjectCount = 0;
        uDynamicClusterNormalSceneViewDrawObjectCount  = 0;
    }

    void FrameResetSceneBlockNormal()
    {
        uSceneBlockNormalSceneViewFrameObjectCount    = 0;
        uSceneBlockNormalSceneViewPassCullObjectCount = 0;
        uSceneBlockNormalSceneViewDispatchXCount      = 0;
        uSceneBlockNormalSceneViewDispatchYCount      = 0;
    }

    void FrameReset()
    {
        uInstanceSceneObjectCount   = 0;
        uTriClusterSceneObjectCount = 0;
    }
};

class KX3DDrawMonitor
{
private:
    struct tagDrawEvent
    {
        KUniqueStr ustrMeshPath;
        KUniqueStr ustrMtlPath;
        KUniqueStr ustrMtlInstDef;
        int        nInstCount = 0;
        int        nFaces     = 0;
        KUniqueStr ustrLauncher;
    };

    KUniqueStr                m_ustrMainSceneName;
    std::vector<tagDrawEvent> m_vecMainSceneDrawEvent;
    std::string               m_strLastCaptureLogFilePath;

private:
    bool m_bWantToCaptureMainSceneDrawFrame = false;
    bool m_bCapturingMainSceneDrawFrame     = false;

public:
    void        TurnOnMainSceneDrawFrameCapture();
    bool        IsCapturingMainSceneDrawFrame();
    const char* GetLastMainSceneDrawFrameCaptureLogFilePath();
    void        AddMainSceneDrawEvent(KUniqueStr ustrMeshPath, KUniqueStr ustrMtlPath, KUniqueStr ustrMtlInstDef, int nInstCount, int nFaces, const char* pcszLauncher);

public:
    void OnMainSceneDrawFrameBegin(const char* pcszSceneName);
    void OnMainSceneDrawFrameEnd();
};

class KX3DAnimationMonitor
{
public:
    std::atomic<int32_t> nKClipCount{0};
    std::atomic<int32_t> nKAniV1Count{0};
    std::atomic<int32_t> nKAniV2Count{0};
    std::atomic<int32_t> nKAnimationControllerCount{0};
};

class KX3DGraphicsMonitor
{
public:
    std::atomic<int32_t> nKMaterialShaderHolderVKCount{0};
    std::atomic<int32_t> nVkPipelineCount{0};
    std::atomic<int32_t> nVkGraphicsPipelineCount{0};

    std::atomic<int32_t> nVkDescriptorPoolCount{0};
    std::atomic<int32_t> nVkDescriptorSetCount{0};
    std::atomic<int32_t> nVkCmdBufferCount{0};
    std::atomic<int32_t> nVkFrameBufferCount{0};

    std::atomic<int32_t> nVkPrimaryCommandBufferCount{0};
    std::atomic<int32_t> nVkSecondaryCommandBufferCount{0};

    std::atomic<int32_t> nKVulkanShaderStage{0};
    std::atomic<int32_t> nKShaderProgram{0};
    std::atomic<int32_t> nVkShaderModule{0};
};

class KX3DWeatherPerMonitor
{
public: // 数据准确度不是特别重要，并且只有一个地方写，不加atomic,避免锁操作影响性能
    uint32_t uTopDepthRenderTimes          = 0;
    // uint32_t uCurAddAndRemoveObjCount = 0;
    uint32_t uCurTopDepthUpdateObjectCount = 0;
};

class KX3DEngineMonitor
{
public:
    KX3DResourceMonitor       m_sResource;
    KX3DVkBufferMonitor       m_sVkBuffer;
    KX3DCullMonitor           m_sCuller;
    KX3DSceneMonitor          m_sScene;
    KX3DKMUIMonitor           m_sKMUI;
    KX3DVkImageMonitor        m_sVkImage;
    KX3DIGDMonitor            m_sIGDMonitor;
    KX3DAnimationEventMonitor m_sAniEvent;
    KX3DDrawMonitor           m_sDraw;
    KX3DAnimationMonitor      m_sAni;
    KX3DGraphicsMonitor       m_sGraphics;
    KX3DWeatherPerMonitor     m_sWeather;
    KX3DVGMonitor             m_sVGMonitor;
    KX3DVSMMonitor            m_sVSMMonitor;
    KX3DVTMonitor             m_sVTMonitor;

    std::atomic<int32_t> nSceneContainerCount;

    std::atomic<int32_t> nTotalSceneActorProxy;
    std::atomic<int32_t> nTotalLoadedSceneActorProxy;
    std::atomic<int32_t> nSceneActorModelCount;

    std::atomic<int32_t> nAsyncTaskMgrWaitCount;
    std::atomic<int32_t> nAsyncTaskMgrCompleteCount;

    std::atomic<int32_t> nLogicThreadTaskCount;
    std::atomic<int32_t> nRenderThreadTaskCount;

    std::atomic<int32_t> nStreamingTextureCount;
    std::atomic<int32_t> nMaterialLoadTaskCount;
    std::atomic<int32_t> nPipelineLoadTaskCount;
    std::atomic<int32_t> nResourceLoadTaskCount;
    std::atomic<int32_t> nResourceCreatingTaskCountByLogic;
    std::atomic<int32_t> nResourceCreatingTaskCountByRender;
    std::atomic<int32_t> nSceneObjLoadTaskCount;
    std::atomic<int32_t> nSceneObjCreatingTaskCount;

    std::atomic<int32_t> nLoadableCount;
    std::atomic<int32_t> nSceneObjectCount;
    std::atomic<int32_t> nTextureVKCount;
    std::atomic<int32_t> nMtlTechVKCount;

    std::atomic<int32_t> nSceneActorProxyRef1;
    std::atomic<int32_t> nSceneActorProxyRef2;
    std::atomic<int32_t> nSceneActorProxyRef3;

    std::atomic<int32_t> nVkStagingBufferCount;
    std::atomic<int32_t> nVkAllocMemoryCount;
    std::atomic<int32_t> nVkAllocMemorySize;
    std::atomic<int32_t> nVkMapMemoryCount;

    std::atomic<int32_t> nVMemoryNodeCount;
    std::atomic<int32_t> nVMemoryNodeUseNodeCount;

    std::atomic<int32_t> nFrameMeshModelLodSwitchCounter;

public:
    uint32_t GetTextureNumber();
    uint32_t GetMeshNumber();
};

namespace NSEngine
{
    extern "C" KX3DEngineMonitor* GetEngineMonitor();

    extern "C" KEnginePerformance* GetEnginePerformance();
    // 获取上一帧的性能数据
    extern "C" KEnginePerformance* GetLastFramePerformance();
    // 重置性能统计数据（帧结束时调用）
    extern "C" void                ResetEnginePerformance();
    // 采集性能统计数据（每帧结束时调用）
    extern "C" void                CollectEnginePerformance();
    // 输出资源池的数据（文件名）
    extern "C" BOOL                OutputResourePoolInfo();
    // 输出VMA内存分配情况
    extern "C" BOOL                OutputVulkanMemoryAllocInfo();
} // namespace NSEngine

#include "KGraphicDevice.h"
#include "GFXVulkan.h"
#include "Engine/KGLog.h"
#include "KEnginePub/Private/loader/KTexturePool.h"
#include "KVulkanDevice.h"
#include "kVulkanBuffer.h"
#include "KVulkanRenderFrameBuffer.h"
#include "KVulkanCommandBuffer.h"
#include "KEnginePub/Public/switchoption/KEngineSwitchOption.h"
#include "KMaterialSystem/Public/IKMaterialTypes.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KShaderResourceVK.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KEnginePub/Private/recorder/KXGReporter.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEngine/Public/Render/KEngineRender.h"
#include "KEnginePub/Private/comm/KGFX_ShaderHelper.h"
#include "KEnginePub/Private/loader/KGFX_FileTexture.h"
#include "KEnginePub/Private/loader/KGFX_MemTexture.h"
#include "KEnginePub/Private/comm/KGFX_ShaderCombinedResult.h"
#include "KEnginePub/Public/KEsDrv.h"
#include "KGFX_GraphicDeviceVK.h"

//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/KProfileTools.h"

#define SECOND_COMMANDBUFFER_LIFE_TIME 10.0f

namespace gfx
{
    /////////////////////////////////////////////////////////////////////////
    KGraphicDevice::KGraphicDevice()
    {
        // memset(m_pContext, 0, sizeof(KGraphicContext*) * CONTEXT_COUNT);
        m_pComputeQueue          = nullptr;
        m_pGraphicQueue          = nullptr;
        m_pTransferQueue         = nullptr;
        m_swapChainSurfaceFormat = TEX_FORMAT_NONE;

        m_AliveCacheId = 0;
        bInitedGraphic = false;
        for (uint32_t i = 0; i < gfx::CONTEXT_COUNT; ++i)
        {
            m_CacheLifeTime[i]      = SECOND_COMMANDBUFFER_LIFE_TIME;
            m_uBakedDrawCall[i]     = 0;
            m_uUnBakedDrawCall[i]   = 0;
            m_uIndexDrawCount[i]    = 0;
            m_uPointCount[i]        = 0;
            m_uIndirectDrawCount[i] = 0;
            m_pGraphicContext[i]    = nullptr;
        }
        m_bUninitting       = false;
        m_bInited           = false;
    }

    KGraphicDevice::~KGraphicDevice()
    {
        // 这两张特殊全局纹理不受引用技术管理，生命周期和device一样，最终一起强行释放

        // if (!m_mapTexture.empty())
        //{
        //   KGLogPrintf(KGLOG_WARNING,"Some Texture not release,now force delete...");
        //   for (auto it = m_mapTexture.begin(); it != m_mapTexture.end(); ++it)
        //   {
        //       SAFE_DELETE(it->second);
        //   }
        // }
        // for (uint32_t i = 0; i < CONTEXT_COUNT; ++i)
        //{
        //   if (m_pContext[i])
        //   {
        //       KGLogPrintf(KGLOG_WARNING,"renderContext %d not release, now force release ...", i);
        //       SAFE_DELETE(m_pContext[i]);
        //   }
        // }

        SAFE_RELEASE(m_pComputeQueue);
        SAFE_RELEASE(m_pGraphicQueue);
        SAFE_RELEASE(m_pTransferQueue);
    }

    KVulkanGfxQueue* KGraphicDevice::GetGraphicQueue()
    {
        if (!m_pGraphicQueue)
        {
            CreateGraphicQueue(&m_pGraphicQueue);
            SetQueueName(m_pGraphicQueue, "graphic_queue");
        }
        return m_pGraphicQueue;
    }

    KVulkanGfxQueue* KGraphicDevice::GetComputeQueue()
    {
        if (!m_pComputeQueue)
        {
            CreateComputeQueue(&m_pComputeQueue);
            SetQueueName(m_pGraphicQueue, "compute_queue");
        }
        return m_pComputeQueue;
    }

    KVulkanGfxQueue* KGraphicDevice::GetTransferQueue()
    {
        if (!m_pTransferQueue)
        {
            CreateTransferQueue(&m_pTransferQueue);
            SetQueueName(m_pGraphicQueue, "transfer_queue");
        }
        return m_pTransferQueue;
    }

    BOOL KGraphicDevice::CreateBuffer(IKGFX_Buffer** ppBuffer, const KGfxBufferDesc& bufDesc, const void* pData)
    {
        PROF_CPU_DETAIL();
        ASSERT(IsMainThread());

        BOOL              bResult        = false;
        BOOL              bRetCode       = false;
        KVulkanBuffer* pCreatedBuffer = nullptr;

        KGLOG_ASSERT_EXIT(ppBuffer);

        ASSERT(*ppBuffer == nullptr);
        *ppBuffer = nullptr;

        KGLOG_ASSERT_EXIT(bufDesc.uByteWidth != 0);

        KG_PROCESS_ERROR(m_bInited);

        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            // 下面几句配合main.cpp里面的MyAllocHook查buffer分配泄露，超级爽
            // char dbgMsg[128];
            // snprintf(dbgMsg, 128, "%d : %d : %d : %d", bufDesc.eResAccessFlags, bufDesc.uByteWidth, bufDesc.uStructureByteStride, bufDesc.uUsageFlags);
            // SetDbgString(dbgMsg);
            pCreatedBuffer = new KVulkanBuffer;
            KGLOG_ASSERT_EXIT(pCreatedBuffer);

            BOOL bRetCode = pCreatedBuffer->Create(bufDesc, pData);
            KGLOG_ASSERT_EXIT(bRetCode);

            *ppBuffer      = pCreatedBuffer;
            pCreatedBuffer = nullptr;
        }

        bResult = true;
    Exit0:
        if (pCreatedBuffer)
        {
            pCreatedBuffer->Destroy();
            SAFE_DELETE(pCreatedBuffer);
        }

        return bResult;
    }

    BOOL KGraphicDevice::CreateBufferView(IKGFX_Buffer* pBuffer, const KGFX_BufferViewDesc& viewDesc, IKGFX_BufferView** pRefBufferView, const char* szDebugName)
    {
        PROF_CPU();
        BOOL                          bResult        = false;
        BOOL                          bRetCode       = false;
        KVulkanBuffer*             pGfxBuffer     = nullptr;
        KVulkanBufferResourceView* pGfxBufferView = nullptr;

        KGLOG_ASSERT_EXIT(pRefBufferView);
        *pRefBufferView = nullptr;

        KGLOG_ASSERT_EXIT(GetGfxApi() == GFX_API::GFX_VULKAN_API);
        KGLOG_ASSERT_EXIT(pBuffer);
        // KGLOG_ASSERT_EXIT(szDebugName);

        pGfxBuffer = dynamic_cast<KVulkanBuffer*>(pBuffer);
        KGLOG_ASSERT_EXIT(pGfxBuffer);

        pGfxBufferView = new KVulkanBufferResourceView;
        KGLOG_ASSERT_EXIT(pGfxBufferView);

        bRetCode = pGfxBufferView->Create(pGfxBuffer, &viewDesc);
        KGLOG_PROCESS_ERROR(bRetCode);
        if (szDebugName)
        {
            pGfxBufferView->SetObjectName(szDebugName);
        }

        *pRefBufferView = pGfxBufferView;
        pGfxBufferView  = nullptr;

        bResult = true;
    Exit0:
        SAFE_RELEASE(pGfxBufferView);
        return bResult;
    }

    BOOL KGraphicDevice::CreateFence(KVulkanFence** ppFence, BOOL bInitWithSignaled)
    {
        PROF_CPU();
        BOOL bRet = false;

        KVulkanFence* pFence = nullptr;
        *ppFence             = nullptr;

        pFence = new KVulkanFence;

        if (!pFence->Create(bInitWithSignaled))
        {
            SAFE_DELETE(pFence);
        }

        KGLOG_PROCESS_ERROR(pFence);

        *ppFence = pFence;
        pFence   = nullptr;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::CreateSignalFence(KSignalFence** ppRetSignalFence)
    {
        PROF_CPU();
        BOOL                bResult         = false;
        KVulkanSignalFence* pkvkSignalFence = nullptr;

        KGLOG_ASSERT_EXIT(ppRetSignalFence);
        *ppRetSignalFence = nullptr;

        pkvkSignalFence = new KVulkanSignalFence;
        KGLOG_ASSERT_EXIT(pkvkSignalFence);

        *ppRetSignalFence = pkvkSignalFence;
        pkvkSignalFence   = nullptr;

        bResult = true;
    Exit0:
        SAFE_RELEASE(pkvkSignalFence);
        return bResult;
    }

    BOOL KGraphicDevice::CreateSemaphoreA(KVulkanSemaphore** ppSem)
    {
        PROF_CPU();
        BOOL bRet = false;

        *ppSem                       = nullptr;
        KVulkanSemaphore* pSemaphore = nullptr;

        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pSemaphore = new KVulkanSemaphore;
        }

        if (!pSemaphore->Create())
        {
            SAFE_DELETE(pSemaphore);
        }

        KGLOG_PROCESS_ERROR(pSemaphore);
        *ppSem = pSemaphore;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::CreateCommandPool(KVulkanCommandPool** ppRetCmdPool, uint32_t uQueueFamilyIndex)
    {
        PROF_CPU();
        BOOL bRet = false;

        KVulkanCommandPool* pCommandPool = nullptr;
        *ppRetCmdPool              = nullptr;
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pCommandPool = new KVulkanCommandPool;
            if (!pCommandPool->Create(uQueueFamilyIndex))
            {
                SAFE_DELETE(pCommandPool);
            }
        }

        KGLOG_PROCESS_ERROR(pCommandPool);
        *ppRetCmdPool = pCommandPool;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::DestroyCommandPool(KVulkanCommandPool*& ppRefCmdPool)
    {
        PROF_CPU();
        BOOL bRet = false;
        KGLOG_PROCESS_ERROR(ppRefCmdPool);

        bRet = ppRefCmdPool->Destroy();
        SAFE_DELETE(ppRefCmdPool);

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::CreateCommandBuffer(KVulkanCommandBuffer** ppRetCmd, enumCommandBufferLevel eCmdBufferLevel, enumForProcessType eCmdType, KVulkanCommandPool* pCommandPool, const char* pcszCommandName)
    {
        PROF_CPU();
        BOOL            bResult        = FALSE;
        KVulkanCommandBuffer* pCommandBuffer = nullptr;

        *ppRetCmd = nullptr;
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pCommandBuffer = new KVulkanCommandBuffer;
            if (!pCommandBuffer->Create(eCmdBufferLevel, eCmdType, pCommandPool))
            {
                SAFE_DELETE(pCommandBuffer);
            }
        }

        KGLOG_PROCESS_ERROR(pCommandBuffer);

        if (pcszCommandName)
        {
            // SetCommandBufferName(pCommandBuffer, pcszCommandName);
        }

        *ppRetCmd = pCommandBuffer;

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KGraphicDevice::CreateCommandBuffer(KVulkanCommandBuffer** ppCmd, enumCommandBufferLevel eCmdBufferLevel, enumForProcessType eCmdType, const char* pcszCommandName)
    {
        PROF_CPU();
        BOOL            bResult        = FALSE;
        KVulkanCommandBuffer* pCommandBuffer = nullptr;

        *ppCmd = nullptr;
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pCommandBuffer = new KVulkanCommandBuffer;
            if (!pCommandBuffer->Create(eCmdBufferLevel, eCmdType))
            {
                SAFE_DELETE(pCommandBuffer);
            }
        }
        KGLOG_PROCESS_ERROR(pCommandBuffer);

        if (pcszCommandName)
        {
            // SetCommandBufferName(pCommandBuffer, pcszCommandName);
            int a = 0;
        }

        pCommandBuffer->m_CreateByThreadId = GetThreadId();
        *ppCmd                             = pCommandBuffer;

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KGraphicDevice::CreateRenderTarget2D(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pDesc, BOOL bTileOptimize, uint64_t* pRetCheckCode)
    {
        PROF_CPU();
        BOOL bRet = false;

        KVulkanRenderTarget2D* pRt = nullptr;
        ASSERT(*ppRenderTarget == nullptr);
        *ppRenderTarget = nullptr;
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pRt = new KVulkanRenderTarget2D();
        }
        if (!pRt->Create(pDesc, bTileOptimize))
        {
            SAFE_DELETE(pRt);
        }

        KGLOG_ASSERT_EXIT(pRt);
        *ppRenderTarget = pRt;
        bRet = true;
    Exit0:
        if (pRetCheckCode && pRt)
        {
            *pRetCheckCode += pRt->GetId();
        }
        return bRet;
    }

    BOOL KGraphicDevice::CreateLayout(KVulkanLayout** ppLayout)
    {
        PROF_CPU();
        BOOL     bRet    = false;
        KVulkanLayout* pLayout = nullptr;

        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pLayout = new KVulkanLayout;
        }

        KGLOG_PROCESS_ERROR(pLayout);
        *ppLayout = pLayout;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::DestroyLayout(KVulkanLayout*& pLayout)
    {
        BOOL bRet = false;
        KGLOG_PROCESS_ERROR(pLayout);

        bRet = pLayout->Destroy();
        SAFE_DELETE(pLayout);

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::CreateDescriptorPool(KVulkanDescriptorPool** ppDescriptorPool)
    {
        PROF_CPU();
        BOOL             bRet            = false;
        KVulkanDescriptorPool* pDescriptorPool = nullptr;
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pDescriptorPool = new KVulkanDescriptorPool;
        }
        KGLOG_PROCESS_ERROR(pDescriptorPool);

        *ppDescriptorPool = pDescriptorPool;
        bRet              = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::DestroyDescriptorPool(KVulkanDescriptorPool*& pDescriptorPool)
    {
        BOOL bRet = false;
        KGLOG_PROCESS_ERROR(pDescriptorPool);
        ASSERT(!m_bUninitting);

        {
            auto piDevice = KGFX_GetGraphicDeviceVKInternal();
            CHECK_ASSERT(piDevice);

            piDevice->GC_DelayReleaseObject(pDescriptorPool);
        }

        pDescriptorPool = nullptr;
        bRet            = true;
    Exit0:
        return bRet;
    }

    const char* KGraphicDevice::GetDeviceName()
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        if (pVulkanDevice)
        {
            return pVulkanDevice->GetGpuName();
        }

        return "";
    }

    void KGraphicDevice::GetDrawCallInfo(gfx::enumGraphicContext id, uint32_t& uBakedDrawCall, uint32_t& uUnBakedDrawCall, uint32_t& uIndexDrawCount, uint32_t& uPointDrawCount)
    {
        uBakedDrawCall   = m_uBakedDrawCall[id];
        uUnBakedDrawCall = m_uUnBakedDrawCall[id];
        uIndexDrawCount  = m_uIndexDrawCount[id] + m_uIndirectDrawCount[id];
        uPointDrawCount  = m_uPointCount[id];
    }

    void KGraphicDevice::AddBakedDrawCall(gfx::enumGraphicContext id, uint32_t uBakedDrawCall)
    {
        m_uBakedDrawCall[id] += uBakedDrawCall;
    }

    void KGraphicDevice::AddUnBakedDrawCall(gfx::enumGraphicContext id, uint32_t uUnBakedDrawCall)
    {
        m_uUnBakedDrawCall[id] += uUnBakedDrawCall;
    }

    void KGraphicDevice::AddIndexDrawCount(gfx::enumGraphicContext id, uint32_t uIndexDrawCount, uint32_t uPointCount)
    {
        m_uIndexDrawCount[id] += uIndexDrawCount;
        m_uPointCount[id]     += uPointCount;
    }

    void KGraphicDevice::AddIndirectDrawCount(gfx::enumGraphicContext id, uint32_t uIndexDrawCount)
    {
        m_uIndirectDrawCount[id] += uIndexDrawCount;
    }

    KDescriptorPoolContainer::KDescriptorPoolContainer()
    {
        m_pDescriptorPool = nullptr;
    }

    void KDescriptorPoolContainer::Clear()
    {
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        for (auto it : m_setAlloced)
        {
            KVulkanDescriptorSet* pDesc = it;
            if (m_pDescriptorPool)
            {
                m_pDescriptorPool->FreeDescriptorSet(pDesc);
            }
            pDesc->ClearPoolContainer();
        }
        m_setAlloced.clear();

        if (m_pDescriptorPool)
        {
            pGraphicDevice->DestroyDescriptorPool(m_pDescriptorPool);
        }
    }

    KDescriptorPoolContainer::~KDescriptorPoolContainer()
    {
        Clear();
    }

    void KDescriptorPoolContainer::AddAlloced(gfx::KVulkanDescriptorSet* p)
    {
        m_setAlloced.insert(p);
    }

    void KDescriptorPoolContainer::Remove(gfx::KVulkanDescriptorSet* pDes)
    {
        PROF_CPU();
        gfx::KVulkanDescriptorPool* pPool = GetDescriptorPool();
        if (m_pDescriptorPool)
        {
            gfx::KVulkanDescriptorPool* p = pPool;
            while (p)
            {
                if (p == m_pDescriptorPool)
                {
                    m_pDescriptorPool->FreeDescriptorSet(pDes);
                    break;
                }
                p = p->GetNext();
            }
        }
        auto it = m_setAlloced.find(pDes);
        if (it != m_setAlloced.end())
        {
            m_setAlloced.erase(pDes);
            pDes->ClearPoolContainer();
        }
    }

    KVulkanDescriptorPool* KDescriptorPoolContainer::GetDescriptorPool()
    {
        return m_pDescriptorPool;
    }

    BOOL KGraphicDevice::CreateDescriptorSet(KVulkanDescriptorSet** ppDescriptorSet, const KVulkanLayout* pLayout, KDescriptorPoolContainer* pDescriptorPoolContainer)
    {
        PROF_CPU_DETAIL();
        BOOL            bRet           = false;
        KVulkanDescriptorSet* pDescriptorSet = nullptr;

        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            KGLOG_PROCESS_ERROR(pLayout);
            KGLOG_PROCESS_ERROR(pDescriptorPoolContainer);
            ASSERT(*ppDescriptorSet == nullptr);
            pDescriptorSet = new KVulkanDescriptorSet(pLayout, pDescriptorPoolContainer);
        }

        KGLOG_PROCESS_ERROR(pDescriptorSet);
        *ppDescriptorSet = pDescriptorSet;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::DestroyDescriptorSet(KVulkanDescriptorSet*& pDescriptorSet)
    {
        PROF_CPU_DETAIL();
        BOOL bRet = false;

        KGLOG_PROCESS_ERROR(pDescriptorSet);

        ASSERT(!m_bUninitting);

        {
            auto piDevice = KGFX_GetGraphicDeviceVKInternal();
            CHECK_ASSERT(piDevice);

            piDevice->GC_DelayReleaseObject(pDescriptorSet);

            pDescriptorSet = nullptr;
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::CreateGraphicsPipeline(KPipeline** ppGraphicPipeline, GraphicsPipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer)
    {
        PROF_CPU();

        BOOL               bResult   = FALSE;
        KGraphicsPipeline* pPipeline = nullptr;
        *ppGraphicPipeline           = nullptr;

        std::lock_guard<std::mutex> lock(m_pipelineCreateLock);
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pPipeline = new KVulkanGraphicsPipeline();
            if (!pPipeline->Create(pDesc, pSpecializationConstantContainer))
            {
                SAFE_DELETE(pPipeline);
            }
        }

        KGLOG_PROCESS_ERROR(pPipeline);
        *ppGraphicPipeline = pPipeline;

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGraphicDevice::CreateComputePipeline(KPipeline** ppComputePipeline, ComputePipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer)
    {
        PROF_CPU();

        BOOL              bResult   = FALSE;
        KComputePipeline* pPipeline = nullptr;
        *ppComputePipeline          = nullptr;

        std::lock_guard<std::mutex> lock(m_pipelineCreateLock);
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pPipeline = new KVulkanComputePipeline();

            if (!pPipeline->Create(pDesc, pSpecializationConstantContainer))
            {
                SAFE_DELETE(pPipeline);
            }
        }
        KGLOG_PROCESS_ERROR(pPipeline);
        *ppComputePipeline = pPipeline;

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KGraphicDevice::DestroyPipeline(KPipeline*& pPipeline)
    {
        PROF_CPU();
        BOOL bRet = false;
        KG_PROCESS_ERROR(pPipeline);
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            if (pPipeline)
            {
                pPipeline->Destroy();
                SAFE_DELETE(pPipeline);
            }
        }
        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::SavePipelineCache()
    {
        PROF_CPU();

        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        std::lock_guard<std::mutex> lock(m_pipelineCreateLock);
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            KGLOG_ASSERT_EXIT(bInitedGraphic);
            vks::KVulkanDevice* pRetGfxDevice = GetVulkanDevice();
            KGLOG_ASSERT_EXIT(pRetGfxDevice);

            bRetCode = pRetGfxDevice->SavePipelineCache();
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KGraphicDevice::CreateGraphicContext(KGraphicContext** ppRetGraphicContext, const KWindow* pWindowInfo)
    {
        PROF_CPU();
        BOOL             bRet     = false;
        BOOL             bRetCode = false;
        KGraphicContext* pContext = nullptr;

        gfx::enumGraphicContext uGraphicContextId = pWindowInfo->m_uId;
        KGLOG_ASSERT_EXIT(m_pGraphicContext[uGraphicContextId] == nullptr);

        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pContext = new KVulkanGraphicContext();
            bRetCode = pContext->Init(pWindowInfo);

            m_pGraphicContext[uGraphicContextId] = pContext;
        }

        KGLOG_ASSERT_EXIT(bRetCode);

        *ppRetGraphicContext = pContext;
        bRet                 = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::DestroyGraphicContext(KGraphicContext*& pGraphicContext)
    {
        PROF_CPU();
        BOOL bRet  = false;
        BOOL bFind = false;
        KGLOG_PROCESS_ERROR(pGraphicContext);

        for (uint32_t i = 0; i < gfx::CONTEXT_COUNT; ++i)
        {
            if (m_pGraphicContext[i] == pGraphicContext)
            {
                bFind                = true;
                m_pGraphicContext[i] = nullptr;
            }
        }

        pGraphicContext->UnInit();
        SAFE_DELETE(pGraphicContext);

        KGLOG_PROCESS_ERROR(bFind);

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::GetGfxDevice(vks::KVulkanDevice** ppRetGfxDevice) const
    {
        BOOL bRet = false;
        KGLOG_PROCESS_ERROR(ppRetGfxDevice);

        *ppRetGfxDevice = nullptr;

        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            *ppRetGfxDevice = GetVulkanDevice();
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    void KGraphicDevice::CopyCurrentSwapChainLoopCount(uint32_t pSwapChainLoopCountArray[], uint32_t uCount)
    {
        KASSERT(uCount == gfx::CONTEXT_COUNT);
        if (DrvOption::bSwapChainSafeLoopRelease)
        {
            for (uint32_t i = 0; i < uCount; ++i)
            {
                if (m_pGraphicContext[i])
                {
                    pSwapChainLoopCountArray[i] = m_pGraphicContext[i]->m_uSwapChainLoopCount;
                }
            }
        }
    }

    void KGraphicDevice::Uninit()
    {
        m_bUninitting = true;

        ClearSecondCommandCache();

        m_lsDelayReleaseSecondaryCommandBuffer_Lock.lock();
        for (auto it : m_lsDelayReleaseSecondaryCommandBuffer)
        {
            KVulkanCommandBuffer* pCmd = it;
            pCmd->Reset(false);
            SAFE_RELEASE(pCmd);
        }
        m_lsDelayReleaseSecondaryCommandBuffer.clear();
        m_lsDelayReleaseSecondaryCommandBuffer_Lock.unlock();

        m_lsDelayReleasePrimaryCommandBuffer_Lock.lock();
        for (auto it : m_lsDelayReleasePrimaryCommandBuffer)
        {
            KVulkanCommandBuffer* pCmd = it;
            pCmd->Reset(false);
            SAFE_RELEASE_LAST(pCmd);
        }
        m_lsDelayReleasePrimaryCommandBuffer.clear();
        m_lsDelayReleasePrimaryCommandBuffer_Lock.unlock();
    }

    static gfx::enumStoreActionType GetStoreOpNone()
    {
        return DrvOption::bSupportStoreOpNone ? STORE_ACTION_NONE : STORE_ACTION_STORE;
    }

    static gfx::enumLoadActionType GetLoadOpNone()
    {
#ifdef _WIN32
        return DrvOption::bSupportStoreOpNone ? LOAD_ACTION_NONE : LOAD_ACTION_LOAD;
#else
        return LOAD_ACTION_LOAD;
#endif
    }

    // renderpass has cache map
    BOOL KGraphicDevice::CreateRenderPass(KVulkanRenderPass** ppRenderPass, KRenderPassDesc* pDesc)
    {
        PROF_CPU();
        BOOL bRet = false;

        KVulkanRenderPass* pRenderPass = nullptr;
        *ppRenderPass                  = nullptr;
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            uint64_t renderPassHash = 0;
            // Generate hash for render pass and frame buffer
            for (uint32_t i = 0; i < pDesc->uRenderTargetCount; ++i)
            {
                uint32_t hashValues[] = {
                    pDesc->vecColorFormats[i],
                    pDesc->vecSampleCount[i],
                    0,
                    pDesc->vecLoadActionsColor.size() ? (uint32_t)pDesc->vecLoadActionsColor[i] : 0,
                };

                renderPassHash = KSTR_HELPER::hash_state(hashValues, countof(hashValues), renderPassHash);
            }

            uint32_t hashValues[] = {
                pDesc->enuDepthStencilFormat,
                pDesc->depthSampleCount,
                0, // bSrgb
                pDesc->enuLoadActionDepth,
                pDesc->enuLoadActionStencil,
                pDesc->depthInitLayout,
                pDesc->depthfinalLayout
            };
            renderPassHash = KSTR_HELPER::hash_state(hashValues, countof(hashValues), renderPassHash);

            gfx::RenderPassMap& renderPassMap = gfx::get_render_pass_map();

            const auto& it = renderPassMap.find(renderPassHash);
            if (it == renderPassMap.end())
            {
                pRenderPass        = new KVulkanRenderPass;
                BOOL bCreateResult = false;
                bCreateResult      = pRenderPass->Create(pDesc);

                if (!bCreateResult)
                {
                    SAFE_DELETE(pRenderPass);
                }
                else
                {
                    renderPassMap.insert({renderPassHash, pRenderPass});
                }
                // SetRenderPassName(pRenderPass, GetRenderPassName(pDesc->enuRenderpass));
            }
            else
            {
                pRenderPass = it->second;
            }
            // pRenderPass->AddRef();
        }

        KGLOG_PROCESS_ERROR(pRenderPass);
        *ppRenderPass = pRenderPass;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::DestroyRenderPass(KVulkanRenderPass*& pRenderPass)
    {
        BOOL bRet = false;

        KG_PROCESS_ERROR(pRenderPass);
        // SAFE_RELEASE(pRenderPass);

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::CreateVertDescriptor(KVulkanVertexDescriptor** ppVertDescriptor)
    {
        BOOL bRet                          = false;
        *ppVertDescriptor                  = nullptr;
        KVulkanVertexDescriptor* pVertDescriptor = nullptr;
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            pVertDescriptor = new KVulkanVertexDescriptor;
        }
        KGLOG_PROCESS_ERROR(pVertDescriptor);
        *ppVertDescriptor = pVertDescriptor;
        bRet              = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::DestroyVertDescriptor(KVulkanVertexDescriptor*& pVertexDescriptor)
    {
        BOOL bRet = false;

        KGLOG_PROCESS_ERROR(pVertexDescriptor);

        SAFE_DELETE(pVertexDescriptor);

        bRet = true;
    Exit0:
        return bRet;
    }




    BOOL KGraphicDevice::LoadShaderVSAndFS(
        KShaderStage*                   ppShaderStage[],
        const char*                     szShaderSource,
        const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char* szShaderDef, const char* szMacro,
        gfx::IShaderReflector* pReflector,
        BOOL                   bReCreate,
        BOOL                   bByBuildToolCmd /* = false*/,
        int                    nPlatform /* = 0*/
    )
    {
        PROF_CPU();
        BOOL     bRet         = false;
        BOOL     bRetCode     = false;
        uint32_t uHashVs      = 0;
        uint32_t uHashFs      = 0;
        uint32_t uHashGs      = 0;
        BOOL     bRealBuildVS = false;
        BOOL     bRealBuildFS = false;
        BOOL     bRealBuildGS = false;
        BOOL     bJson        = false;

        std::lock_guard<std::mutex> lock(m_ShaderLoadLock);

        char techPath[MAX_PATH];
        snprintf(techPath, MAX_PATH, "%s/%s", TECH_ROOT_DIR, szShaderSource);

        if (strstr(techPath, ".jsontech"))
        {
            bJson = true;
            std::string              szWholeFileString;
            KUniqueStr               ustrShaderPath = g_CachePathString(techPath, TRUE);
            NSKBase::tagFileLocation sShaderLoc(ustrShaderPath);
            bRetCode = IncludeFileHelper::ReadWholeShaderFile(sShaderLoc, szWholeFileString, nullptr);
            KGLOG_PROCESS_ERROR(bRetCode);

            // 尝试用json去解析
            rapidjson::Document JsonDocument;
            JsonDocument.Parse(szWholeFileString.c_str());
            KGLOG_ASSERT_EXIT(!JsonDocument.HasParseError());
            auto& ParamObjectArray = JsonDocument["Tech"];
            ASSERT(ParamObjectArray.IsArray());
            BOOL bFind = false;
            for (auto it = ParamObjectArray.Begin(), iend = ParamObjectArray.End(); it != iend; ++it)
            {
                ASSERT(it->IsObject());
                auto        ParamObject = it->GetObject();
                const char* pTechName   = ParamObject["Name"].GetString();
                if (strcmp(pTechName, szShaderDef) == 0)
                {
                    bFind              = true;
                    auto& programArray = ParamObject["Program"];
                    ASSERT(programArray.IsArray());

                    if (pReflector)
                    {
                        nPlatform    = pReflector->GetPlatform();
                        uint32_t uId = 0;
                        for (auto itt = programArray.Begin(), end = programArray.End(); itt != end; ++itt)
                        {
                            ASSERT(itt->IsObject());
                            auto        programItem = itt->GetObject();
                            const char* pShaderType = programItem["ShaderType"].GetString();

                            if (strcmp(pShaderType, "vs") == 0)
                            {
                                bRetCode = _LoadShader(nPlatform, &ppShaderStage[uId], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Vertex, &uHashVs, &bRealBuildVS, pReflector, bReCreate);
                                KGLOG_PROCESS_ERROR(bRetCode);
                                ++uId;
                                continue;
                            }
                            else if (strcmp(pShaderType, "gs") == 0)
                            {
                                bRetCode = _LoadShader(nPlatform, &ppShaderStage[uId], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Geometry, &uHashGs, &bRealBuildGS, pReflector, bReCreate);
                                KGLOG_PROCESS_ERROR(bRetCode);
                                ++uId;
                                continue;
                            }
                            else if (strcmp(pShaderType, "fs") == 0)
                            {
                                bRetCode = _LoadShader(nPlatform, &ppShaderStage[uId], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Fragment, &uHashVs, &bRealBuildVS, pReflector, bReCreate);
                                KGLOG_PROCESS_ERROR(bRetCode);
                                ++uId;
                                continue;
                            }
                        }
                    }
                    else
                    {
                        KShaderResourceVK reflector;
                        reflector.bByBuildTool = bByBuildToolCmd;
                        nPlatform              = pReflector->GetPlatform();
                        if (!bByBuildToolCmd)
                        {
                            reflector.bForMaterialSystem = false;
                        }
                        uint32_t uId = 0;
                        for (auto itt = programArray.Begin(), end = programArray.End(); itt != end; ++itt)
                        {
                            ASSERT(itt->IsObject());
                            auto        programItem = itt->GetObject();
                            const char* pShaderType = programItem["ShaderType"].GetString();

                            if (strcmp(pShaderType, "vs") == 0)
                            {
                                bRetCode = _LoadShader(nPlatform, &ppShaderStage[uId], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Vertex, &uHashVs, &bRealBuildVS, &reflector, bReCreate);
                                KGLOG_PROCESS_ERROR(bRetCode);
                                ++uId;
                                continue;
                            }
                            else if (strcmp(pShaderType, "gs") == 0)
                            {
                                bRetCode = _LoadShader(nPlatform, &ppShaderStage[uId], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Geometry, &uHashGs, &bRealBuildGS, &reflector, bReCreate);
                                KGLOG_PROCESS_ERROR(bRetCode);
                                ++uId;
                                continue;
                            }
                            else if (strcmp(pShaderType, "fs") == 0)
                            {
                                bRetCode = _LoadShader(nPlatform, &ppShaderStage[uId], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Fragment, &uHashVs, &bRealBuildVS, &reflector, bReCreate);
                                KGLOG_PROCESS_ERROR(bRetCode);
                                ++uId;
                                continue;
                            }
                        }
                    }
                    break;
                }
            }
            if (!bFind)
            {
                KGLogPrintf(KGLOG_ERR, "%s 找不到tech: %s", techPath, szShaderDef);
                goto Exit0;
            }
            nPlatform = pReflector->GetPlatform();
        }

        if (!bJson)
        { // 还留着这个流程，跑glvk的加载，glvk改完之后，这个里面的流程就可以去掉了
            if (pReflector)
            {
                pReflector->bByBuildTool = bByBuildToolCmd;
                bRetCode                 = _LoadShader(nPlatform, &ppShaderStage[0], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Vertex, &uHashVs, &bRealBuildVS, pReflector, bReCreate);
                KGLOG_PROCESS_ERROR(bRetCode);

                bRetCode = _LoadShader(nPlatform, &ppShaderStage[1], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Fragment, &uHashFs, &bRealBuildFS, pReflector, bReCreate);
                KGLOG_PROCESS_ERROR(bRetCode);

                nPlatform = pReflector->GetPlatform();
            }
            else
            {
                // KShaderCheckReflector reflector;
                KShaderResourceVK reflector;
                reflector.bByBuildTool = bByBuildToolCmd;

                if (!bByBuildToolCmd)
                {
                    reflector.bForMaterialSystem = false;
                }

                bRetCode = _LoadShader(nPlatform, &reflector.m_pShaderStage[0], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Vertex, &uHashVs, &bRealBuildVS, &reflector, bReCreate);
                KGLOG_PROCESS_ERROR(bRetCode);

                bRetCode = _LoadShader(nPlatform, &reflector.m_pShaderStage[1], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Fragment, &uHashFs, &bRealBuildFS, &reflector, bReCreate);
                KGLOG_PROCESS_ERROR(bRetCode);

                ppShaderStage[0] = reflector.m_pShaderStage[0];
                ppShaderStage[1] = reflector.m_pShaderStage[1];

                reflector.m_pShaderStage[0] = nullptr;
                reflector.m_pShaderStage[1] = nullptr;

                nPlatform = reflector.GetPlatform();
            }
        }

        ASSERT(nPlatform > 0);
        if (!bByBuildToolCmd)
        {
            // 上报shader编译信息到XGSDK
            if (g_sX3DXGSDKReporter.IsValid() && (bRealBuildVS || bRealBuildFS))
            {
                g_sX3DXGSDKReporter.TrackShaderVSFSCompileEvent(nPlatform, szShaderSource, sIncludeShaderLoc.GetFilePath(), szShaderDef, szMacro, uHashVs, uHashFs);
            }
        }

        bRet = true;
    Exit0:
        return bRet;
    }


    BOOL KGraphicDevice::LoadShaderWithoutTech(KShaderStage** ppShaderStage,
        const char* pMainShader,
        const char* pEnterpoint,
        const NSKBase::tagFileLocation& sUserShaderLoc,
        const char* szMacro,
        gfx::IShaderReflector* pReflector,
        gfx::ShaderStageType eShaderStage,
        BOOL bByBuildToolCmd,
        int nPlatform)
    {
        BOOL bResult = FALSE;
        BOOL bRetCode = FALSE;
        uint32_t uHash = 0;
        BOOL     bRealBuild = FALSE;
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        KVulkanShaderStage* pShaderStage = new KVulkanShaderStage(nPlatform);

        pShaderStage->m_shaderInfo.ustrShaderSource = g_CachePathString(pMainShader, TRUE);
        pShaderStage->m_shaderInfo.sIncludedShaderLoc = sUserShaderLoc;
        pShaderStage->m_shaderInfo.strMacro = szMacro;

        ASSERT(strstr(pShaderStage->m_shaderInfo.strMacro.c_str(), "PLATFORM") == nullptr);
        pShaderStage->m_shaderInfo.strShaderDef = pEnterpoint;
        pShaderStage->m_shaderInfo.eShaderStage = eShaderStage;

        pShaderStage->m_shaderInfo.MakeGroupKey();
        pShaderStage->m_shaderInfo.MakeSpvPath();
        ASSERT(!pShaderStage->m_shaderInfo.strSpvPath.empty());
        pShaderStage->m_shaderInfo.MakeScPath();
        pReflector->SetPlatform(GetCurrentPlatform());
        bRetCode = pVulkanDevice->LoadShaderWithoutTech(pShaderStage, pMainShader, pEnterpoint, sUserShaderLoc, szMacro, eShaderStage, pReflector);
        if (!bRetCode)
        {
            SAFE_DELETE(pShaderStage);
            goto Exit0;
        }
        else
        {
            bRetCode = pShaderStage->CreateShader(&uHash, &bRealBuild, pReflector);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        *ppShaderStage = pShaderStage;
        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGraphicDevice::LoadShaderCS(
        KShaderStage**                  ppShaderStage,
        const char*                     szShaderSource,
        const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char*                     szShaderDef,
        const char*                     szMacro,
        BOOL                            bByBuildToolCmd /* = false*/,
        int                             nPlatform /* = 0*/
    )
    {
        PROF_CPU();
        BOOL     bRet       = false;
        BOOL     bRetCode   = false;
        uint32_t uHash      = 0;
        BOOL     bRealBuild = false;

        std::lock_guard<std::mutex> lock(m_ShaderLoadLock);

        // KShaderCheckReflector reflector;
        KShaderResourceVK reflector;
        reflector.bForMaterialSystem = false;
        bRetCode                     = _LoadShader(nPlatform, &reflector.m_pShaderStage[0], szShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, gfx::ShaderStageType::Compute, &uHash, &bRealBuild, &reflector, false);
        KGLOG_PROCESS_ERROR(bRetCode);

        ppShaderStage[0]            = reflector.m_pShaderStage[0];
        reflector.m_pShaderStage[0] = nullptr;

        nPlatform = reflector.GetPlatform();
        ASSERT(nPlatform > 0);
        if (!bByBuildToolCmd)
        {
            // 上报shader编译信息到XGSDK
            if (g_sX3DXGSDKReporter.IsValid() && bRealBuild)
            {
                g_sX3DXGSDKReporter.TrackShaderCSCompileEvent(nPlatform, szShaderSource, UNUSED_FILE_PATH, szShaderDef, szMacro, uHash);
            }
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::LoadShaderCS(
        KShaderStage*                   ppShaderStage[],
        const char*                     pcszShaderSource,
        const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char*                     pcszShaderDef,
        const char*                     szMacro,
        gfx::IShaderReflector*          pReflector,
        BOOL bByBuildToolCmd /*= false*/, int nPlatform /*= 0*/
    )
    {
        PROF_CPU();

        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        uint32_t uHash      = 0;
        BOOL     bRealBuild = FALSE;

        std::lock_guard<std::mutex> lock(m_ShaderLoadLock);

        KGLOG_ASSERT_EXIT(ppShaderStage);
        KGLOG_ASSERT_EXIT(pcszShaderSource && pcszShaderSource[0]);
        KGLOG_ASSERT_EXIT(pcszShaderDef && pcszShaderDef[0]);
        KGLOG_ASSERT_EXIT(pReflector);

        pReflector->bForMaterialSystem = false;
        bRetCode                       = _LoadShader(nPlatform, ppShaderStage, pcszShaderSource, sIncludeShaderLoc, pcszShaderDef, szMacro, gfx::ShaderStageType::Compute, &uHash, &bRealBuild, pReflector, false);
        KGLOG_PROCESS_ERROR(bRetCode);

        nPlatform = pReflector->GetPlatform();
        ASSERT(nPlatform > 0);
        if (!bByBuildToolCmd)
        {
            // 上报shader编译信息到XGSDK
            if (g_sX3DXGSDKReporter.IsValid() && bRealBuild)
            {
                g_sX3DXGSDKReporter.TrackShaderCSCompileEvent(nPlatform, pcszShaderSource, UNUSED_FILE_PATH, pcszShaderDef, szMacro, uHash);
            }
        }

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KGraphicDevice::_LoadShader(
        int                             nPlatform,
        KShaderStage**                  ppShaderStage,
        const char*                     pcszShaderSource,
        const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char*                     szShaderDef,
        const char*                     szMacro,
        gfx::ShaderStageType            shaderType,
        uint32_t*                       pRetHash,
        BOOL*                           pRealBuild,
        gfx::IShaderReflector*          pReflector,
        BOOL                            bReCreate
    )
    {
        PROF_CPU();
        BOOL          bRet         = false;
        BOOL          bRetCode     = false;
        static double fLoadTime    = 0.0f;
        double        fTime        = NSEngine::GetEnginePerformance()->TimeGetTime();
        KShaderStage* pShaderStage = nullptr;
        *ppShaderStage             = nullptr;
        double fDeltaTime          = 0.0f;

        KGLOG_ASSERT_EXIT(pReflector);
        KGLOG_ASSERT_EXIT(GetGfxApi() == GFX_API::GFX_VULKAN_API);

        if (nPlatform == 0)
        {
            // 构建机预编译shader会明确传入平台定义，只有客户端Runtime实时计算平台
            KGLOG_ASSERT_EXIT(!pReflector->bByBuildTool);
            nPlatform = GetCurrentPlatform();
        }
        pReflector->SetPlatform(nPlatform); // 为了传给SpirvBuilder

        pShaderStage = new KVulkanShaderStage(nPlatform);
        if (!pShaderStage->LoadShader(pcszShaderSource, sIncludeShaderLoc, szShaderDef, szMacro, shaderType, pReflector))
        {
            SAFE_DELETE(pShaderStage);
        }
        else
        {
            *ppShaderStage = pShaderStage; // 记录，KShaderResourceVK::BuildReflection有用
            if (bReCreate)
            {
                bRetCode = pShaderStage->ReCreateShader(pRetHash, pRealBuild, pReflector);
                KGLOG_PROCESS_ERROR(bRetCode);
            }
            else
            {
                bRetCode = pShaderStage->CreateShader(pRetHash, pRealBuild, pReflector);
                KGLOG_PROCESS_ERROR(bRetCode);
            }
        }

        KGLOG_PROCESS_ERROR(*ppShaderStage);
        fDeltaTime  = NSEngine::GetEnginePerformance()->TimeGetTime() - fTime;
        fLoadTime  += fDeltaTime;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KGraphicDevice::UnloadShader(KShaderStage*& pShaderStage)
    {
        BOOL bRet = false;

        KGLOG_PROCESS_ERROR(pShaderStage);


        SAFE_DELETE(pShaderStage);

        bRet = true;
    Exit0:
        return bRet;
    }

    void KGraphicDevice::ClearFrameCounter(gfx::enumGraphicContext id)
    {
        // for (uint32_t i = 0; i < gfx::CONTEXT_COUNT; ++i)
        {
            m_uBakedDrawCall[id]     = 0;
            m_uUnBakedDrawCall[id]   = 0;
            m_uIndexDrawCount[id]    = 0;
            m_uIndirectDrawCount[id] = 0;
            m_uPointCount[id]        = 0;
        }
    }

    void KGraphicDevice::FrameMove(BOOL bFrameRendered)
    {
        PROF_CPU();
        for (uint32_t id = 0; id < enumGraphicContext::CONTEXT_COUNT; id++)
        {
            DoSecondCommandCacheGC((enumGraphicContext)id);
        }

        vks::KVulkanDevice* pDevice = GetVulkanDevice();
        if (pDevice)
        {
            pDevice->FrameMove();
        }

        // if (bFrameRendered)
        {
            // 比如过图界面3d场景就不会绘制，这样会导致绑定rt的关系不会重建，所以没有绘制就不能触发删除，不然有的设备驱动认为资源还绑定在descriptorset上还正在使用,比如ios会搞宕机

            {
                PROF_CPU("DelayReleasePrimaryCommandBuffer");
                m_lsDelayReleasePrimaryCommandBuffer_Lock.lock();
                for (auto it = m_lsDelayReleasePrimaryCommandBuffer.begin(), e = m_lsDelayReleasePrimaryCommandBuffer.end(); it != e;)
                {
                    KVulkanCommandBuffer* pCmd = *it;
                    if (pCmd->m_delayReleaseCounter == 0)
                    {
                        it = m_lsDelayReleasePrimaryCommandBuffer.erase(it);
                        pCmd->Reset(false);
                        SAFE_RELEASE(pCmd);
                    }
                    else
                    {
                        pCmd->m_delayReleaseCounter--;
                        ++it;
                    }
                }
                m_lsDelayReleasePrimaryCommandBuffer_Lock.unlock();
            }

            {
                PROF_CPU("DelayReleaseSecondaryCommandBuffer");
                // 卡顿优化：限制每帧释放
                m_lsDelayReleaseSecondaryCommandBuffer_Lock.lock();
                int nMaxReleaseCountOneFrame = std::max(std::min(30, (int)m_lsDelayReleaseSecondaryCommandBuffer.size() / 30), 30);
                int nCurReleasedCount        = 0;
                for (auto it = m_lsDelayReleaseSecondaryCommandBuffer.begin(), e = m_lsDelayReleaseSecondaryCommandBuffer.end();
                     nCurReleasedCount < nMaxReleaseCountOneFrame && it != e;)
                {
                    KVulkanCommandBuffer* pCmd = *it;
                    if (pCmd->m_delayReleaseCounter == 0)
                    {
                        ++nCurReleasedCount;
                        it = m_lsDelayReleaseSecondaryCommandBuffer.erase(it);
                        pCmd->Reset(false);
                        int32_t ret = pCmd->Release();
                        ASSERT(ret == 0);
                    }
                    else
                    {
                        pCmd->m_delayReleaseCounter--;
                        ++it;
                    }
                }
                m_lsDelayReleaseSecondaryCommandBuffer_Lock.unlock();
            }
        }
    }

    gfx::ShaderStageType GetShaderTypeFromName(const char* pShaderName)
    {
        gfx::ShaderStageType type = gfx::ShaderStageType::AllGraphics;
        if (KSTR_HELPER::StrEndWith(pShaderName, ".vert"))
        {
            type = gfx::ShaderStageType::Vertex;
        }
        else if (KSTR_HELPER::StrEndWith(pShaderName, ".frag"))
        {
            type = gfx::ShaderStageType::Fragment;
        }
        else if (KSTR_HELPER::StrEndWith(pShaderName, ".comp"))
        {
            type = gfx::ShaderStageType::Compute;
        }
        return type;
    }

    BOOL GetGroupKey(std::string& strGroupKey, const char* pcszShaderSource, const char* pcszIncludeShaderSource, const char* pcszShaderDef, const char* pcszMacro)
    {
        char szGroupKey[MAX_PATH] = "";
        char szOutPath[MAX_PATH]  = "";
        char szName[MAX_PATH]     = "";
        KSTR_HELPER::SplitPath(pcszShaderSource, szOutPath, szName, nullptr);

        unsigned uIncludeFileNameHash = 0;
        if (pcszIncludeShaderSource && strlen(pcszIncludeShaderSource) > 0)
        {
            uIncludeFileNameHash = g_FileNameHash(pcszIncludeShaderSource);
        }
        // szInclude里面可能有非ASCII编码字符，后续Shader预编译资源写磁盘在移动平台会出错，所以调整成Hash
        snprintf(szGroupKey, MAX_PATH, "%s/%s[%u]%s@%s", szOutPath, szName, uIncludeFileNameHash, pcszMacro, pcszShaderDef);
        szGroupKey[countof(szGroupKey) - 1] = 0;

        strGroupKey = szGroupKey;
        return true;
    }

    BOOL GetSpvPath(std::string& strSpvPath, const char* pcszGroupKey, gfx::ShaderStageType eShaderStage)
    {
        BOOL bResult = FALSE;

        char szSpvPath[NSEngine::MAX_PATH_LEN] = "";
        switch (eShaderStage)
        {
        case gfx::ShaderStageType::Vertex:
            {
                snprintf(szSpvPath, countof(szSpvPath), "%s.vert", pcszGroupKey);
                szSpvPath[countof(szSpvPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Hull:
            {
                snprintf(szSpvPath, countof(szSpvPath), "%s.tesc", pcszGroupKey);
                szSpvPath[countof(szSpvPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Domain:
            {
                snprintf(szSpvPath, countof(szSpvPath), "%s.tese", pcszGroupKey);
                szSpvPath[countof(szSpvPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Geometry:
            {
                snprintf(szSpvPath, countof(szSpvPath), "%s.geom", pcszGroupKey);
                szSpvPath[countof(szSpvPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Fragment:
            {
                snprintf(szSpvPath, countof(szSpvPath), "%s.frag", pcszGroupKey);
                szSpvPath[countof(szSpvPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Compute:
            {
                snprintf(szSpvPath, countof(szSpvPath), "%s.comp", pcszGroupKey);
                szSpvPath[countof(szSpvPath) - 1] = 0;
            }
            break;
        default:
            {
                KGLogPrintf(KGLOG_ERR, "something error");
                KGLOG_ASSERT_EXIT(FALSE);
            }
            break;
        }

        strSpvPath = szSpvPath;
        bResult    = TRUE;
    Exit0:
        return bResult;
    }

    BOOL GetScPath(std::string& strScPath, const char* pcszShaderSource, const char* pcszIncludeShaderSource, const char* pcszShaderDef, gfx::ShaderStageType eShaderStage)
    {
        BOOL bResult = FALSE;

        char szGroupKey[NSEngine::MAX_PATH_LEN] = "";
        char szOutPath[MAX_PATH]                = "";
        char szName[MAX_PATH]                   = "";
        KSTR_HELPER::SplitPath(pcszShaderSource, szOutPath, szName, nullptr);

        unsigned uIncludeFileNameHash = 0;
        if (pcszIncludeShaderSource && strlen(pcszIncludeShaderSource) > 0)
        {
            uIncludeFileNameHash = g_FileNameHash(pcszIncludeShaderSource);
        }
        snprintf(szGroupKey, countof(szGroupKey), "%s/%s[%u]%s", szOutPath, szName, uIncludeFileNameHash, pcszShaderDef);
        szGroupKey[countof(szGroupKey) - 1] = 0;

        char szScPath[NSEngine::MAX_PATH_LEN] = "";
        switch (eShaderStage)
        {
        case gfx::ShaderStageType::Vertex:
            {
                snprintf(szScPath, countof(szScPath), "%s.vert.sc", szGroupKey);
                szScPath[countof(szScPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Hull:
            {
                snprintf(szScPath, countof(szScPath), "%s.tesc.sc", szGroupKey);
                szScPath[countof(szScPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Domain:
            {
                snprintf(szScPath, countof(szScPath), "%s.tese.sc", szGroupKey);
                szScPath[countof(szScPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Geometry:
            {
                snprintf(szScPath, countof(szScPath), "%s.geom.sc", szGroupKey);
                szScPath[countof(szScPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Fragment:
            {
                snprintf(szScPath, countof(szScPath), "%s.frag.sc", szGroupKey);
                szScPath[countof(szScPath) - 1] = 0;
            }
            break;
        case gfx::ShaderStageType::Compute:
            {
                snprintf(szScPath, countof(szScPath), "%s.comp.sc", szGroupKey);
                szScPath[countof(szScPath) - 1] = 0;
            }
            break;
        default:
            {
                KGLogPrintf(KGLOG_ERR, "something error");
                KGLOG_ASSERT_EXIT(FALSE);
            }
            break;
        }

        strScPath = szScPath;
        bResult   = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KShaderInfo::MakeGroupKey()
    {
        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        bRetCode = GetGroupKey(strGroupkey, ustrShaderSource, sIncludedShaderLoc.GetFilePath().Str(), strShaderDef.c_str(), strMacro.c_str());
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KShaderInfo::MakeSpvPath()
    {
        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        bRetCode = GetSpvPath(strSpvPath, strGroupkey.c_str(), eShaderStage);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KShaderInfo::MakeScPath()
    {
        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        bRetCode = GetScPath(strScPath, ustrShaderSource, sIncludedShaderLoc.GetFilePath().Str(), strShaderDef.c_str(), eShaderStage);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    KVulkanCommandBuffer* KGraphicDevice::GetSecondCommandFromCache(gfx::enumGraphicContext eGraphicContext, const KCommandBufferKey& sCmdBufferKey, const char* pcszCommandName)
    {
        PROF_CPU();
        BOOL                 bRetCode       = false;
        gfx::KVulkanCommandBuffer* pCommandBuffer = nullptr;

        uint32_t uDeadCacheId = m_AliveCacheId ? 0 : 1;

        const auto& it = m_mapSecondCommandCache[eGraphicContext][uDeadCacheId].find(sCmdBufferKey);
        if (it != m_mapSecondCommandCache[eGraphicContext][uDeadCacheId].end())
        {
            pCommandBuffer = it->second;
            m_mapSecondCommandCache[eGraphicContext][m_AliveCacheId].insert({sCmdBufferKey, pCommandBuffer});
            m_mapSecondCommandCache[eGraphicContext][uDeadCacheId].erase(it);
        }
        else
        {
            const auto& it1 = m_mapSecondCommandCache[eGraphicContext][m_AliveCacheId].find(sCmdBufferKey);
            if (it1 != m_mapSecondCommandCache[eGraphicContext][m_AliveCacheId].end())
            {
                pCommandBuffer = it1->second;
            }
            else
            {
                m_lsDelayReleaseSecondaryCommandBuffer_Lock.lock();
                if (!m_lsDelayReleaseSecondaryCommandBuffer.empty())
                {
                    gfx::KVulkanCommandBuffer* pCmd = m_lsDelayReleaseSecondaryCommandBuffer.front();
                    m_lsDelayReleaseSecondaryCommandBuffer.pop_front();
                    if (pCmd)
                    {
                        pCmd->m_uCmmitCode = 0;
                        pCommandBuffer     = pCmd;
                        m_mapSecondCommandCache[eGraphicContext][m_AliveCacheId].insert(std::make_pair<>(sCmdBufferKey, pCommandBuffer));
                    }
                }
                else
                {
                    bRetCode = CreateCommandBuffer(&pCommandBuffer, gfx::COMMAND_BUFFER_LEVEL_SECONDARY, gfx::FOR_GRPAHIC, pcszCommandName);
                    if (bRetCode && pCommandBuffer)
                    {
                        m_mapSecondCommandCache[eGraphicContext][m_AliveCacheId].insert(std::make_pair<>(sCmdBufferKey, pCommandBuffer));
                    }
                }
                m_lsDelayReleaseSecondaryCommandBuffer_Lock.unlock();
            }
        }

        if (pCommandBuffer)
        {
            // 拿出去用了，这个计数器清零
            pCommandBuffer->m_delayReleaseCounter = 0;

            if (pCommandBuffer->m_uFrameCount == NSEngine::GetRenderFrameMoveLoopCount())
            {
                // ASSERT(0 && "secondcommand 不能一帧使用多次");
                if (pcszCommandName && pcszCommandName[0])
                {
                    KGLogPrintf(KGLOG_WARNING, "secondcommand 不能一帧使用多次 %s", pcszCommandName);
                }
                else
                {
                    KGLogPrintf(KGLOG_WARNING, "secondcommand 不能一帧使用多次");
                }
            }
            pCommandBuffer->m_uFrameCount = NSEngine::GetRenderFrameMoveLoopCount();
        }
        return pCommandBuffer;
    }


    void KGraphicDevice::DoSecondCommandCacheGC(gfx::enumGraphicContext eGraphicContext)
    {
        PROF_CPU();
        //float fdeltaTime                 = NSEngine::g_sRenderTimer.GetDeltaTime();
        if (m_CacheLifeTime[eGraphicContext] < 0.0f)
        {
            uint32_t    uDeadCacheId = m_AliveCacheId ? 0 : 1;
            const auto& mp           = m_mapSecondCommandCache[eGraphicContext][uDeadCacheId];
            for (auto it : mp)
            {
                PROF_CPU("DoSecondCommandCacheGC_Traverse");
                gfx::KVulkanCommandBuffer* pCommandBuffer = it.second;
                DestroyCommandBuffer(pCommandBuffer);
            }
            {
                PROF_CPU("DoSecondCommandCacheGC_Clear");
                m_mapSecondCommandCache[eGraphicContext][uDeadCacheId].clear();
            }
            m_AliveCacheId                   = uDeadCacheId;
            m_CacheLifeTime[eGraphicContext] = SECOND_COMMANDBUFFER_LIFE_TIME;
        }
    }

    void KGraphicDevice::ClearSecondCommandCache()
    {
        BOOL bRetCode = false;
        for (uint32_t t = 0; t < 2; ++t)
        {
            for (uint32_t i = 0; i < gfx::CONTEXT_COUNT; ++i)
            {
                if (m_mapSecondCommandCache[i][t].empty())
                {
                    continue;
                }
                for (const auto& it : m_mapSecondCommandCache[i][t])
                {
                    gfx::KVulkanCommandBuffer* pCommandBuffer = it.second;
                    bRetCode                            = DestroyCommandBuffer(pCommandBuffer);
                    if (!bRetCode)
                    {
                        KGLogPrintf(KGLOG_ERR, "destroy command failed");
                    }
                }
                m_mapSecondCommandCache[i][t].clear();
            }
        }
    }

    enumTextureFormat KGraphicDevice::GetSwapChainSufaceFormat()
    {
        return m_swapChainSurfaceFormat;
    }

    KGraphicDevice* g_pGraphic = nullptr; // TODO: 删了吧球球了

    KGraphicDevice* GetGraphicDevice()
    {
        if (GetGfxApi() == GFX_API::GFX_VULKAN_API)
        {
            // ASSERT(g_pGraphic);
        }
        return g_pGraphic;
    }
} // namespace gfx

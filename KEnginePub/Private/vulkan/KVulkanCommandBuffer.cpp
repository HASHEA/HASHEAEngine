#include "KVulkanCommandBuffer.h"
#include "GFXVulkan.h"
#include "KVulkanDevice.h"
#include "KVulkanRenderFrameBuffer.h"
#include "KVulkanGraphicDevice.h"
#include "KVulkanInitializers.h"

////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    ////////////////////////////////////////////////////////////////////
    KVulkanCommandPool::KVulkanCommandPool()
    {
    }

    KVulkanCommandPool::~KVulkanCommandPool()
    {
        Destroy();
    }

    BOOL KVulkanCommandPool::Create(uint32_t uQueueFamilyIndex)
    {
        BOOL bRet = FALSE;

        m_vkCmdPool = GetVulkanDevice()->CreateCommandPool(uQueueFamilyIndex);
        KGLOG_PROCESS_ERROR(m_vkCmdPool);

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanCommandPool::Destroy()
    {
        BOOL bRet = FALSE;

        if (m_vkCmdPool)
        {
            vks::vkDestroyCommandPool(GetVulkanDevice()->m_pLogicalDevice, m_vkCmdPool, nullptr);
            m_vkCmdPool = VK_NULL_HANDLE;
        }

        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    void KVulkanCommandPool::Trim()
    {
        if (!m_vkCmdPool)
        {
            return;
        }

        vks::vkTrimCommandPool(GetVulkanDevice()->m_pLogicalDevice, m_vkCmdPool, 0);
    }

    void KVulkanCommandPool::SetObjectName(const char* szName)
    {
        GetVulkanDevice()->SetObjectLabel(m_vkCmdPool, szName);
    }

    VkCommandPool KVulkanCommandPool::GetCmdPool()
    {
        return m_vkCmdPool;
    }

    ////////////////////////////////////////////////////////////////////
    std::atomic<uint32_t> g_CmdSequenceId{ 0 };

    KVulkanCommandBuffer::KVulkanCommandBuffer()
    {
        m_uCmmitCode._uCmmitCode = 0;
        m_uRebuildFlag = 0;
        m_uRecordFrameId = 0;
        m_uDrawCallCount = 0;
        m_uDrawIndexCount = 0;
        m_uCommitRecordFrameId = 0;

        m_uRedirectFrameId = 0;
        m_uRedirectIndexCount = 0;
        m_uFrameCount = 0;
        m_delayReleaseCounter = 0;
        m_uId = g_CmdSequenceId++;
        m_CreateByThreadId = 0;
        m_uDrawPointCount = 0;

        //////////////////////////////////////////////////////////////////
        m_eLifecycleState = KCommandBufferStates::Invalid;
        m_pCommandBuffer = 0;
        m_commandLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        m_InheritanceInfo = vks::initializers::CommandBufferInheritanceInfo();
        m_cmdBufInfo = vks::initializers::CommandBufferBeginInfo();
        m_VkActiveRenderPass = VK_NULL_HANDLE;
        m_fence = VK_NULL_HANDLE;

        m_commandType = gfx::enumForProcessType::FOR_GRPAHIC;
#if GfxVulkanCommandBufferMemLeakDetect
        static uint32_t g_CommandBufferMemeLeakDetect = 0;
        g_CommandBufferMemeLeakDetect++;
        m_pMemLeckDetect = new uint8_t[g_CommandBufferMemeLeakDetect];
        m_nMemLeckAlloc = g_CommandBufferMemeLeakDetect;
        if (m_nMemLeckAlloc == 92)
        {
            int x = 0;
        }
        KGLogPrintf(KGLOG_INFO, "create cmd %d", m_nMemLeckAlloc);
#endif

        KX3DEngineMonitor* pEnginePerf = NSEngine::GetEngineMonitor();
        ++pEnginePerf->m_sGraphics.nVkCmdBufferCount;
    }

    KVulkanCommandBuffer::~KVulkanCommandBuffer()
    {
        Destroy();
        m_bManagedUploadingUsage = false;
#if GfxVulkanCommandBufferMemLeakDetect
        SAFE_DELETE_ARRAY(m_pMemLeckDetect);
#endif
        KX3DEngineMonitor* pEnginePerf = NSEngine::GetEngineMonitor();
        --pEnginePerf->m_sGraphics.nVkCmdBufferCount;
    }

    BOOL KVulkanCommandBuffer::Destroy()
    {
        BOOL bRet = FALSE;

        for (auto& iter : m_WaitSemaphores)
        {
            SAFE_RELEASE(iter);
        }
        m_WaitSemaphores.clear();

        for (auto& iter : m_SubmittedWaitSemaphores)
        {
            SAFE_RELEASE(iter);
        }
        m_SubmittedWaitSemaphores.clear();

        SAFE_RELEASE(m_pSignalSemaphore);

        for (auto& iter : m_SubmittedSCBs)
        {
            SAFE_RELEASE(iter);
        }
        m_SubmittedSCBs.clear();

        SAFE_RELEASE(m_fence);

        vks::KVulkanDevice* pDevice = GetVulkanDevice();
        if (m_pCommandBuffer)
        {
            pDevice->FreeCommandBuffer(m_pCommandBuffer, m_pCmdPool, m_commandLevel);
        }

        m_pCommandBuffer = VK_NULL_HANDLE;
        m_pCmdPool = VK_NULL_HANDLE;

        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    uint32_t KVulkanCommandBuffer::GetId()
    {
        return m_uId;
    }

    void KVulkanCommandBuffer::SetId(uint32_t id)
    {
        // this function is call only when is primyCommandbuffer
        ASSERT(id < MAX_SWAP_CHAIN_COUNT);
        if (id < MAX_SWAP_CHAIN_COUNT)
        {
            m_uId = id;
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "error command id %d", id);
        }
    }

    VkCommandBufferLevel GetVkCommandLevel(enumCommandBufferLevel level)
    {
        VkCommandBufferLevel ret = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        switch (level)
        {
        case COMMAND_BUFFER_LEVEL_PRIMARY:
            ret = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            break;
        case COMMAND_BUFFER_LEVEL_SECONDARY:
            ret = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            break;
        default:
            break;
        }
        return ret;
    }

    BOOL KVulkanCommandBuffer::Create(enumCommandBufferLevel eCmdBufferLevel, enumForProcessType eCmdType)
    {
        BOOL                        bResult = FALSE;
        VkResult                    hRetCode = VK_INCOMPLETE;
        VkCommandBufferAllocateInfo cmdBufAllocateInfo;
        VkCommandBuffer             pVkCmdBuffer = VK_NULL_HANDLE;

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();

        vks::KVulkanDevice* pDevice = GetVulkanDevice();
        m_commandLevel = GetVkCommandLevel(eCmdBufferLevel);

        VkCommandPool pVkCmdPool = pDevice->GetCommonPool(eCmdType);
        KGLOG_ASSERT_EXIT(pVkCmdPool);

        cmdBufAllocateInfo = vks::initializers::CommandBufferAllocateInfo(pVkCmdPool, m_commandLevel, 1);

        hRetCode = vks::vkAllocateCommandBuffers(pDevice->m_pLogicalDevice, &cmdBufAllocateInfo, &pVkCmdBuffer);
        KGLOG_COM_ASSERT_EXIT(hRetCode);

        m_commandType = eCmdType;
        m_pCommandBuffer = pVkCmdBuffer;
        m_pCmdPool = pVkCmdPool;

        if (m_commandLevel == VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {
            static int count = 0;
            m_fence = new KVulkanFence;
            KGLOG_ASSERT_EXIT(m_fence);

            BOOL bRetCode = m_fence->Create(false);
            KGLOG_ASSERT_EXIT(bRetCode);

            std::string str = "Primary Cmd";
            str += std::to_string(count++);
            m_fence->SetObjectName(str.c_str());
            ++pPerfMonitor->m_sGraphics.nVkPrimaryCommandBufferCount;
        }
        else
        {
            ++pPerfMonitor->m_sGraphics.nVkSecondaryCommandBufferCount;
        }


        m_eLifecycleState = KCommandBufferStates::Initial;

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanCommandBuffer::Create(enumCommandBufferLevel eCmdBufferLevel, enumForProcessType eCmdType, KVulkanCommandPool* pCommandPool)
    {
        PROF_CPU();
        BOOL     bResult = FALSE;
        VkResult vkRetCode = VK_INCOMPLETE;

        vks::KVulkanDevice* pDevice = GetVulkanDevice();
        m_commandLevel = GetVkCommandLevel(eCmdBufferLevel);

        VkCommandPool               pVkCmdPool = pCommandPool->GetCmdPool();
        VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::CommandBufferAllocateInfo(pVkCmdPool, m_commandLevel, 1);
        VkCommandBuffer             pVkCmdBuffer = VK_NULL_HANDLE;

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();

        vkRetCode = vks::vkAllocateCommandBuffers(pDevice->m_pLogicalDevice, &cmdBufAllocateInfo, &pVkCmdBuffer);
        KGLOG_COM_ASSERT_EXIT(vkRetCode);

        m_commandType = eCmdType;
        m_pCommandBuffer = pVkCmdBuffer;
        m_pCmdPool = pVkCmdPool;


        if (m_commandLevel == VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {
            static int count = 0;
            m_fence = new KVulkanFence;
            KGLOG_ASSERT_EXIT(m_fence);

            BOOL bRetCode = m_fence->Create(false);
            KGLOG_ASSERT_EXIT(bRetCode);

            std::string str = "Primary Cmd";
            str += std::to_string(count++);
            m_fence->SetObjectName(str.c_str());

            ++pPerfMonitor->m_sGraphics.nVkPrimaryCommandBufferCount;
        }
        else
        {
            ++pPerfMonitor->m_sGraphics.nVkSecondaryCommandBufferCount;
        }

        m_eLifecycleState = KCommandBufferStates::Initial;

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    VkCommandBuffer& KVulkanCommandBuffer::GetCommandBuffer()
    {
        return m_pCommandBuffer;
    }

    void KVulkanCommandBuffer::BeginDebugLabel(const char* szName)
    {
        VkDebugUtilsLabelEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        markerInfo.pLabelName = szName;
        vks::vkCmdBeginDebugUtilsLabelEXT(m_pCommandBuffer, &markerInfo);
    }

    void KVulkanCommandBuffer::EndDebugLabel()
    {
        vks::vkCmdEndDebugUtilsLabelEXT(m_pCommandBuffer);
    }

    void KVulkanCommandBuffer::SetObjectName(const char* szName)
    {
#ifdef _WIN32
        std::string id = std::to_string(m_uId);
        m_strName = szName + id;
        GetVulkanDevice()->SetObjectLabel(m_pCommandBuffer, m_strName.c_str());

        if (m_fence)
        {
            std::string strName = szName;
            strName += "_fence";
            strName += id;
            m_fence->SetObjectName(strName.c_str());
        }
#endif
    }

    void KVulkanCommandBuffer::OptickBeginGpuContext()
    {
#if USE_OPTICK
        m_prevContext = Optick::SetGpuContext(Optick::GPUContext(m_pCommandBuffer));
#endif
    }

    void KVulkanCommandBuffer::OptickEndGpuContext()
    {
#if USE_OPTICK
        Optick::SetGpuContext(m_prevContext);
#endif
    }

    void* KVulkanCommandBuffer::GetCommandPtr()
    {
        return m_pCommandBuffer;
    }

    const VkRenderPass& KVulkanCommandBuffer::GetCurrentPass() const
    {
        return m_VkActiveRenderPass;
    }

    void KVulkanCommandBuffer::SetCurRenderPass(VkRenderPass vkRenderPass)
    {
        m_VkActiveRenderPass = vkRenderPass;
    }

    KVulkanFence* KVulkanCommandBuffer::GetFence()
    {
        return m_fence;
    }

    BOOL KVulkanCommandBuffer::Begin(KVulkanRenderFrameBuffer* pVulkanRenderFrameBuffer)
    {
        BOOL     bRet = false;
        VkResult vkRetCode = VK_INCOMPLETE;
        KGLOG_ASSERT_EXIT(m_eLifecycleState == KCommandBufferStates::Initial);

        {
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::CommandBufferBeginInfo();
            m_cmdBufInfo = cmdBufInfo;

            if (pVulkanRenderFrameBuffer)
            {
                KVulkanRenderPass* renderPass = pVulkanRenderFrameBuffer->GetRenderPassPtr();
                VkRenderPass              pass = renderPass->GetPass();
                VkFramebuffer             framebuffer = pVulkanRenderFrameBuffer->GetFrameBuffer();
                m_cmdBufInfo.flags = (m_bSubmitOnce ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0) | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
                m_cmdBufInfo.pInheritanceInfo = &m_InheritanceInfo;
                m_InheritanceInfo.renderPass = pass;
                m_InheritanceInfo.framebuffer = framebuffer;
            }
            else
            {
                cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            }

            // https://community.intel.com/t5/Developing-Games-Graphics-on/Vulkan-Linux-Mesa-SIGBUS-Bus-error/m-p/1147985#M1576%3Fwapkw=vulkan%20ubuntu
            // intel 显卡 ubuntu crash在这里，貌似这个网址说的有点类是，估计是bug
            vkRetCode = vks::vkBeginCommandBuffer(m_pCommandBuffer, &m_cmdBufInfo);
            KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS);
        }

        m_eLifecycleState = KCommandBufferStates::Recording;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanCommandBuffer::End(std::function<void()> pfunBeforeEndCall)
    {
        BOOL     bResult = FALSE;
        VkResult vkRetCode = VK_INCOMPLETE;

        KG_PROCESS_SUCCESS(m_eLifecycleState == KCommandBufferStates::Initial);
        KGLOG_ASSERT_EXIT(m_eLifecycleState == KCommandBufferStates::Recording);

        ASSERT(m_VkActiveRenderPass == nullptr);

        if (pfunBeforeEndCall)
        {
            pfunBeforeEndCall();
        }

        vkRetCode = vks::vkEndCommandBuffer(m_pCommandBuffer);
        KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS);

        m_eLifecycleState = KCommandBufferStates::Executable;

    Exit1:
        bResult = TRUE;
    Exit0:

        return bResult;
    }

    BOOL KVulkanCommandBuffer::IsSubmitted()
    {
        return m_fence ? m_fence->IsSubmitted() : FALSE;
    }

    BOOL KVulkanCommandBuffer::QuerySignaled()
    {
        return m_fence ? m_fence->Query() : FALSE;
    }

    BOOL KVulkanCommandBuffer::QueryExecutable(BOOL bWait)
    {
        PROF_CPU();

        BOOL bResult = FALSE;
        if (m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {
            if (m_fence && m_fence->IsSubmitted())
            {
                m_fence->Query();

                if (m_uSubmittedFenceCounter < m_fence->GetSignalFenceCounter())
                {
                    goto Exit1;
                }
                if (m_uSubmittedFenceCounter >= m_fence->GetSignalFenceCounter())
                {
                    if (bWait)
                    {
                        KVulkanFence* kFence = m_fence;

                        KVulkanGraphicDevice* pGfxDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
                        pGfxDevice->WaitForFence(1, &kFence, TRUE, UINT64_MAX);
                        m_eLifecycleState = KCommandBufferStates::Executable;
                    }
                    else
                    {
                        goto Exit0;
                    }
                }
            }
        }
        else
        {
            if (m_fence)
            {
                KG_ASSERT_EXIT(m_uSubmittedFenceCounter <= m_fence->GetSignalFenceCounter());
            }
        }

    Exit1:
        if (m_fence)
        {
            m_fence->Reset();
        }
        RefreshPendingState();

        {
            PROF_CPU_DETAIL("SubmittedWaitSemaphores_Clear");
            for (auto& iter : m_SubmittedWaitSemaphores)
            {
                SAFE_RELEASE(iter);
            }
            m_SubmittedWaitSemaphores.clear();
        }

        {
            PROF_CPU_DETAIL("SingnalSemaphores_Clear");
            SAFE_RELEASE(m_pSignalSemaphore);
        }

        {
            PROF_CPU_DETAIL("SubmittedWaitSCBs_Clear");
            for (auto& iter : m_SubmittedSCBs)
            {
                iter->AfterSCBPending(this);
                SAFE_RELEASE(iter);
            }
            m_SubmittedSCBs.clear();
        }
        m_eLifecycleState = KCommandBufferStates::Initial;

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KVulkanCommandBuffer::Reset(BOOL bWait)
    {
        PROF_CPU_DETAIL();

        BOOL bResult = FALSE;
        if (m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {
            PROF_CPU("PRIMARY_Reset");
            // printf("[XXXXXXXXX] Reset 1, bWait:%d, cmbBuffer:%p \n", bWait ? 1 : 0, m_pCommandBuffer);
            if (m_fence && m_fence->IsSubmitted())
            {
                m_fence->Query();

                // printf("[XXXXXXXXX] Reset 2, bWait:%d, cmbBuffer:%p \n", bWait ? 1 : 0, m_pCommandBuffer);
                if (m_uSubmittedFenceCounter >= m_fence->GetSignalFenceCounter() && bWait)
                {
                    // printf("[XXXXXXXXX] Reset 3, bWait:%d, cmbBuffer:%p \n", bWait ? 1 : 0, m_pCommandBuffer);
                    KVulkanFence* kFence = m_fence;
                    KVulkanGraphicDevice* pGfxDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
                    if (pGfxDevice->WaitForFence(1, &kFence, TRUE, UINT64_MAX))
                    {
                        m_eLifecycleState = KCommandBufferStates::Executable;
                    }
                }

                if (m_uSubmittedFenceCounter < m_fence->GetSignalFenceCounter())
                {
                    // 正常情况走这里了
                    //  printf("[XXXXXXXXX] Reset 4, bWait:%d, cmbBuffer:%p \n", bWait ? 1 : 0, m_pCommandBuffer);
                    vks::vkResetCommandBuffer(m_pCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
                    m_fence->Reset();
                    goto Exit1;
                }
                else
                {
                    // 出了异常情况，command的fence就是没完成，只能最后再挽救一下
                    KVulkanFence* kFence = m_fence;
                    KVulkanGraphicDevice* pGfxDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
                    pGfxDevice->WaitForFence(1, &kFence, TRUE, UINT64_MAX);
                    m_eLifecycleState = KCommandBufferStates::Executable;
                    if (m_uSubmittedFenceCounter < m_fence->GetSignalFenceCounter())
                    {
                        // 挽救成功了
                        vks::vkResetCommandBuffer(m_pCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
                        m_fence->Reset();
                        goto Exit1;
                    }
                    else
                    {
                        // 还不行就肯定是驱动有啥问题了
                        KGLogPrintf(KGLOG_ERR, "primary commandbuffer SubmittedFenceCounter over times");
                        goto Exit0;
                    }
                }
            }
        }
        else
        {
            if (m_fence)
            {
                KGLOG_ASSERT_EXIT(m_uSubmittedFenceCounter <= m_fence->GetSignalFenceCounter());
            }
        }

    Exit1:
        RefreshPendingState();

        {
            PROF_CPU_DETAIL("SubmittedWaitSemaphores_Clear");
            for (auto& iter : m_SubmittedWaitSemaphores)
            {
                SAFE_RELEASE(iter);
            }
            m_SubmittedWaitSemaphores.clear();
        }

        {
            PROF_CPU_DETAIL("SingnalSemaphores_Clear");
            SAFE_RELEASE(m_pSignalSemaphore);
        }

        {
            PROF_CPU_DETAIL("SubmittedWaitSCBs_Clear");
            for (auto& iter : m_SubmittedSCBs)
            {
                iter->AfterSCBPending(this);
                SAFE_RELEASE(iter);
            }
            m_SubmittedSCBs.clear();
        }
        m_eLifecycleState = KCommandBufferStates::Initial;

        bResult = true;
    Exit0:
        vks::vkResetCommandBuffer(m_pCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        return bResult;
    }

    void KVulkanCommandBuffer::RefreshPendingState()
    {
        PROF_CPU_DETAIL();
        if (m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {
            if (m_fence && m_eLifecycleState == KCommandBufferStates::Pending)
            {
                m_fence->Query();

                if (m_uSubmittedFenceCounter < m_fence->GetSignalFenceCounter())
                {
                    m_eLifecycleState = KCommandBufferStates::Executable;
                }
            }

            for (auto iter : m_SubmittedSCBs)
            {
                iter->RefreshPendingState();
            }
        }
    }

    void KVulkanCommandBuffer::AddWaitSemaphores(KVulkanSemaphore** ppWaitSemaphores, uint32_t uNumOfWaitSemaphores)
    {
        CHECK_ASSERT(ppWaitSemaphores);
        CHECK_ASSERT(uNumOfWaitSemaphores > 0);

        if (uNumOfWaitSemaphores == 1)
        {
            auto iter = ppWaitSemaphores[0];
            iter->AddRef();
            m_WaitSemaphores.push_back(iter);
            m_WaitDstStageMasks.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }
        else
        {
            size_t uOldSize = m_WaitSemaphores.size();

            m_WaitSemaphores.resize(uOldSize + uNumOfWaitSemaphores);
            auto pPushData = m_WaitSemaphores.data() + uOldSize;
            for (uint32_t i = 0; i < uNumOfWaitSemaphores; i++)
            {
                auto iter = ppWaitSemaphores[i];
                iter->AddRef();
                pPushData[i] = iter;
            }

            // todo: set wait stage masks.
            m_WaitDstStageMasks.resize(m_WaitSemaphores.size(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }
    }

    BOOL KVulkanCommandBuffer::IsInitialState()
    {
        if (m_eLifecycleState == KCommandBufferStates::Initial)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    BOOL KVulkanCommandBuffer::WaitForFence(uint64_t nanosecond)
    {
        BOOL                 bRet = false;
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        VkDevice             pDevice = GetVkDevice();

        if (m_fence->IsSubmitted())
        {
            gfx::FenceStatus fenceStatus = gfx::FENCE_STATUS_INCOMPLETE;

            KVulkanFence* pkFence = m_fence;
            VkFence  pvkFence = m_fence->GetFence();
            VkResult vkRes = vks::vkGetFenceStatus(pDevice, pvkFence);
            if (vkRes == VK_SUCCESS)
            {
                fenceStatus = gfx::FENCE_STATUS_COMPLETE;
                bRet = true;
            }

            if (fenceStatus != gfx::FENCE_STATUS_COMPLETE)
            {
                // pGraphicDevice->WaitForFence(1, &pkFence, true, UINT64_MAX);
                if (pGraphicDevice->WaitForFence(1, &pkFence, true, nanosecond))
                {
                    bRet = true;
                    pGraphicDevice->ResetFences(1, &pkFence);
                }
            }
        }
        return bRet;
    }


    BOOL KVulkanCommandBuffer::OnSCBExecute(KVulkanCommandBuffer* pPrimaryCmdBuffer)
    {
        BOOL bResult = false;

        KGLOG_ASSERT_EXIT(m_commandLevel == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        KGLOG_ASSERT_EXIT(pPrimaryCmdBuffer);
        KGLOG_ASSERT_EXIT(pPrimaryCmdBuffer->m_commandLevel == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        KGLOG_ASSERT_EXIT(m_eLifecycleState == KCommandBufferStates::Executable);

        KG_PROCESS_SUCCESS(pPrimaryCmdBuffer == m_pLastPrimary);

        SAFE_RELEASE(m_fence);

        m_fence = pPrimaryCmdBuffer->m_fence;
        m_fence->AddRef();
        m_uSubmittedFenceCounter = m_fence->GetSignalFenceCounter();

        m_pLastPrimary = pPrimaryCmdBuffer;
    Exit1:
        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanCommandBuffer::AfterSCBPending(KVulkanCommandBuffer* pPrimaryCmdBuffer)
    {
        BOOL bResult = false;

        KGLOG_ASSERT_EXIT(m_commandLevel == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        // KGLOG_ASSERT_EXIT(m_eLifecycleState == States::Executable); // Secondary command buffer may not have pending state.

        if (m_pLastPrimary == pPrimaryCmdBuffer)
        {
            SAFE_RELEASE(m_fence);
            m_pLastPrimary = nullptr;
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    KVulkanCommandBuffer::_CmmitCode& KVulkanCommandBuffer::_CmmitCode::operator=(const _CmmitCode& other)
    {
        this->_uCmmitCode = other._uCmmitCode;
        return *this;
    }

    KVulkanCommandBuffer::_CmmitCode& KVulkanCommandBuffer::_CmmitCode::operator=(const uint64_t& other)
    {
        this->_uCmmitCode = other;
        return *this;
    }
    bool KVulkanCommandBuffer::_CmmitCode::operator==(const uint64_t& other) const
    {
        BOOL bRet = this->_uCmmitCode == other;
        // return bRet;
        return false;
    }
    bool KVulkanCommandBuffer::_CmmitCode::operator!=(const uint64_t& other) const
    {
        BOOL bRet = this->_uCmmitCode != other;
        // return bRet;
        return true;
    }
    KVulkanCommandBuffer::_CmmitCode::operator uint64_t()
    {
        return this->_uCmmitCode;
    }
}

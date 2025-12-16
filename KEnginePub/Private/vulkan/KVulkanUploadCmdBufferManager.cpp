#include "../stdafx.h"
#include "KVulkanUploadCmdBufferManager.h"
#include "KVulkanDevice.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KVulkanGraphicDevice.h"
#include "KVulkanCommandBuffer.h"

/////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    void KVulkanUploadCmdBufferManager::PoolManager::Init(KVulkanCommandPool* pCmdPool, enumForProcessType eCommandType)
    {
        ASSERT(pCmdPool);
        ASSERT(!m_pCmdPool);

        m_pCmdPool     = pCmdPool;
        m_eCommandType = eCommandType;
    }

    void KVulkanUploadCmdBufferManager::PoolManager::ClearNotifyList()
    {
    }

    void KVulkanUploadCmdBufferManager::PoolManager::Trim()
    {
        if (m_pCmdPool)
        {
            m_pCmdPool->Trim();
        }
    }

    void KVulkanUploadCmdBufferManager::PoolManager::Uninit()
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        auto                        pGfxDevice = GetGraphicDevice();
        pGfxDevice->DeviceWaitIdle();

        for (auto& iter : m_vecFreeCmdBuffers)
        {
            iter->Destroy();
            SAFE_DELETE(iter);
        }
        m_vecFreeCmdBuffers.clear();

        for (auto& iter : m_vecActiveCmdBuffers)
        {
            iter->Destroy();
            SAFE_DELETE(iter);
        }
        m_vecActiveCmdBuffers.clear();

        if (m_pCmdPool)
        {
            KVulkanCommandPool* pCmdPool = m_pCmdPool;
            pGfxDevice->DestroyCommandPool(pCmdPool);

            m_pCmdPool = nullptr;
        }
    }

    KVulkanCommandBuffer* gfx::KVulkanUploadCmdBufferManager::PoolManager::AllocCmdBuffer()
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        KVulkanCommandBuffer*       pCmdBuffer = nullptr;
        BOOL                        bRetCode   = false;

        // for (int32_t i = (int32_t)m_vecFreeCmdBuffers.size() - 1; i >= 0; --i)
        //{
        //	KVulkanCommandBuffer* iter = m_vecFreeCmdBuffers[i];

        //	if (i < (int32_t)m_vecFreeCmdBuffers.size() - 1)
        //		std::swap(m_vecFreeCmdBuffers[i], m_vecFreeCmdBuffers.back());

        //	m_vecFreeCmdBuffers.pop_back();

        //	m_vecActiveCmdBuffers.push_back(iter);
        //	pCmdBuffer = iter;

        //	//if (pCmdBuffer)
        //	//{
        //	//	break;
        //	//}
        //}

        while (!m_vecFreeCmdBuffers.empty())
        {
            pCmdBuffer = m_vecFreeCmdBuffers.back();
            m_vecFreeCmdBuffers.pop_back();
            if (pCmdBuffer)
            {
                m_vecActiveCmdBuffers.push_back(pCmdBuffer);
                break;
            }
        }

        if (!pCmdBuffer)
        {
            pCmdBuffer = new KVulkanCommandBuffer;
            KGLOG_ASSERT_EXIT(pCmdBuffer);

            bRetCode = pCmdBuffer->Create(enumCommandBufferLevel::COMMAND_BUFFER_LEVEL_PRIMARY, m_eCommandType, m_pCmdPool);
            KGLOG_ASSERT_EXIT(bRetCode);

            m_vecActiveCmdBuffers.push_back(pCmdBuffer);
        }
    Exit0:
        return pCmdBuffer;
    }

    void KVulkanUploadCmdBufferManager::PoolManager::FreeUnusedCmdBuffers(BOOL bWait)
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        for (int32_t i = (int32_t)m_vecActiveCmdBuffers.size() - 1; i >= 0; --i)
        {
            KVulkanCommandBuffer*& iter = m_vecActiveCmdBuffers[i];
            iter->RefreshPendingState();

            if (iter->m_fence->Query() || bWait)
            {
                BOOL bRetCode = iter->Reset(bWait);
                ASSERT(bRetCode);

                m_vecFreeCmdBuffers.push_back(iter);
                iter = nullptr;

                if (i < (int32_t)m_vecActiveCmdBuffers.size() - 1)
                    std::swap(m_vecActiveCmdBuffers[i], m_vecActiveCmdBuffers.back());

                m_vecActiveCmdBuffers.pop_back();
            }
        }
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////


    KVulkanUploadCmdBufferManager::KVulkanUploadCmdBufferManager()
    {
    }

    KVulkanUploadCmdBufferManager::~KVulkanUploadCmdBufferManager()
    {
        ASSERT(m_vecUploadSignalSemaphores.empty());
        // ASSERT(m_pUploadCmdBuffer == nullptr);

        ASSERT(m_pUploadCmdBuffer == nullptr);
        ASSERT(m_pUploadSemaphore == nullptr);
    }

    BOOL KVulkanUploadCmdBufferManager::Init()
    {
        BOOL bResult = false;


        // 纹理加载还是得要graphic queue, 暂且先用graphic queue
        // enumForProcessType eCommandType = enumForProcessType::FOR_TRANSFER;
        enumForProcessType eCommandType = enumForProcessType::FOR_GRPAHIC;

        KVulkanGraphicDevice* pGfxDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KGLOG_ASSERT_EXIT(pGfxDevice);

        // m_pQueue = pGfxDevice->GetTransferQueue();
        // if (!m_pQueue)
        //{
        m_pQueue = pGfxDevice->GetGraphicQueue();
        KGLOG_ASSERT_EXIT(m_pQueue);
        //}

        {
            auto     pkvkDevice        = GetVulkanDevice();
            uint32_t uQueueFamilyIndex = pkvkDevice->GetQueueFamilyIndexProcessType(eCommandType);
            KGLOG_ASSERT_EXIT(uQueueFamilyIndex != UINT32_MAX);

            KVulkanCommandPool* pCmdPool = nullptr;
            pGfxDevice->CreateCommandPool((KVulkanCommandPool**)&pCmdPool, uQueueFamilyIndex);
            KGLOG_ASSERT_EXIT(pCmdPool);
            pCmdPool->SetObjectName("UpLoadCommandPool");
            m_UploadCmdPool.Init(pCmdPool, eCommandType);
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    void KVulkanUploadCmdBufferManager::Uninit()
    {
        m_pUploadCmdBuffer = nullptr;
        SAFE_RELEASE(m_pUploadSemaphore);

        m_UploadCmdPool.Uninit();


        m_pQueue = nullptr;
        for (auto& iter : m_vecUploadSignalSemaphores)
        {
            SAFE_RELEASE(iter);
        }
        m_vecUploadSignalSemaphores.clear();
    }

    KVulkanCommandBuffer* KVulkanUploadCmdBufferManager::GetUploadCmdBuffer()
    {
        ASSERT(IsMainThread());
        BOOL bRetCode = FALSE;

        if (!m_pUploadCmdBuffer)
        {
            KVulkanGraphicDevice* pGfxDevice = (KVulkanGraphicDevice*)GetGraphicDevice();

            if (!m_pUploadSemaphore)
            {
                bRetCode = pGfxDevice->CreateSemaphoreA((KVulkanSemaphore**)&m_pUploadSemaphore);
                KGLOG_CHECK_ERROR(bRetCode);
                ASSERT(m_pUploadSemaphore);
            }

            m_pUploadCmdBuffer = m_UploadCmdPool.AllocCmdBuffer();
            ASSERT(m_pUploadCmdBuffer);

            m_pUploadCmdBuffer->MarkManagedUploadingUsage();
            BOOL bRetCode = pGfxDevice->BeginCommandBuffer(m_pUploadCmdBuffer);

            m_pUploadCmdBuffer->BeginDebugLabel("StageUpdatePass_");
            m_pUploadCmdBuffer->SetObjectName("UploadCmdBuffer_");
            ASSERT(bRetCode);

            // printf("[XXXXXXXXX] Create ActionBuffer:%p \n", m_pUploadCmdBuffer->GetCommandBuffer());
        }

        return m_pUploadCmdBuffer;
    }

    void KVulkanUploadCmdBufferManager::SubmitUploadCmdBuffer(BOOL bAllocateSemaphores)
    {
        KVulkanGraphicDevice* pGfxDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        ASSERT(pGfxDevice);

        if (m_pUploadCmdBuffer)
        {
            m_pUploadCmdBuffer->EndDebugLabel();
            pGfxDevice->EndCommandBuffer(m_pUploadCmdBuffer);

            if (bAllocateSemaphores)
            {
                pGfxDevice->QueueSubmitInternal(m_pQueue, m_pUploadCmdBuffer, false, m_pUploadSemaphore);

                m_pUploadSemaphore->AddRef();
                m_vecUploadSignalSemaphores.push_back(m_pUploadSemaphore);

                // printf("[XXXXXXXXX] Submit ActionBuffer:%p \n", m_pUploadCmdBuffer->GetCommandBuffer());
            }
            else
            {
                pGfxDevice->QueueSubmitInternal(m_pQueue, m_pUploadCmdBuffer, false, nullptr);
            }
            m_pUploadCmdBuffer = nullptr;
        }
    }

    void KVulkanUploadCmdBufferManager::FreeUnusedCmdBuffers(BOOL bWait)
    {
        m_UploadCmdPool.FreeUnusedCmdBuffers(bWait);
    }

    void KVulkanUploadCmdBufferManager::DependOnUploadSemaphores(KVulkanCommandBuffer* pRenderCmdBuffer)
    {
        if (!m_vecUploadSignalSemaphores.empty())
        {
            pRenderCmdBuffer->AddWaitSemaphores(m_vecUploadSignalSemaphores.data(), (uint32_t)m_vecUploadSignalSemaphores.size());

            for (auto& iter : m_vecUploadSignalSemaphores)
            {
                SAFE_RELEASE(iter);
            }
            m_vecUploadSignalSemaphores.clear();
        }
    }

    void KVulkanUploadCmdBufferManager::Reset()
    {
        if (m_pUploadCmdBuffer)
        {
            // printf("[XXXXXXXXX] ResetActionBuffer:%p", m_pUploadCmdBuffer->GetCommandBuffer());
            m_pUploadCmdBuffer->Reset(true);
            m_pUploadCmdBuffer = nullptr;
        }
    }

    void KVulkanUploadCmdBufferManager::ClearNotifyList()
    {
        m_UploadCmdPool.ClearNotifyList();
    }

    void KVulkanUploadCmdBufferManager::TrimCommandPool()
    {
        m_UploadCmdPool.Trim();
    }

} // namespace gfx

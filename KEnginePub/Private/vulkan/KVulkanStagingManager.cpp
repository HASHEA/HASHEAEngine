#include "../stdafx.h"
#include "KVulkanStagingManager.h"
#include "KVulkanInitializers.h"
#include "KVulkanDevice.h"
#include "KVulkanCommandBuffer.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KBase/Public/thread/KThread.h"
#include "Engine/KGLog.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/KProfileTools.h"

namespace gfx
{
    KVulkanStagingBuffer::KVulkanStagingBuffer()
    {
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->nVkStagingBufferCount;
    }

    KVulkanStagingBuffer::~KVulkanStagingBuffer()
    {
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->nVkStagingBufferCount;

        ASSERT(!m_vkBuffer);
        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            ASSERT(!m_pVmaAllocation);
        }
        // #else
        else
        {
            ASSERT(!m_vkDeviceMemory);
        }
        // #endif
        ASSERT(!m_pMappedPtr);
        ASSERT(!m_pFence);
    }

    BOOL KVulkanStagingBuffer::IsMapped() const
    {
        return m_bMapped;
    }

    BOOL KVulkanStagingBuffer::Map(uint32_t uOffset, uint32_t uSize)
    {
        PROF_CPU_DETAIL();
        BOOL     bResult   = FALSE;
        BOOL     bRetCode  = FALSE;
        VkResult vkRetCode = VK_ERROR_UNKNOWN;
        auto     pvkDevice = GetVkDevice();

        ASSERT(uSize <= m_uMemoryByteWidth);

        vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();
        ASSERT(pvkDevice);

        KG_PROCESS_SUCCESS(m_bPersistentMapping);

        KGLOG_ASSERT_EXIT(!m_bMapped);
        KGLOG_ASSERT_EXIT(pvkDevice);

        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            KGLOG_ASSERT_EXIT(m_pVmaAllocation);

            bRetCode = pkvkDevice->VMAMapMemory(m_pVmaAllocation, &m_pMappedPtr);
            KGLOG_ASSERT_EXIT(bRetCode);
        }
        // #else
        else
        {
            KGLOG_ASSERT_EXIT(m_vkDeviceMemory);

            vkRetCode = vks::vkMapMemory(pvkDevice, m_vkDeviceMemory, m_uMemoryOffset, m_uMemoryByteWidth, 0, &m_pMappedPtr);
            KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);
        }
        // #endif

    Exit1:
        m_bMapped          = true;
        m_uMappedDstOffset = uOffset;
        m_uMappedDstSize   = uSize;

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanStagingBuffer::Unmap()
    {
        PROF_CPU_DETAIL();
        BOOL bResult   = FALSE;
        BOOL bRetCode  = FALSE;
        auto pvkDevice = GetVkDevice();

        vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();
        ASSERT(pvkDevice);

        KG_PROCESS_SUCCESS(m_bPersistentMapping);

        KGLOG_ASSERT_EXIT(m_bMapped);
        KGLOG_ASSERT_EXIT(pvkDevice);

        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            KGLOG_ASSERT_EXIT(m_pVmaAllocation);

            bRetCode = pkvkDevice->VMAUnmapMemory(m_pVmaAllocation);
            KGLOG_ASSERT_EXIT(bRetCode);
        }
        // #else
        else
        {
            vks::vkUnmapMemory(pvkDevice, m_vkDeviceMemory);
        }
        // #endif
        m_pMappedPtr = nullptr;

    Exit1:
        m_bMapped          = false;
        m_uMappedDstOffset = 0;
        m_uMappedDstSize   = 0;

        bResult = true;
    Exit0:
        return bResult;
    }

    void KVulkanStagingBuffer::GetMappedRange(VkDeviceSize& uOffset, VkDeviceSize& uSize)
    {
        ASSERT(m_bMapped);
        uOffset = m_uMappedDstOffset;
        uSize   = m_uMappedDstSize;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////
    KVulkanStagingManager::KVulkanStagingManager(KVulkanGraphicDevice* gfxDevice)
        : m_gfxDevice(gfxDevice)
    {
    }

    KVulkanStagingManager::~KVulkanStagingManager()
    {
        m_gfxDevice = nullptr;
        ASSERT(m_vecUsedBuffers.empty());
        ASSERT(m_vecPendingFreeBuffers.empty());
        ASSERT(m_vecFreeBuffers.empty());
    }

    void KVulkanStagingManager::Uninit()
    {
        PROF_CPU();
        ASSERT(IsMainThread());

        std::lock_guard<std::mutex> lock(m_Lock);
        VkDevice                    pvkDevice = GetVkDevice();
        if (pvkDevice)
        {
            vks::vkDeviceWaitIdle(pvkDevice);


            for (auto& iter : m_vecUsedBuffers)
            {
                SAFE_RELEASE(iter->m_pFence);
                _DestroyBuffer(iter);
                SAFE_DELETE(iter);
            }
            m_vecUsedBuffers.clear();

            for (auto& iter : m_vecPendingFreeBuffers)
            {
                SAFE_RELEASE(iter->m_pFence);
                _DestroyBuffer(iter);
                SAFE_DELETE(iter);
            }
            m_vecPendingFreeBuffers.clear();

            for (auto& iter : m_vecFreeBuffers)
            {
                _DestroyBuffer(iter.pStagingBuffer);
                SAFE_DELETE(iter.pStagingBuffer);
            }
            m_vecFreeBuffers.clear();
        }
    }

    KVulkanStagingBuffer* KVulkanStagingManager::AllocBuffer(uint32_t uByteWidth, const KVK_SUBRESOURCE_DATA* pData, bool bReadOperation)
    {
        PROF_CPU_DETAIL();

        std::lock_guard<std::mutex> lock(m_Lock);
        BOOL                        bResult    = FALSE;
        BOOL                        bRetCode   = FALSE;
        VkResult                    vkRetCode  = VK_INCOMPLETE;
        KVulkanStagingBuffer*       pRetBuffer = nullptr;

        VkBufferUsageFlags vkUsageFlags     = bReadOperation ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkBufferCreateInfo bufferCreateInfo = vks::initializers::BufferCreateInfo(vkUsageFlags, uByteWidth);

        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc = vks::initializers::MemoryAllocateInfo();

        VkDevice pvkDevice = GetVkDevice();
        ASSERT(pvkDevice);

        vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();
        ASSERT(pvkDevice);

        // find the match one.
        auto FreeBuffers = m_vecFreeBuffers.data();
        // 优先找相等的
        for (int32_t i = (int32_t)m_vecFreeBuffers.size() - 1; i >= 0; i--)
        {
            auto& iter           = FreeBuffers[i];
            auto  pStagingBuffer = iter.pStagingBuffer;
            CHECK_ASSERT(pStagingBuffer);

            if (pStagingBuffer->m_uRequiredSize == uByteWidth && pStagingBuffer->IsReadUsage() == bReadOperation)
            {
                // remove from m_vecFreeBuffers
                if (i < (int32_t)m_vecFreeBuffers.size() - 1)
                    std::swap(FreeBuffers[i], m_vecFreeBuffers.back());

                m_vecFreeBuffers.pop_back();

                pRetBuffer = pStagingBuffer;
                break;
            }
        }

        // 没有相等，就找容量够用的,但是比需求大很多的不要，防止内存冗余
        if (!pRetBuffer)
        {
            for (int32_t i = (int32_t)m_vecFreeBuffers.size() - 1; i >= 0; i--)
            {
                auto& iter           = FreeBuffers[i];
                auto  pStagingBuffer = iter.pStagingBuffer;
                CHECK_ASSERT(pStagingBuffer);

                if (pStagingBuffer->m_uRequiredSize >= uByteWidth && pStagingBuffer->m_uRequiredSize <= uByteWidth + STAGE_BUFFER_MATCH_FIT_SIZE && pStagingBuffer->IsReadUsage() == bReadOperation)
                {
                    // remove from m_vecFreeBuffers
                    if (i < (int32_t)m_vecFreeBuffers.size() - 1)
                        std::swap(FreeBuffers[i], m_vecFreeBuffers.back());

                    m_vecFreeBuffers.pop_back();

                    pRetBuffer = pStagingBuffer;
                    break;
                }
            }
        }

        // no match one, new
        if (!pRetBuffer)
        {
            pRetBuffer = new KVulkanStagingBuffer;
            KGLOG_ASSERT_EXIT(pRetBuffer);

            // #if X3D_VK_USE_VMA
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                bRetCode = pkvkDevice->VMACreateBuffer(uByteWidth, vkUsageFlags, VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_ONLY, pRetBuffer->m_vkBuffer, pRetBuffer->m_pVmaAllocation, &pRetBuffer->m_pMappedPtr);
                KGLOG_ASSERT_EXIT(bRetCode);
                pRetBuffer->m_bPersistentMapping = (pRetBuffer->m_pMappedPtr != nullptr);

                pRetBuffer->m_uMemoryByteWidth = uByteWidth;
            }
            // #else
            else
            {
                vkRetCode = vks::vkCreateBuffer(pvkDevice, &bufferCreateInfo, KVK_ALLOCATER, &pRetBuffer->m_vkBuffer);
                KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

                vks::vkGetBufferMemoryRequirements(pvkDevice, pRetBuffer->m_vkBuffer, &memReqs);
                memAlloc.allocationSize  = memReqs.size;
                memAlloc.memoryTypeIndex = pkvkDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                vkRetCode = pkvkDevice->AllocateMemory(pvkDevice, &memAlloc, KVK_ALLOCATER, &pRetBuffer->m_vkDeviceMemory, &pRetBuffer->m_uMemoryOffset, (uint32_t)memReqs.alignment);
                KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

                vkRetCode = vks::vkBindBufferMemory(pvkDevice, pRetBuffer->m_vkBuffer, pRetBuffer->m_vkDeviceMemory, pRetBuffer->m_uMemoryOffset);
                KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

                pRetBuffer->m_uMemoryByteWidth = (uint32_t)memReqs.size;

                vkRetCode = vks::vkMapMemory(pvkDevice, pRetBuffer->m_vkDeviceMemory, pRetBuffer->m_uMemoryOffset, pRetBuffer->m_uMemoryByteWidth, 0, &pRetBuffer->m_pMappedPtr);
                KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);
                pRetBuffer->m_bPersistentMapping = (pRetBuffer->m_pMappedPtr != nullptr);

            }
            // #endif
            pRetBuffer->m_bReadOperation = bReadOperation;
            pRetBuffer->m_uRequiredSize  = uByteWidth;
        }

        // copy data into memory
        if (pData && pData->DataPtr)
        {
            bRetCode = pRetBuffer->Map(0, pRetBuffer->GetMemorySize());
            KGLOG_ASSERT_EXIT(bRetCode);

            memcpy(pRetBuffer->m_pMappedPtr, pData->DataPtr, pRetBuffer->m_uRequiredSize);

            pRetBuffer->Unmap();
        }

        m_vecUsedBuffers.push_back(pRetBuffer);

        // Exit1:
        bResult = TRUE;
    Exit0:
        if (!bResult && pRetBuffer)
        {
            _DestroyBuffer(pRetBuffer);
            SAFE_DELETE(pRetBuffer);
        }

        return pRetBuffer;
    }

    void KVulkanStagingManager::FreeBuffer(KVulkanCommandBuffer* pCmdBuffer, KVulkanStagingBuffer* pDstBuffer)
    {
        PROF_CPU_DETAIL();
        ASSERT(IsMainThread());

        // todo: use KGList instead may free the find position time.
        std::lock_guard<std::mutex> lock(m_Lock);
        auto                        ifind = std::find(m_vecUsedBuffers.begin(), m_vecUsedBuffers.end(), pDstBuffer);
        KGLOG_ASSERT_EXIT(ifind != m_vecUsedBuffers.end());
        m_vecUsedBuffers.erase(ifind);

        if (pCmdBuffer)
        {
            pDstBuffer->m_pFence = pCmdBuffer->m_fence;
            pDstBuffer->m_pFence->AddRef();

            pDstBuffer->m_uSignalFenceCounter = pDstBuffer->m_pFence->GetSignalFenceCounter();
            m_vecPendingFreeBuffers.push_back(pDstBuffer);
        }
        else if (pDstBuffer->m_pFence)
        {
            m_vecPendingFreeBuffers.push_back(pDstBuffer);
        }
        else
        {
            FREE_BUFFER_ITEM itemFree;
            itemFree.pStagingBuffer = pDstBuffer;
            itemFree.uFrameNum      = (uint32_t)NSEngine::GetRenderFrameMoveLoopCount();

            m_vecFreeBuffers.push_back(itemFree);
        }
    Exit0:
        return;
    }

    BOOL KVulkanStagingManager::WaitStagingTaskFinished(KVulkanStagingBuffer* pStagingBuffer)
    {
        PROF_CPU();
        if (pStagingBuffer->m_pFence)
        {
            if (pStagingBuffer->m_uSignalFenceCounter >= pStagingBuffer->m_pFence->GetSignalFenceCounter())
            {
                if (!pStagingBuffer->m_pFence->IsSubmitted())
                    return false;

                KVulkanFence* pFence = pStagingBuffer->m_pFence;
                m_gfxDevice->WaitForFence(1, &pFence, true, UINT64_MAX);

                SAFE_RELEASE(pStagingBuffer->m_pFence);
                pStagingBuffer->m_uSignalFenceCounter = UINT64_MAX;
            }
        }
        return true;
    }

    BOOL KVulkanStagingManager::CanBeMapped(KVulkanStagingBuffer* pStagingBuffer)
    {
        PROF_CPU_DEEP();
        if (pStagingBuffer->m_pFence)
        {
            if (pStagingBuffer->m_uSignalFenceCounter < pStagingBuffer->m_pFence->GetSignalFenceCounter())
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    void KVulkanStagingManager::FrameMove()
    {
        PROF_CPU();
        _QueuyPendingFreeBuffers();
        _ProcessFreeBuffers(100);
    }

    void KVulkanStagingManager::_QueuyPendingFreeBuffers()
    {
        PROF_CPU();
        std::lock_guard<std::mutex> lock(m_Lock);
        auto                        PendingBuffers = m_vecPendingFreeBuffers.data();
        for (int32_t i = (int32_t)m_vecPendingFreeBuffers.size() - 1; i >= 0; --i)
        {
            auto& pIterBuffer = PendingBuffers[i];
            if (CanBeMapped(pIterBuffer))
            {
                SAFE_RELEASE(pIterBuffer->m_pFence);
                pIterBuffer->m_uSignalFenceCounter = UINT64_MAX;

                FREE_BUFFER_ITEM itemFree;
                itemFree.pStagingBuffer = pIterBuffer;
                itemFree.uFrameNum      = (uint32_t)NSEngine::GetRenderFrameMoveLoopCount();

                m_vecFreeBuffers.push_back(itemFree);
                pIterBuffer = nullptr;

                // remove from m_vecPendingFreeBuffers
                if (i < (int32_t)m_vecPendingFreeBuffers.size() - 1)
                    std::swap(PendingBuffers[i], m_vecPendingFreeBuffers.back());

                m_vecPendingFreeBuffers.pop_back();
            }
        }
    }

    void KVulkanStagingManager::_ProcessFreeBuffers(uint32_t uNumNeedToFree)
    {
        PROF_CPU();
        ASSERT(IsMainThread());
        std::lock_guard<std::mutex> lock(m_Lock);

        uint32_t uDelayFreeFrameCount = DELAY_RELEASE_FRAME_COUNT;

        uint32_t uFreeCounter       = 0;
        uint32_t uCurrentFrameCount = (uint32_t)NSEngine::GetRenderFrameMoveLoopCount();

        auto FreeBuffers = m_vecFreeBuffers.data();
        for (int32_t i = (int32_t)m_vecFreeBuffers.size() - 1; i >= 0; i--)
        {
            auto& item = FreeBuffers[i];
            if (item.uFrameNum + uDelayFreeFrameCount < uCurrentFrameCount)
            {
                _DestroyBuffer(item.pStagingBuffer);
                SAFE_DELETE(item.pStagingBuffer);

                // remove from m_vecFreeBuffers
                if (i < (int32_t)m_vecFreeBuffers.size() - 1)
                    std::swap(FreeBuffers[i], m_vecFreeBuffers.back());

                m_vecFreeBuffers.pop_back();

                if (uNumNeedToFree != 0)
                {
                    ++uFreeCounter;
                    if (uFreeCounter >= uNumNeedToFree)
                    {
                        break;
                    }
                }
            }
        }
    }

    void KVulkanStagingManager::_DestroyBuffer(KVulkanStagingBuffer* pStagingBuffer)
    {
        PROF_CPU();
        if (pStagingBuffer == nullptr)
            return;

        VkDevice pvkDevice = GetVkDevice();
        ASSERT(pvkDevice);
        ASSERT(!pStagingBuffer->m_pFence);
        ASSERT(!pStagingBuffer->IsMapped());

        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            if (pStagingBuffer->m_vkBuffer)
            {
                vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();
                ASSERT(pkvkDevice);

                if (pStagingBuffer->m_bPersistentMapping)
                {
                    pStagingBuffer->m_pMappedPtr = nullptr;
                }

                pkvkDevice->VMADestroyBuffer(pStagingBuffer->m_vkBuffer, pStagingBuffer->m_pVmaAllocation);
                ASSERT(!pStagingBuffer->m_vkBuffer);
                ASSERT(!pStagingBuffer->m_pVmaAllocation);
            }
        }
        // #else
        else
        {
            if (pStagingBuffer->m_pMappedPtr && pStagingBuffer->m_vkDeviceMemory)
            {
                vks::vkUnmapMemory(pvkDevice, pStagingBuffer->m_vkDeviceMemory);
                pStagingBuffer->m_pMappedPtr = nullptr;
            }

            if (pStagingBuffer->m_vkBuffer)
            {
                vks::vkDestroyBuffer(pvkDevice, pStagingBuffer->m_vkBuffer, KVK_ALLOCATER);
                pStagingBuffer->m_vkBuffer = nullptr;
            }

            if (pStagingBuffer->m_vkDeviceMemory)
            {
                vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();
                ASSERT(pkvkDevice);

                pkvkDevice->FreeMemory(
                    pvkDevice,
                    pStagingBuffer->m_vkDeviceMemory,
                    KVK_ALLOCATER,
                    pStagingBuffer->m_uMemoryOffset,
                    pStagingBuffer->m_uMemoryByteWidth
                );

                pStagingBuffer->m_vkDeviceMemory = nullptr;
            }
        }
        // #endif
        pStagingBuffer->m_uMemoryOffset    = 0;
        pStagingBuffer->m_uMemoryByteWidth = 0;
        pStagingBuffer->m_uRequiredSize    = 0;
    }

} // namespace gfx

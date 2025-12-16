#include "../stdafx.h"
#include "../IGFX_Private.h"
#include "KVulkanBuffer.h"
#include "KVulkanDevice.h"
#include "KVulkanUploadCmdBufferManager.h"
#include "KVulkanStagingManager.h"
#include "KVulkanBindlessManager.h"
#include "KVulkanInitializers.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KVulkanDynamicRingBuffer.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KVulkanGraphicDevice.h"
#include "Engine/KGLog.h"
//////////////////////////////////////////////////////////////////////////
#include "KGFX_GraphicDeviceVK.h"
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/KProfileTools.h"

namespace gfx
{
    uint32_t g_GfxBufferId{ 0 };
    uint32_t gfxMem_Count = 0;

    KVulkanBuffer::KVulkanBuffer()
    {
        m_uId = ++g_GfxBufferId;
        uHashCode = 0;
        m_bNeedUpdate = false;
        m_uNeedUpdateSize = 0;

#if GfxBufferMemLeakDetect
        gfxMem_Count++;
        if (gfxMem_Count == 201)
        {
            // 看泄多少字节，这里下断点就是现场了
            int x = 0;
        }
        m_memLeakDetect = new char[gfxMem_Count];
#endif
        ZeroMemory(&m_bufDesc, sizeof(m_bufDesc));
        m_nFrameCount = -1;
        m_vkDescriptorBufferInfo.offset = 0;
        m_vkDescriptorBufferInfo.buffer = nullptr;
        m_vkDescriptorBufferInfo.range = 0;
    }

    KVulkanBuffer::~KVulkanBuffer()
    {
        ASSERT(m_nRef == 0);
        Destroy();

        ASSERT(!m_pvkBuffer);
        ASSERT(!m_pStagingBuffer);

#if GfxBufferMemLeakDetect
        SAFE_DELETE_ARRAY(m_memLeakDetect);
#endif
    }

    int32_t KVulkanBuffer::AddRef()
    {
        return ++m_nRef;
    }

    int32_t KVulkanBuffer::GetRef()
    {
        return m_nRef;
    }

    int32_t KVulkanBuffer::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            if (m_pvkBuffer != VK_NULL_HANDLE)
            {
                auto piDevice = KGFX_GetGraphicDeviceVKInternal();
                CHECK_ASSERT(piDevice);

                piDevice->GC_DelayReleaseObject(this);
            }
            else
            {
                // 如果没有创建缓冲对象，直接释放
                delete this;
            }
        }

        return nRef;
    }

    VkBufferUsageFlags GetVkBufferUsageFlags(BufferUsageFlags uUsageFlags)
    {
        VkBufferUsageFlags uVkBufferUsageFlags = 0;

        // These macros/constants should be defined in your engine or Vulkan headers
        if (uUsageFlags & BUFFER_USAGE_TRANSFER_SRC_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (uUsageFlags & BUFFER_USAGE_TRANSFER_DST_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (uUsageFlags & BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        if (uUsageFlags & BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
        if (uUsageFlags & BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (uUsageFlags & BUFFER_USAGE_STORAGE_BUFFER_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (uUsageFlags & BUFFER_USAGE_INDEX_BUFFER_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (uUsageFlags & BUFFER_USAGE_VERTEX_BUFFER_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (uUsageFlags & BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
#ifdef VK_KHR_ray_tracing_pipeline
        if (uUsageFlags & BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        if (uUsageFlags & BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        if (uUsageFlags & BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
        if (uUsageFlags & BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            uVkBufferUsageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        
#endif
        // Add more mappings if your engine defines additional usage bits

        return uVkBufferUsageFlags;
    }

    BufferUsageFlags GetBufferUsageFlagsFromVk(VkBufferUsageFlags vkUsageFlags)
    {
        BufferUsageFlags usageFlags = 0;

        if (vkUsageFlags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
            usageFlags |= BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            usageFlags |= BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
            usageFlags |= BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
            usageFlags |= BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            usageFlags |= BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            usageFlags |= BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            usageFlags |= BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            usageFlags |= BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (vkUsageFlags & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            usageFlags |= BUFFER_USAGE_INDIRECT_BUFFER_BIT;
#ifdef VK_KHR_ray_tracing_pipeline
        if (vkUsageFlags & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
            usageFlags |= BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        if (vkUsageFlags & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
            usageFlags |= BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        if (vkUsageFlags & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
            usageFlags |= BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
        if (vkUsageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            usageFlags |= BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
#endif
        // Add more mappings if your engine defines additional usage bits

        return usageFlags;
    }

    BOOL KVulkanBuffer::Create(const KGfxBufferDesc& bufDesc, const void* pData)
    {
        KVulkanCommandBuffer* pUploadCmdBuffer = nullptr;
        BOOL            bResult          = false;
        BOOL            bRetCode         = false;
        VkResult        vkResult         = VK_ERROR_UNKNOWN;

        VmaMemoryUsage        eMemUsage = VMA_MEMORY_USAGE_UNKNOWN;
        VkMemoryPropertyFlags uVkMemoryPropertyFlags;
        VkBufferUsageFlags    uBufferUsageFlags;
        VkMemoryAllocateInfo  memAlloc       = vks::initializers::MemoryAllocateInfo();
        VkDevice              pvkDevice      = GetVkDevice();
        vks::KVulkanDevice*   pkvkDevice     = GetVulkanDevice();
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        bool bHostCoherent = false;

        m_bufDesc = bufDesc;
        uBufferUsageFlags = GetVkBufferUsageFlags(m_bufDesc.uUsageFlags);

        switch (m_bufDesc.eResAccessFlags)
        {
        case KGfxResourceAccessType::KGfxResourceAccess_GPUOnly:
        {
            eMemUsage = VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_ONLY;

            uVkMemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            if ((m_bufDesc.uUsageFlags & BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) > 0)
            {
                uVkMemoryPropertyFlags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            }

            uBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }
        break;
        case KGfxResourceAccessType::KGfxResourceAccess_Read:
        {
            eMemUsage = VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_TO_CPU;

            // TODO: uVkMemoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT
            uVkMemoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if (DrvOption::bSupportHostCoherentCached)
                uVkMemoryPropertyFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

            uBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bHostCoherent = true; 
        }
        break;
        case KGfxResourceAccessType::KGfxResourceAccess_Write:
        {
            eMemUsage = VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_TO_GPU;
            uVkMemoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            uBufferUsageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bHostCoherent = true;
        }
        break;
        default:
        {
            KGLOG_ASSERT_EXIT(FALSE);
            goto Exit0;
        }
        break;
        }

        if (DrvOption::bSupportDeviceAddress)
        {
            if ((uBufferUsageFlags & BUFFER_USAGE_INDEX_BUFFER_BIT) || (uBufferUsageFlags & BUFFER_USAGE_VERTEX_BUFFER_BIT))
            {
                uBufferUsageFlags = uBufferUsageFlags | gfx::BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                if (DrvOption::bSupportRayTracing)
                {
                    uBufferUsageFlags = uBufferUsageFlags | gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
                }
            }
        }

        m_bufDesc.uUsageFlags = GetBufferUsageFlagsFromVk(uBufferUsageFlags);

        {
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                bRetCode = pkvkDevice->VMACreateBuffer(bufDesc.uByteWidth, uBufferUsageFlags, eMemUsage, m_pvkBuffer, m_pVmaAllocation);
                KGLOG_ASSERT_EXIT(bRetCode);

                m_bCoherent = pkvkDevice->VMAGetAllocationIsCoherent(m_pVmaAllocation);

                m_uDevivceMemSize = pkvkDevice->VMAGetAllocSize(m_pVmaAllocation);
                ASSERT(m_uDevivceMemSize > 0);
            }
            else
            {
                VkMemoryPropertyFlags memAllocPropertyFlags;

                VkMemoryRequirements memReqs;
                VkBufferCreateInfo   bufferCreateInfo = {};
                bufferCreateInfo.sType                = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferCreateInfo.size                 = bufDesc.uByteWidth;
                bufferCreateInfo.usage                = uBufferUsageFlags;

                ASSERT(!m_pvkBuffer);

                vkResult = vks::vkCreateBuffer(pvkDevice, &bufferCreateInfo, KVK_ALLOCATER, &m_pvkBuffer);
                KGLOG_COM_PROCESS_ERROR(vkResult);

                vks::vkGetBufferMemoryRequirements(pvkDevice, m_pvkBuffer, &memReqs);

                memAlloc.allocationSize  = memReqs.size;
                memAlloc.memoryTypeIndex = pkvkDevice->GetMemoryType(memReqs.memoryTypeBits, uVkMemoryPropertyFlags);

                pkvkDevice->GetMemoryTypeProperties(memAlloc.memoryTypeIndex, memAllocPropertyFlags);
                m_bCoherent = (memAllocPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

                m_uDevivceMemSize          = (uint32_t)memReqs.size;
                m_uDevivceMemAlignmentSize = (uint32_t)memReqs.alignment;

                vkResult = pkvkDevice->AllocateMemory(pvkDevice, &memAlloc, KVK_ALLOCATER, &m_pvkDevivceMem, &m_uDevivceMemOffset, (uint32_t)memReqs.alignment);
                KGLOG_COM_PROCESS_ERROR(vkResult);

                vkResult = vks::vkBindBufferMemory(pvkDevice, m_pvkBuffer, m_pvkDevivceMem, m_uDevivceMemOffset);
                KGLOG_COM_PROCESS_ERROR(vkResult);
            }

            m_layoutTracker = KGFX_ResourceLayoutTrackerVK(KGfxAccess::Unknown);

            if (pData)
            {
                CHECK_ASSERT(IsMainThread());

                gfx::IKGFX_RenderContext* pRenderCtx = gfx::GetRenderContext();
                CHECK_ASSERT(pRenderCtx);

                KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)gfx::GetGraphicDevice();
                pRenderCtx->CmdUpdateSubResource(this, 0, bufDesc.uByteWidth, pData);
            }

            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            pPerfMonitor->m_sVkBuffer.UsageBufferCountInc((int)m_bufDesc.uUsageFlags);
        }

        if (m_bCoherent)
        {
            CHECK_ASSERT(bHostCoherent == m_bCoherent);

            if (DrvOption::bX3D_VK_USE_VMA)
            {
                vks::KVulkanDevice* pvkDevice = GetVulkanDevice();

                CHECK_ASSERT(m_pVmaAllocation);

                bRetCode = pvkDevice->VMAMapMemory(m_pVmaAllocation, &m_pMappedMemoryPtr);
                KGLOG_ASSERT_EXIT(bRetCode);
            }
            else
            {
                CHECK_ASSERT(m_pvkDevivceMem);

                VkResult vkRetCode = vks::vkMapMemory(GetVkDevice(), m_pvkDevivceMem, m_uDevivceMemOffset, m_bufDesc.uByteWidth, 0, &m_pMappedMemoryPtr);
                KGLOG_COM_PROCESS_ERROR(vkRetCode);
            }
        }
        GetDescriptorBufferInfo();
        m_uUpDateSize = m_bufDesc.uByteWidth;
        bResult       = true;
    Exit0:
        if (!bResult)
        {
            Destroy();
        }

        return bResult;
    }

    BOOL KVulkanBuffer::Destroy()
    {
        PROF_CPU();

        BOOL bRetCode = FALSE;

        if (m_bCoherent && m_pMappedMemoryPtr)
        {
            m_pMappedMemoryPtr = nullptr;

            if (DrvOption::bX3D_VK_USE_VMA)
            {
                vks::KVulkanDevice* pvkDevice = GetVulkanDevice();
                if (m_pVmaAllocation)
                {
                    pvkDevice->VMAUnmapMemory(m_pVmaAllocation);
                }
            }
            else
            {
                if (m_pvkDevivceMem)
                {
                    vks::vkUnmapMemory(GetVkDevice(), m_pvkDevivceMem);
                }
            }
        }

        if (m_pvkBuffer)
        {
            if (m_uBindlessHandle != UINT32_MAX)
            {
                ASSERT(IS_BINDLESS_ENABLED);
                //m_uBindlessHandle = VUlkanBindlessManager::
                KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
                KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
                pBindlessMgr->ReleaseResourceBindlessSlot(m_uBindlessHandle);
                m_uBindlessHandle = UINT32_MAX;
            }
            VkDevice            pvkDevice  = GetVkDevice();
            vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();

            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            pPerfMonitor->m_sVkBuffer.UsageBufferCountDec((int)m_bufDesc.uUsageFlags);

            // #if X3D_VK_USE_VMA
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                bRetCode = pkvkDevice->VMADestroyBuffer(m_pvkBuffer, m_pVmaAllocation);
                ASSERT(bRetCode);
                ASSERT(!m_pvkBuffer && !m_pVmaAllocation);
            }
            // #else
            else
            {
                vks::vkDestroyBuffer(pvkDevice, m_pvkBuffer, KVK_ALLOCATER);
                m_pvkBuffer = VK_NULL_HANDLE;

                pkvkDevice->FreeMemory(pvkDevice, m_pvkDevivceMem, KVK_ALLOCATER, m_uDevivceMemOffset, m_uDevivceMemSize);
                m_uDevivceMemOffset        = 0;
                m_uDevivceMemAlignmentSize = 0;
                m_pvkDevivceMem            = VK_NULL_HANDLE;
            }
            // #endif

            m_uDevivceMemSize = 0;
        }

        if (m_pStagingBuffer)
        {
            KVulkanGraphicDevice*  pGraphicDevice  = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanStagingManager* pStagingManager = pGraphicDevice->GetStagingManager();

            pStagingManager->FreeBuffer(nullptr, m_pStagingBuffer);
            m_pStagingBuffer = nullptr;
        }

        return true;
    }
    uint32_t KVulkanBuffer::GetBufferBindlessHandle()
    {
        ASSERT(IS_BINDLESS_ENABLED);
        if (m_uBindlessHandle == UINT32_MAX)
        {
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            m_uBindlessHandle = pBindlessMgr->RequestResourceBindlessSolt();
        }
        return m_uBindlessHandle;

    }
    const VkDescriptorBufferInfo& KVulkanBuffer::GetDescriptorBufferInfo(bool bDynamic)
    {
        assert(m_pvkBuffer);
        m_vkDescriptorBufferInfo.buffer = m_pvkBuffer;
        m_vkDescriptorBufferInfo.offset = 0;
        m_vkDescriptorBufferInfo.range  = m_bufDesc.uByteWidth;
        return m_vkDescriptorBufferInfo;
    }

    VkBuffer KVulkanBuffer::GetVkBuffer()
    {
        return m_pvkBuffer;
    }

    uint32_t KVulkanBuffer::GetVkMemorySize()
    {
        return m_uDevivceMemSize;
    }

    void KVulkanBuffer::SetUpdateRenderPassTick(uint64_t uLastUpdateRenderPassTick)
    {
        if (uLastUpdateRenderPassTick != (uint64_t)-1)
        {
            m_LastUpdateRenderPassTick = uLastUpdateRenderPassTick;
        }
    }

    uint64_t KVulkanBuffer::GetUpdateRenderPassTick() const
    {
        return m_LastUpdateRenderPassTick;
    }

    BOOL KVulkanBuffer::FlushMappedRanges()
    {
        vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();
        if (!m_bCoherent)
        {
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                return pkvkDevice->VMAFlushAllocation(m_pVmaAllocation);
            }
            else
            {
                VkDevice            l_hDevice = VkDevice(*pkvkDevice);
                VkMappedMemoryRange memRange{};
                memRange.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                memRange.size   = GetVkMemorySize();
                memRange.offset = GetVkMemoryOffset();
                memRange.memory = GetVkMemory()->m_pMem;

                return vks::vkFlushMappedMemoryRanges(l_hDevice, 1, &memRange);
            }
        }
        return TRUE;
    }

    BOOL KVulkanBuffer::InvalidateMappedRanges()
    {
        vks::KVulkanDevice* pkvkDevice = GetVulkanDevice();
        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            return pkvkDevice->VMAInvalidateAllocation(m_pVmaAllocation, 0, VK_WHOLE_SIZE);
        }
        // #else
        else
        {
            VkDevice            l_hDevice = VkDevice(*pkvkDevice);
            VkMappedMemoryRange l_Range{};
            l_Range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            l_Range.size   = GetVkMemorySize();
            l_Range.offset = GetVkMemoryOffset();
            l_Range.memory = GetVkMemory()->m_pMem;
            return vks::vkInvalidateMappedMemoryRanges(l_hDevice, 1, &l_Range);
        }
        // #endif
    }

    void KVulkanBuffer::MapTempStagingBuffer(KVulkanStagingBuffer* pStagingBuffer)
    {
        ASSERT(m_pvkBuffer);

        if (pStagingBuffer)
        {
            ASSERT(!m_pStagingBuffer);
            m_pStagingBuffer = pStagingBuffer;
        }
        else
        {
            m_pStagingBuffer = nullptr;
        }
    }

    uint64_t KVulkanBuffer::GetBufferDeviceAddress()
    {
        uint64_t                  ret  = 0;
        VkBufferDeviceAddressInfo info = {};
        info.sType                     = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.pNext                     = nullptr;
        info.buffer                    = m_pvkBuffer;
        ret                            = vks::vkGetBufferDeviceAddress(GetVkDevice(), &info);
        return ret;
    }
    

    void KVulkanBuffer::SetDebugName(const char* pcszName)
    {
        if(pcszName)
        {
            if(IsDynamic())
            {
                if(m_strVKObjectName.empty())
                {
                    GetVulkanDevice()->SetObjectLabel(m_pvkBuffer, pcszName);
                }
            }
            else
            {
                GetVulkanDevice()->SetObjectLabel(m_pvkBuffer, pcszName);
            }
        }        
        m_strVKObjectName = pcszName ? pcszName : "";
    }

    const char* KVulkanBuffer::GetDebugName()
    {
        return m_strVKObjectName.c_str();        
    }

    BOOL KVulkanBuffer::Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bOverWrite)
    {
        PROF_CPU();
        BOOL bResult = FALSE;

        KGLOG_ASSERT_EXIT(uDstOffset < m_bufDesc.uByteWidth);
        if (uSrcDataSize == 0)
        {
            uSrcDataSize = m_bufDesc.uByteWidth - uDstOffset;
        }
        KGLOG_ASSERT_EXIT(uSrcDataSize + uDstOffset <= m_bufDesc.uByteWidth);

        if (m_nFrameCount >= NSEngine::GetRenderFrameMoveLoopCount() && !IsDynamic())
        {
            //DEBUG_BREAK();
        }
        m_nFrameCount = NSEngine::GetRenderFrameMoveLoopCount();

        {
            CHECK_ASSERT(IsMainThread());

            gfx::IKGFX_RenderContext* pRenderCtx = gfx::GetRenderContext();
            CHECK_ASSERT(pRenderCtx);

            pRenderCtx->CmdUpdateSubResource(this, uDstOffset, uSrcDataSize, pSrcData);
        }

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    KGFX_ResourceLayoutTrackerVK& KVulkanBuffer::GetLayoutTracker()
    {
        return m_layoutTracker;
    }

    void KVulkanBuffer::EnableUAVOverlap(bool bEnable)
    {
        m_UAVOverlap = bEnable;
    }

    void* KVulkanBuffer::MapRange()
    {
        void* pRetMappedPtr = nullptr;

        KGLOG_ASSERT_EXIT(m_bCoherent);

        pRetMappedPtr = m_pMappedMemoryPtr;
        KGLOG_ASSERT_EXIT(pRetMappedPtr);

    Exit0:
        return pRetMappedPtr;
    }

    uint32_t KVulkanBuffer::GetId()
    {
        return m_uId;
    }

    uint64_t KVulkanBuffer::GetCode()
    {
        return m_uId + (uint64_t)GetNativeResourceHandle();
    }

    /////////////////////////////////////////////////////////////////////////////////////
    KVulkanBufferResourceView::KVulkanBufferResourceView()
    {
        m_nRef = 1;
        ZeroMemory(&m_viewDesc, sizeof(m_viewDesc));
        ZeroMemory(&m_createInfo, sizeof(m_createInfo));

#if GfxGfxBufferResourceViewMemLeakDetect
        static int gfxMem_Count = 0;
        gfxMem_Count++;
        if (gfxMem_Count == 11)
        {
            // 看泄多少字节，这里下断点就是现场了
            int x = 0;
        }
        m_memLeakDetect = new char[gfxMem_Count];
#endif
    }

    KVulkanBufferResourceView::~KVulkanBufferResourceView()
    {
        Destroy();
#if GfxGfxBufferResourceViewMemLeakDetect
        SAFE_DELETE_ARRAY(m_memLeakDetect);
#endif
    }

    void KVulkanBufferResourceView::SetObjectName(const char* szName)
    {
        GetVulkanDevice()->SetObjectLabel(m_pvkView, szName);
    }

    int32_t KVulkanBufferResourceView::AddRef()
    {
        ASSERT(m_nRef > 0);
        int32_t nRef = ++m_nRef;
        return nRef;
    }

    int32_t KVulkanBufferResourceView::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            if (m_pResource)
            {
                auto piDevice = KGFX_GetGraphicDeviceVKInternal();
                CHECK_ASSERT(piDevice);

                piDevice->GC_DelayReleaseObject(this, [this]() { SAFE_RELEASE(m_pResource); });
            }
            else
            {
                // 如果没有初始化成功，直接释放
                delete this;
            }
        }

        return nRef;
    }

    const KGfxTextureFormatInfo& GetTextureFormatInfoVk(enumTextureFormat eFormat);

    BOOL KVulkanBufferResourceView::Create(KVulkanBuffer* pResource, const KGFX_BufferViewDesc* pDesc)
    {
        BOOL                         bResult           = false;
        VkResult                     vkResult          = VK_ERROR_UNKNOWN;
        VkDevice                     pvkDevice         = GetVkDevice();
        KGraphicDevice*              pGfxGraphicDevice = gfx::GetGraphicDevice();
        VkBuffer                     pvkBuffer         = VK_NULL_HANDLE;
        VkBufferViewCreateInfo       createInfo        = vks::initializers::BufferViewCreateInfo();
        const KGfxTextureFormatInfo* textureFormatInfo = nullptr;
        const KGfxBufferDesc*        bufDesc           = nullptr;
        uint32_t                     uBytesRange       = 0;

        KGLOG_ASSERT_EXIT(pResource);
        KGLOG_ASSERT_EXIT(pDesc);

        bufDesc = pResource->GetDesc();
        KGLOG_ASSERT_EXIT(bufDesc->uByteWidth > pDesc->uBytesOffset);

        uBytesRange = pDesc->uBytesRange == 0 ? bufDesc->uByteWidth - pDesc->uBytesOffset : pDesc->uBytesRange;
        KGLOG_ASSERT_EXIT(bufDesc->uByteWidth >= pDesc->uBytesOffset + uBytesRange);

        pvkBuffer = pResource->GetVkBuffer();
        KGLOG_ASSERT_EXIT(pvkBuffer);

        if (pDesc->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV)
        {
            CHECK_ASSERT(bufDesc->eResAccessFlags == KGfxResourceAccessType::KGfxResourceAccess_GPUOnly);
        }

        if (pDesc->eFormat != TEX_FORMAT_NONE)
        {
            // Image Buffer
            textureFormatInfo = &GetTextureFormatInfoVk(pDesc->eFormat);

            createInfo.buffer = pvkBuffer;
            createInfo.format = textureFormatInfo->vkFormat;
            createInfo.offset = pDesc->uBytesOffset;
            createInfo.range  = uBytesRange;

            /*
            offset and range must comply with VkPhysicalDeviceLimits::minTexelBufferOffsetAlignment
            */

            {
                BOOL              bColorAttach   = FALSE;
                BOOL              bDepth         = FALSE;
                BOOL              bStencil       = FALSE;
                uint32_t          uBytesStride   = 4;
                enumTextureFormat eTextureFormat = gfx::GetTextureEnumFromFormat(textureFormatInfo->vkFormat);
                gfx::GetTextureFormatFromTargetFormat(eTextureFormat, bColorAttach, bDepth, bStencil, uBytesStride);
                KGFX_PHYSICAL_DEVICE_LIMITS* pLimits = pGfxGraphicDevice->GetPhysicalDeviceLimits();
                if (pLimits && (createInfo.range / std::max<uint32_t>(uBytesStride, 1)) > pLimits->maxTexelBufferElements)
                {
                    KGLOG_PROCESS_ERROR(FALSE);
                }
            }

            vkResult = vks::vkCreateBufferView(pvkDevice, &createInfo, KVK_ALLOCATER, &m_pvkView);
            KGLOG_COM_ASSERT_EXIT(vkResult);

            m_createInfo = createInfo;
        }
        else
        {
            // Structured Buffer or ByteAddressBuffer(StructuredBuffer<uint>)
            m_createInfo = {};
        }

        m_pResource = pResource;
        m_pResource->AddRef();

        m_viewDesc             = *pDesc;
        m_viewDesc.uBytesRange = uBytesRange;

        bResult = true;
    Exit0:
        return bResult;
    }

    void KVulkanBufferResourceView::Destroy()
    {
        if (m_pvkView)
        {
            VkDevice pvkDevice = GetVkDevice();
            CHECK_ASSERT(pvkDevice);

            vks::vkDestroyBufferView(pvkDevice, m_pvkView, KVK_ALLOCATER);
            m_pvkView = VK_NULL_HANDLE;
        }

        SAFE_RELEASE(m_pResource);
    }

    VkBufferView KVulkanBufferResourceView::GetVkHandle()
    {
        return m_pvkView;
    }

    KVulkanBuffer* KVulkanBufferResourceView::GetGfxResource()
    {
        return m_pResource;
    }

    uintptr_t KVulkanBufferResourceView::GetNativeHandle()
    {
        return (uintptr_t)m_pvkView;
    }

    uint32_t KVulkanBufferResourceView::GetBindlessHandle()
    {
        return m_pResource->GetBufferBindlessHandle();
    }

    IKGFX_Buffer* KVulkanBufferResourceView::GetResource()
    {
        return m_pResource;
    }

    const KGFX_BufferViewDesc* KVulkanBufferResourceView::GetViewDesc() const
    {
        return &m_viewDesc;
    }

    void* KVulkanBufferResourceView::GetViewHandle()
    {
        return (m_pvkView);
    }

    uint64_t KVulkanBufferResourceView::GetCode()
    {
        return (uint64_t)m_pvkView;
    }

    /////////////////////////////////////////////////////////////////////////////////////
    KVulkanDynamicBuffer::KVulkanDynamicBuffer(uint32_t uSize, gfx::BufferUsageFlags uUsageFlags, BOOL bShareMode)
    {        
        m_bufDesc.uStructureByteStride = 0;
        m_bufDesc.uByteWidth           = uSize;
        m_bufDesc.uUsageFlags          = uUsageFlags; // gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        m_bufDesc.eResAccessFlags      = gfx::KGfxResourceAccessType::KGfxResourceAccess_Write;
        
        m_vkDescriptorBufferInfo.offset = 0xFFFFFFFF;
        m_vkDescriptorBufferInfo.range  = uSize;
        m_nFrameCount                   = -1;
        m_uUpDateSize                   = m_bufDesc.uByteWidth;
        m_bShareMode = bShareMode;
        m_pPrivateDyNamicBufferRing = nullptr;

        m_vkStaticDescriptorBufferInfo = m_vkDescriptorBufferInfo;

        if(bShareMode)
        {
            gfx::KVulkanGraphicDevice *pGrpahicDevice = (gfx::KVulkanGraphicDevice*) gfx::GetGraphicDevice();
            KDynamicBufferRing* pGlobalRingBUffer = pGrpahicDevice->GetDynamicBufferPool();            
            m_vkDescriptorBufferInfo.buffer = pGlobalRingBUffer->GetVKBuffer();
            m_pvkBuffer = m_vkDescriptorBufferInfo.buffer;
        }
        else
        {
            //ASSERT(IsMainThread());
            //vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
            //m_pPrivateDyNamicBufferRing = new KDynamicBufferRing();
            //uint32_t numberOfBackBuffers = DrvOption::nSwapChainCount;
            //if(DrvOption::bDynamicBufferFrameSliceLimit)
            //{
            //    numberOfBackBuffers = 3;
            //}
            //uint32_t totalMem = (numberOfBackBuffers + 1) * m_bufDesc.uByteWidth;            
            //VkResult result = m_pPrivateDyNamicBufferRing->Create(pVulkanDevice, numberOfBackBuffers, totalMem, nullptr);
            //m_pvkBuffer = m_pPrivateDyNamicBufferRing->GetVKBuffer();
            //ASSERT(result ==  VK_SUCCESS);
            //m_vkDescriptorBufferInfo.buffer = m_pPrivateDyNamicBufferRing->GetVKBuffer();

            ReCreatePrivateDyNamicBufferRing();
        }
    }


    uint32_t KVulkanDynamicBuffer::GetBufferBindlessHandle()
    {
        ASSERT(m_uBindlessHandle != UINT32_MAX && "You must get bindless handle after updating the dynamic buffer !");
        return m_uBindlessHandle;
    }

    BOOL KVulkanDynamicBuffer::ReCreatePrivateDyNamicBufferRing()
    {
        BOOL bRet = false;

        ASSERT(IsMainThread());
        if(m_pPrivateDyNamicBufferRing)
        {
            //m_pPrivateDyNamicBufferRing->Destroy();
            //SAFE_DELETE(m_pPrivateDyNamicBufferRing);
            auto piDevice = gfx::KGFX_GetGraphicDeviceVKInternal();
            CHECK_ASSERT(piDevice);
            piDevice->GC_DelayReleaseObject(m_pPrivateDyNamicBufferRing, [&]() {m_pPrivateDyNamicBufferRing->Destroy(); });
        }

        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        m_pPrivateDyNamicBufferRing = new KDynamicBufferRing();
        uint32_t numberOfBackBuffers = DrvOption::nSwapChainCount;
        if (DrvOption::bDynamicBufferFrameSliceLimit)
        {
            numberOfBackBuffers = 3;
        }
        uint32_t totalMem = (numberOfBackBuffers + 1) * m_bufDesc.uByteWidth * m_uMultiple;
        VkResult result = m_pPrivateDyNamicBufferRing->Create(pVulkanDevice, numberOfBackBuffers, totalMem, nullptr);
        m_pvkBuffer = m_pPrivateDyNamicBufferRing->GetVKBuffer();     
        m_vkDescriptorBufferInfo.buffer = m_pPrivateDyNamicBufferRing->GetVKBuffer();
        KGLOG_ASSERT_EXIT(result == VK_SUCCESS);        
        bRet = true;
    Exit0:
        return   bRet;
    }

    KVulkanDynamicBuffer::~KVulkanDynamicBuffer()
    {        
        ASSERT(m_nRef == 0);
        m_pvkBuffer = nullptr;
        if(m_pPrivateDyNamicBufferRing)
        {                        
            //m_pPrivateDyNamicBufferRing->Destroy();
            //SAFE_DELETE(m_pPrivateDyNamicBufferRing);

            auto piDevice = gfx::KGFX_GetGraphicDeviceVKInternal();
            CHECK_ASSERT(piDevice);
            piDevice->GC_DelayReleaseObject(m_pPrivateDyNamicBufferRing, [&]() {m_pPrivateDyNamicBufferRing->Destroy(); });
        }
    }

    int32_t KVulkanDynamicBuffer::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            
            if(m_bShareMode)
            {
                //共享模式引用共用buffer资源，并不需要释放Device资源，不需要资源回收
                delete this;                
            }
            else
            {
                if (m_pvkBuffer != VK_NULL_HANDLE)
                {
                    auto piDevice = KGFX_GetGraphicDeviceVKInternal();
                    CHECK_ASSERT(piDevice);

                    piDevice->GC_DelayReleaseObject(this);
                }
                else
                {
                    // 如果没有创建过，直接释放
                    delete this;
                }
            }
        }

        return nRef;
    }

    static inline uint32_t AlignUp(uint32_t val, uint32_t alignment)
    {
        return (val + alignment - 1) & ~(alignment - 1);
    }

    const VkDescriptorBufferInfo& KVulkanDynamicBuffer::GetDescriptorBufferInfo(bool bDynamic)
    {        
        ASSERT(m_vkDescriptorBufferInfo.offset != 0xFFFFFFFF && "还没更新就绑定，不允许的，这很危险，尽管vk可以这么做，但对于dx而言这样会出问题");
        if(bDynamic)
        {
            return m_vkDescriptorBufferInfo;
        }
        else
        {
            ///按动态buffer默认绑定的时候offset和range都是不修改的，绘制的时候才决定offset，这样descriptorset才不会发生绑定变化
            //所以动态绑定bDynamic应该是false,走这个流程才是对的
            m_vkStaticDescriptorBufferInfo.buffer = m_pvkBuffer;
            m_vkStaticDescriptorBufferInfo.offset = 0;                    // 绑定descriptor不需要
            m_vkStaticDescriptorBufferInfo.range = m_bufDesc.uByteWidth; // 真正大小
            return m_vkStaticDescriptorBufferInfo;
        }
    }

    uint32_t KVulkanDynamicBuffer::GetDynamicOffset()
    {
        uint32_t uOffset    = (uint32_t)m_vkDescriptorBufferInfo.offset;
        int32_t  uLoopCount = NSEngine::GetRenderFrameMoveLoopCount();
        ASSERT(m_vkDescriptorBufferInfo.range > 0 && m_nFrameCount == uLoopCount);
        return uOffset;
    }

    BOOL KVulkanDynamicBuffer::Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bOverWrite)
    {
        BOOL bRet = FALSE;

        KGLOG_ASSERT_EXIT(uDstOffset < m_bufDesc.uByteWidth);

        if (uSrcDataSize == 0)
        {
            uSrcDataSize = m_bufDesc.uByteWidth - uDstOffset;
        }
        KGLOG_ASSERT_EXIT(uSrcDataSize + uDstOffset <= m_bufDesc.uByteWidth);

        if(m_bShareMode)
        {
            //共享模式，帧移动回收在全局统一移动
            KDynamicBufferRing* pBufferRing = GetGraphicDevice()->GetDynamicBufferPool();

            if (bOverWrite)
            {
                pBufferRing->ReUpdate(uSrcDataSize, GetDynamicOffset() + uDstOffset, pSrcData);
                bRet = TRUE;
            }
            else
            {
                void* pBuffer = nullptr;
                uint32_t size = m_bufDesc.uByteWidth;

                if (pBufferRing->AllocConstantBuffer(size, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
                {
                    if(pBuffer)
                    {
                        memcpy((uint8_t*)pBuffer + uDstOffset, pSrcData, uSrcDataSize);
                    }
                    bRet = TRUE;
                }
                else
                {
                    //自动扩容
                    gfx::KVulkanGraphicDevice* pGrpahicDevice = (gfx::KVulkanGraphicDevice*)gfx::GetGraphicDevice();
                    BOOL bRetCode = pGrpahicDevice->ReCreateDynamicBufferPool();
                    if(bRetCode)
                    {
                        pBufferRing = pGrpahicDevice->GetDynamicBufferPool();
                        m_vkDescriptorBufferInfo.buffer = pBufferRing->GetVKBuffer();
                        m_pvkBuffer = m_vkDescriptorBufferInfo.buffer;                        
                        if (pBufferRing->AllocConstantBuffer(size, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
                        {
                            if (pBuffer)
                            {
                                memcpy((uint8_t*)pBuffer + uDstOffset, pSrcData, uSrcDataSize);
                            }
                            bRet = TRUE;
                        }
                    }
                }
            }
        }
        else if(m_pPrivateDyNamicBufferRing)
        {            
            //if(m_nFrameCount  == NSEngine::GetRenderFrameMoveLoopCount())
            //{
            //    bRet = TRUE;
            //}
            //else
            {
                //非共享模式，控制一帧更新一次，每次更新自己移动帧回收
                if(m_nFrameCount != NSEngine::GetRenderFrameMoveLoopCount())
                {
                    m_pPrivateDyNamicBufferRing->BeginFrame();
                }
                void* pBuffer = nullptr;
                uint32_t size = m_bufDesc.uByteWidth;            
                if (m_pPrivateDyNamicBufferRing->AllocConstantBuffer(size, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
                {
                    if (pBuffer)
                    {
                        memcpy((uint8_t*)pBuffer + uDstOffset, pSrcData, uSrcDataSize);
                    }
                    bRet = TRUE;
                }
                else
                {
                    ASSERT(m_uMultiple < 20); //如果很大倍数了，应该是出了啥问题吧？
                    m_uMultiple += 1;
                    BOOL retCode = ReCreatePrivateDyNamicBufferRing();
                    if (retCode && m_pPrivateDyNamicBufferRing->AllocConstantBuffer(size, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
                    {
                        if (pBuffer)
                        {
                            memcpy((uint8_t*)pBuffer + uDstOffset, pSrcData, uSrcDataSize);
                        }
                        bRet = TRUE;
                    }
                    else
                    {
                        ASSERT(0);
                    }
                }
            }
        }
        if (IS_BINDLESS_ENABLED)
        {
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            if (m_uBindlessHandle != UINT32_MAX)
            {
                pBindlessMgr->DelayReleaseResourceBindlessSlot(m_uBindlessHandle);
            }
            m_uBindlessHandle = pBindlessMgr->RequestResourceBindlessSolt();
            KGLOG_ASSERT_EXIT(m_uBindlessHandle != UINT32_MAX);
        }
        m_nFrameCount = NSEngine::GetRenderFrameMoveLoopCount();
    Exit0:
        return bRet;
    }

    void* KVulkanDynamicBuffer::MapRange()
    {
        void* pBuffer = nullptr;

        if(m_bShareMode)
        {
            KDynamicBufferRing* pBufferRing = GetGraphicDevice()->GetDynamicBufferPool();
            
            if (pBufferRing->AllocConstantBuffer(m_bufDesc.uByteWidth, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
            {
                pBuffer = pBuffer;
            }
            else
            {
                //自动扩容
                gfx::KVulkanGraphicDevice* pGrpahicDevice = (gfx::KVulkanGraphicDevice*)gfx::GetGraphicDevice();
                BOOL bRetCode = pGrpahicDevice->ReCreateDynamicBufferPool();
                if (bRetCode)
                {                    
                    pBufferRing = pGrpahicDevice->GetDynamicBufferPool();
                    m_vkDescriptorBufferInfo.buffer = pBufferRing->GetVKBuffer();
                    m_pvkBuffer = m_vkDescriptorBufferInfo.buffer;
                    if (pBufferRing->AllocConstantBuffer(m_bufDesc.uByteWidth, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
                    {
                        pBuffer = pBuffer;
                    }
                }
            }
        }
        else
        {
            void* pBuffer = nullptr;
            if (m_nFrameCount != NSEngine::GetRenderFrameMoveLoopCount())
            {
                m_pPrivateDyNamicBufferRing->BeginFrame();
            }
            uint32_t size = m_bufDesc.uByteWidth;
            if (m_pPrivateDyNamicBufferRing->AllocConstantBuffer(size, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
            {
                pBuffer = pBuffer;
            }
            else
            {
                ASSERT(m_uMultiple < 20); //如果很大倍数了，应该是出了啥问题吧？
                m_uMultiple += 1;
                BOOL retCode = ReCreatePrivateDyNamicBufferRing();
                if (retCode && m_pPrivateDyNamicBufferRing->AllocConstantBuffer(size, &pBuffer, m_vkDescriptorBufferInfo, m_bShareMode))
                {
                    pBuffer = pBuffer;
                }
                else
                {
                    ASSERT(0);
                }
            }
        }
        m_nFrameCount = NSEngine::GetRenderFrameMoveLoopCount();
        return pBuffer;
    }
} // namespace gfx

#include "KVulkanDynamicRingBuffer.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "optick.h"
#include "KVulkanDevice.h"
#include "KVulkanFuncStub.h"
#include "Engine/KGLog.h"
// #include "../GFXVulkan.h"
// #include "KVulkanStagingManager.h"

#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    static inline uint32_t AlignUp(uint32_t val, uint32_t alignment)
    {
        return (val + alignment - 1) & ~(alignment - 1);
    }

    // In practice, all PC GPU vendors (AMD, Intel, NVIDIA) support HOST_COHERENT on every memory type that is HOST_VISIBLE.
// No need to worry about it on current Windows PCs.
#if _WIN32
    static bool g_bDynamicRingBufferUseVMA = DrvOption::bX3D_VK_USE_VMA;
#else
    static bool g_bDynamicRingBufferUseVMA = false;
#endif


    KDynamicBufferRing::KDynamicBufferRing()
        : m_pDevice(nullptr)
        , m_pData(nullptr)
        , m_uMemTotalSize(0)
        , m_vkBuffer(VK_NULL_HANDLE)
        , m_deviceMemory(VK_NULL_HANDLE)
        , m_nAlignment(256)
        , m_nAllocSize(0)
        , m_nAllocCount(0)
    {
    }

    KDynamicBufferRing::~KDynamicBufferRing()
    {
        if (g_bDynamicRingBufferUseVMA)
        {
            m_pDevice->VMAUnmapMemory(m_pVmaAllocation);
            m_pDevice->VMADestroyBuffer(m_vkBuffer, m_pVmaAllocation);
        }
        else
        {
            vkfunc::vkUnmapMemory(GetVkDevice(), m_deviceMemory);
            vkfunc::vkFreeMemory(GetVkDevice(), m_deviceMemory, nullptr);
            vkfunc::vkDestroyBuffer(GetVkDevice(), m_vkBuffer, nullptr);
        }
        m_mem.OnDestroy();
    }

    VkResult KDynamicBufferRing::Create(vks::KVulkanDevice* pDevice, uint32_t numberOfBackBuffers, uint32_t memTotalSize, char* name)
    {
        VkResult res;
        m_pDevice       = pDevice;
        m_uMemTotalSize = AlignUp(memTotalSize, 256);
        m_nAlignment    = std::max(pDevice->GetMinUniformBufferOffsetAlignment(), pDevice->GetStorageBufferOffsetAlignment());

        m_mem.OnCreate(numberOfBackBuffers, m_uMemTotalSize);
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (g_bDynamicRingBufferUseVMA)
        {
            /*	VmaAllocationCreateInfo allocInfo = {};
                allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
                allocInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
                allocInfo.pUserData = name;*/

            BOOL bRet = pDevice->VMACreateBuffer(m_uMemTotalSize, usage, VMA_MEMORY_USAGE_CPU_TO_GPU, m_vkBuffer, m_pVmaAllocation);
            res       = bRet ? VK_SUCCESS : VK_INCOMPLETE;

            ASSERT(res == VK_SUCCESS);
            pDevice->SetObjectLabel(m_vkBuffer, "DynamicBufferRing");

            bRet = pDevice->VMAMapMemory(m_pVmaAllocation, (void**)&m_pData);

            res = bRet ? VK_SUCCESS : VK_INCOMPLETE;
            ASSERT(res == VK_SUCCESS);
        }
        else
        {
            // create a buffer that can host uniforms, indices and vertexbuffers
            VkBufferCreateInfo buf_info    = {};
            buf_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buf_info.pNext                 = NULL;
            buf_info.usage                 = usage; //| VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            buf_info.size                  = m_uMemTotalSize;
            buf_info.queueFamilyIndexCount = 0;
            buf_info.pQueueFamilyIndices   = NULL;
            buf_info.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
            buf_info.flags                 = 0;
            res                            = vkfunc::vkCreateBuffer(GetVkDevice(), &buf_info, NULL, &m_vkBuffer);
            ASSERT(res == VK_SUCCESS);

            VkMemoryRequirements mem_reqs;
            vkfunc::vkGetBufferMemoryRequirements(GetVkDevice(), m_vkBuffer, &mem_reqs);

            VkMemoryPriorityAllocateInfoEXT next = {};
            next.pNext                           = NULL;
            next.priority                        = 0.5;
            next.sType                           = VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT;

            VkMemoryAllocateInfo alloc_info = {};
            VkBool32             bFind      = FALSE;
            alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.pNext                = nullptr;
            alloc_info.allocationSize       = mem_reqs.size;
            alloc_info.memoryTypeIndex      = m_pDevice->GetMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &bFind);
            ASSERT(bFind && "No mappable, coherent memory");
            KGLOG_PROCESS_ERROR(bFind);

            res = vkfunc::vkAllocateMemory(GetVkDevice(), &alloc_info, nullptr, &m_deviceMemory);
            ASSERT(res == VK_SUCCESS);

            res = vkfunc::vkMapMemory(GetVkDevice(), m_deviceMemory, 0, mem_reqs.size, 0, (void**)&m_pData);
            ASSERT(res == VK_SUCCESS);

            res = vkfunc::vkBindBufferMemory(GetVkDevice(), m_vkBuffer, m_deviceMemory, 0);
            ASSERT(res == VK_SUCCESS);
            m_pDevice->SetObjectLabel(m_vkBuffer, "DynamicBufferRing");
        }
    Exit0:
        return res;
    }

    void KDynamicBufferRing::Destroy()
    {

    }

    bool KDynamicBufferRing::AllocConstantBuffer(uint32_t size, void** pData, VkDescriptorBufferInfo& info, BOOL bShared)
    {
        size = AlignUp(size, m_nAlignment);
        uint32_t memOffset;

        if (m_mem.Alloc(size, &memOffset) == false)
        {
            //if(bShared)
            //{   //非shared情况，是正常的，外部需要做自动扩容处理，shared就是异常了，全局的不能自动扩容
            //    KGLogPrintf(KGLOG_ERR, "Ran out of mem for 'dynamic' buffers, please increase the allocated size");
            //    RaiseDumpException();
            //}
            return false;
        }

        *pData = (void*)(m_pData + memOffset);

        info.buffer   = m_vkBuffer;
        info.offset   = memOffset;
        info.range    = size;
        m_nAllocSize += size;
        ++m_nAllocCount;
        return true;
    }

    void KDynamicBufferRing::ReUpdate(uint32_t uSize, uint32_t uOffset, const void* pData)
    {
        memcpy(m_pData + uOffset, pData, uSize);
    }

    //VkDescriptorBufferInfo KDynamicBufferRing::AllocConstantBuffer(uint32_t size, const void* pData)
    //{
    //    void*                  pBuffer;
    //    VkDescriptorBufferInfo out;

    //    if (AllocConstantBuffer(size, &pBuffer, out))
    //    {
    //        memcpy(pBuffer, pData, size);
    //    }

    //    return out;
    //}

    /*BOOL KDynamicBufferRing::AllocConstantBuffer(uint32_t size, VkDescriptorBufferInfo& info)
    {
        size = AlignUp(size, m_nAlignment);

        uint32_t memOffset;
        if (m_mem.Alloc(size, &memOffset) == false)
        {
            ASSERT(!"Ran out of mem for 'dynamic' buffers, please increase the allocated size");
            return false;
        }

        info.buffer = m_vkBuffer;
        info.offset = memOffset;
        info.range = size;
        m_nAllocSize += size;

        return true;
    }

    VkDescriptorBufferInfo KDynamicBufferRing::AllocConstantBuffer1(uint32_t size, const void* pData)
    {
        //void* pBuffer;
        VkDescriptorBufferInfo out;

        if (AllocConstantBuffer(size, out))
        {
            //memcpy(pBuffer, pData, size);

            //VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, m_deviceMemory, out.offset, out.range };
            //vkfunc::vkFlushMappedMemoryRanges(GetVkDevice(), 1, &range);

            KVulkanGraphicDevice* pDevice = (KVulkanGraphicDevice*)gfx::GetGraphicDevice();
            KVulkanCommandBuffer* pkvkCmdBuffer = pDevice->GetUploadCmdBufferInternal();
            gfx::KVulkanStagingManager* pStagingMgr = pDevice->GetStagingManager();
            KVulkanStagingBuffer* pStagingBuffer = pStagingMgr->AllocBuffer(out.range, nullptr, KMapOperationMode::KMapOperationMode_Write_Discard);

            BOOL bMapped = pStagingBuffer->Map(0, out.range);
            memcpy((uint8_t*)pStagingBuffer->GetMappedMemoryPtr(), pData, size);
            pStagingBuffer->Unmap();

            if (bMapped)
            {
                VkBufferCopy copyRegion = {};
                copyRegion.dstOffset = out.offset;
                copyRegion.size = out.range;
                vks::vkCmdCopyBuffer(pkvkCmdBuffer->GetCommandBuffer(), pStagingBuffer->GetVkBufferHandle(), m_vkBuffer, 1, &copyRegion);
            }

            pStagingMgr->FreeBuffer(pkvkCmdBuffer, pStagingBuffer);
        }

        return out;
    }*/

    //VkDescriptorBufferInfo KDynamicBufferRing::AllocConstantBuffer(VkCommandBuffer pCmdBuffer, uint32_t size, const void* pData)
    //{
    //    void*                  pBuffer;
    //    VkDescriptorBufferInfo out;

    //    if (AllocConstantBuffer(size, &pBuffer, out))
    //    {
    //        memcpy(pBuffer, pData, size);
    //    }

    //    return out;
    //}

    void KDynamicBufferRing::BeginFrame()
    {
        m_nAllocSize  = 0;
        m_nAllocCount = 0;
        m_mem.BeginFrame();
    }

    void KDynamicBufferRing::SetDescriptorSet(int index, uint32_t size, VkDescriptorSet descriptorSet)
    {
        VkDescriptorBufferInfo out = {};
        out.buffer                 = m_vkBuffer;
        out.offset                 = 0;
        out.range                  = size;

        VkWriteDescriptorSet write;
        write                 = {};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext           = nullptr;
        write.dstSet          = descriptorSet;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo     = &out;
        write.dstArrayElement = 0;
        write.dstBinding      = index;

        vks::vkUpdateDescriptorSets(GetVkDevice(), 1, &write, 0, nullptr);
    }
} // namespace gfx

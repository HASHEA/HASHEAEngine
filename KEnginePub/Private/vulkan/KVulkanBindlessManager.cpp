#include "KVulkanBindlessManager.h"
#include "KVulkanRayTracing.h"
#include "KVulkanDevice.h"
#include "KVulkanTexture.h"
#include "kVulkanBuffer.h"
#include "KVulkanInitializers.h"

namespace gfx
{
    namespace BindlessHelper
    {
        static VkDescriptorType GetDescriptorType(enumDescriptorType desc)
        {
            VkDescriptorType ret = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            switch (desc)
            {
            case gfx::DESCRIPTOR_TYPE_SAMPLER:
                ret = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            case gfx::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                ret = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                break;
            case gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                ret = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE:
                ret = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case gfx::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                ret = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                break;
            case gfx::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                ret = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                break;
            case gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                ret = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER:
                ret = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                ret = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                break;
            case gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                ret = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                break;
            case gfx::DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                ret = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                break;
            case gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
                ret = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                break;
            default:
                break;
            }
            return ret;
        }
    }
    constexpr uint32_t k_max_bindless_resource_count = 4096;
    constexpr uint32_t k_max_bindless_sampler_count = 8;
    constexpr uint32_t k_delay_release_frame_count = 4;
    KVulkanBindlessSlot KVulkanBindlessAllocator::RequestBindlessSlot()
    {
        KVulkanBindlessSlot retSlot = UINT32_MAX;
        if (m_vecFreeSlot.size() == 0)
        {
            retSlot = ++m_uCurrentAllocatedSlotBound;
        }
        else
        {
            retSlot = m_vecFreeSlot.back();
            m_vecFreeSlot.pop_back();
        }
        return retSlot;
    }

    void KVulkanBindlessAllocator::DelayReleaseResourceBindlessSlot(KVulkanBindlessSlot _slot)
    {
        m_stackDelayRelease[m_uCurrentDeletionQueueIndex].push_back(_slot);
    }

    void KVulkanBindlessAllocator::Tick()
    {
        auto& l_curQue = m_stackDelayRelease[m_uCurrentDeletionQueueIndex];
        for (auto& i : l_curQue)
        {
            m_vecFreeSlot.push_back(i);
        }
        l_curQue.clear();
        m_uCurrentDeletionQueueIndex++;
        m_uCurrentDeletionQueueIndex %= k_delay_release_frame_count;
    }

    void KVulkanBindlessAllocator::ReleaseBindlessSlot(KVulkanBindlessSlot _slot)
    {
        m_vecFreeSlot.push_back(_slot);
    }

    bool KVulkanBindlessManager::Init_InRenderThread()
    {
        bool bRetCode = false;
        bool bResult = false;
        VkDevice pDevice = VK_NULL_HANDLE;
        KGLOG_PROCESS_ERROR(IS_BINDLESS_ENABLED);
        pDevice = GetVkDevice();
        KGLOG_PROCESS_ERROR(pDevice);
        {
            KBindlessDescriptorPoolCreation poolCreation{};
            poolCreation.AddPoolItem(gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, k_max_bindless_resource_count)
                .AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE, k_max_bindless_resource_count)
                .AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE, k_max_bindless_resource_count)
                .AddPoolItem(gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER, k_max_bindless_resource_count)
                .AddPoolItem(gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_max_bindless_resource_count)
                .AddPoolItem(gfx::DESCRIPTOR_TYPE_SAMPLER, k_max_bindless_sampler_count);
            KGLOG_PROCESS_ERROR(!poolCreation.m_vecDescriptorPoolSize.empty());
            VkDescriptorPoolCreateInfo descriptorPoolInfo =
                vks::initializers::DescriptorPoolCreateInfo(
                    (uint32_t)poolCreation.m_vecDescriptorPoolSize.size(),
                    poolCreation.m_vecDescriptorPoolSize.data(),
                    1
                );
            descriptorPoolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            VkResult vkRetCode = vks::vkCreateDescriptorPool(pDevice, &descriptorPoolInfo, nullptr, &m_pDescriptorPool);
            KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS);
        }
        {
            VkDescriptorType bindlessResouceType[] =
            {
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR /* Need to check support if this is desired. */
            };
            VkMutableDescriptorTypeListEXT bindlessResourceTypeList{};
            bindlessResourceTypeList.descriptorTypeCount = sizeof(bindlessResouceType) / sizeof(VkDescriptorType);
            bindlessResourceTypeList.pDescriptorTypes = bindlessResouceType;
            VkMutableDescriptorTypeCreateInfoEXT mutableTypeInfo{};
            mutableTypeInfo.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT;
            mutableTypeInfo.pNext = nullptr;
            mutableTypeInfo.mutableDescriptorTypeListCount = 1;
            mutableTypeInfo.pMutableDescriptorTypeLists = &bindlessResourceTypeList;

            std::vector<VkDescriptorSetLayoutBinding> vecBindings;
            std::vector<VkDescriptorBindingFlagsEXT>  vecBindingFlags;
            VkDescriptorSetLayoutBinding resourceBinding{};
            resourceBinding.binding = 0;
            resourceBinding.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
            resourceBinding.descriptorCount = k_max_bindless_resource_count;
            resourceBinding.stageFlags = VK_SHADER_STAGE_ALL;
            resourceBinding.pImmutableSamplers = nullptr;
            vecBindings.push_back(resourceBinding);
            vecBindingFlags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT);
            VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = 1;
            samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerBinding.descriptorCount = k_max_bindless_sampler_count;
            samplerBinding.stageFlags = VK_SHADER_STAGE_ALL;
            samplerBinding.pImmutableSamplers = nullptr;
            vecBindings.push_back(samplerBinding);
            vecBindingFlags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT);
            VkDescriptorSetLayoutBinding counterBinding{};
            counterBinding.binding = 2;
            counterBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            counterBinding.descriptorCount = k_max_bindless_resource_count;
            counterBinding.stageFlags = VK_SHADER_STAGE_ALL;
            counterBinding.pImmutableSamplers = nullptr;
            vecBindings.push_back(counterBinding);
            vecBindingFlags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT);
          
            VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::DescriptorSetLayoutCreateInfo(vecBindings.data(), static_cast<uint32_t>(vecBindings.size()));
            descriptorLayout.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
            descriptorLayout.pNext = &mutableTypeInfo;
            VkDescriptorSetLayoutBindingFlagsCreateInfoEXT binding_flags{};
            binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
            binding_flags.bindingCount = (uint32_t)vecBindingFlags.size();
            binding_flags.pBindingFlags = vecBindingFlags.data();
            binding_flags.pNext = descriptorLayout.pNext;
            descriptorLayout.pNext = &binding_flags;

            VkDescriptorSetLayoutSupport support{};
            support.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
            vks::vkGetDescriptorSetLayoutSupport(pDevice, &descriptorLayout, &support);
            KGLOG_PROCESS_ERROR(support.supported);
            VkResult result = vks::vkCreateDescriptorSetLayout(pDevice, &descriptorLayout, NULL, &m_pDescriptorSetLayout);
            KGLOG_PROCESS_ERROR(result == VK_SUCCESS);
            VkDescriptorSetAllocateInfo allocInfo = vks::initializers::DescriptorSetAllocateInfo(m_pDescriptorPool, &m_pDescriptorSetLayout, 1);
            result = vks::vkAllocateDescriptorSets(pDevice, &allocInfo, &m_pGlobalBindlessSet);
            KGLOG_PROCESS_ERROR(result == VK_SUCCESS);
        }
        

        bResult = true;
    Exit0:
        return bResult;
    }

    bool KVulkanBindlessManager::FrameMove()
    {
        m_resourceAllocator.Tick();
        m_samplerAllocator.Tick();
        return true;
    }

    bool KVulkanBindlessManager::Uninit()
    {
        VkDevice pDevice = GetVkDevice();
        if (m_pDescriptorSetLayout)
        {
            vks::vkDestroyDescriptorSetLayout(pDevice, m_pDescriptorSetLayout, nullptr);
            m_pDescriptorSetLayout = VK_NULL_HANDLE;
        }
        if (m_pGlobalBindlessSet)
        {
            vks::vkFreeDescriptorSets(pDevice, m_pDescriptorPool, 1, &m_pGlobalBindlessSet);
            m_pGlobalBindlessSet = VK_NULL_HANDLE;
        }
        if (m_pDescriptorPool)
        {
            vks::vkDestroyDescriptorPool(pDevice, m_pDescriptorPool, nullptr);
            m_pDescriptorPool = VK_NULL_HANDLE;
        }
        return false;
    }

    KVulkanBindlessSlot KVulkanBindlessManager::RequestResourceBindlessSolt()
    {
        return m_resourceAllocator.RequestBindlessSlot();
    }
    KVulkanBindlessSlot KVulkanBindlessManager::RequestSamplerBindlessSolt()
    {
        return m_samplerAllocator.RequestBindlessSlot();
    }
    void KVulkanBindlessManager::ReleaseSamplerBindlessSlot(KVulkanBindlessSlot _slot)
    {
        m_samplerAllocator.ReleaseBindlessSlot(_slot);
    }
    void KVulkanBindlessManager::ReleaseResourceBindlessSlot(KVulkanBindlessSlot _slot)
    {
        m_resourceAllocator.ReleaseBindlessSlot(_slot);
    }

    void KVulkanBindlessManager::DelayReleaseResourceBindlessSlot(KVulkanBindlessSlot _slot)
    {
        m_resourceAllocator.DelayReleaseResourceBindlessSlot(_slot);
    }

    bool KVulkanBindlessManager::AddBindlessSRV(IKGFX_TextureView* bindlessSRV)
    {
        ASSERT(bindlessSRV->GetViewDesc().eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV);
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = VK_NULL_HANDLE;
        imageInfo.imageView = (VkImageView)bindlessSRV->GetNativeHandle();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m_CachedDescriptorInfo.m_vecImageAndSamplerInfo.push_back(imageInfo);
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pGlobalBindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = bindlessSRV->GetBindlessHandle();
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &imageInfo;
        m_vecDirtyDescriptorWrite.push_back(write);
        return true;
    }

    bool KVulkanBindlessManager::AddBindlessUAV(IKGFX_TextureView* bindlessUAV)
    {
        ASSERT(bindlessUAV->GetViewDesc().eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV);
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = VK_NULL_HANDLE;
        imageInfo.imageView = (VkImageView)bindlessUAV->GetNativeHandle();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        m_CachedDescriptorInfo.m_vecImageAndSamplerInfo.push_back(imageInfo);
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pGlobalBindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = bindlessUAV->GetBindlessHandle();
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &imageInfo;
        m_vecDirtyDescriptorWrite.push_back(write);
        return true;
    }

    bool KVulkanBindlessManager::AddBindlessSRV(IKGFX_BufferView* bindlessView)
    {
        ASSERT(bindlessView->GetViewDesc()->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV);
        KVulkanBuffer* pVulkanSSBO = (KVulkanBuffer*)bindlessView->GetResource();
        VkDescriptorBufferInfo bufferInfo = pVulkanSSBO->GetDescriptorBufferInfo(true);
        bufferInfo.buffer = (VkBuffer)pVulkanSSBO->GetNativeResourceHandle();
        m_CachedDescriptorInfo.m_vecBufferInfo.push_back(bufferInfo);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pGlobalBindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = bindlessView->GetBindlessHandle();
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        m_vecDirtyDescriptorWrite.push_back(write);
        return true;
    }

    bool KVulkanBindlessManager::AddBindlessUAV(IKGFX_BufferView* bindlessUAV)
    {
        ASSERT(bindlessUAV->GetViewDesc()->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV);
        KVulkanBuffer* pVulkanSSBO = (KVulkanBuffer*)bindlessUAV->GetResource();
        VkDescriptorBufferInfo bufferInfo = pVulkanSSBO->GetDescriptorBufferInfo(true);
        bufferInfo.buffer = (VkBuffer)pVulkanSSBO->GetNativeResourceHandle();
        m_CachedDescriptorInfo.m_vecBufferInfo.push_back(bufferInfo);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pGlobalBindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = bindlessUAV->GetBindlessHandle();
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        m_vecDirtyDescriptorWrite.push_back(write);
        return true;
    }

    bool KVulkanBindlessManager::AddBindlessCBV(IKGFX_BufferView* bindlessCBV)
    {
        ASSERT(bindlessCBV->GetViewDesc()->eViewType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV);
        KVulkanBuffer* pVulkanSSBO = (KVulkanBuffer*)bindlessCBV->GetResource();
        VkDescriptorBufferInfo bufferInfo = pVulkanSSBO->GetDescriptorBufferInfo(true);
        bufferInfo.buffer = (VkBuffer)pVulkanSSBO->GetNativeResourceHandle();
        m_CachedDescriptorInfo.m_vecBufferInfo.push_back(bufferInfo);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pGlobalBindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = bindlessCBV->GetBindlessHandle();
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        m_vecDirtyDescriptorWrite.push_back(write);
        return true;
    }

    bool KVulkanBindlessManager::AddBindlessSampler(IKGFX_SamplerBindlessView* pSamplerState)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = (VkSampler)pSamplerState->GetResource()->GetNativeHandle();
        imageInfo.imageView = VK_NULL_HANDLE;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m_CachedDescriptorInfo.m_vecImageAndSamplerInfo.push_back(imageInfo);
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pGlobalBindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = pSamplerState->GetBindlessHandle();
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &imageInfo;
        m_vecDirtyDescriptorWrite.push_back(write);
        return true;
    }

    bool KVulkanBindlessManager::AddBindlessRayTracingScene(KVulkanBindlessSlot _slot, KRayTracingScene* pScene)
    {
        CHECK_ASSERT(pScene);
        KVulkanRayTracingScene* vkScene = static_cast<KVulkanRayTracingScene*>(pScene);
        VkAccelerationStructureKHR vkAS = vkScene->GetAcceleration();
        VkWriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_info{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
        descriptor_acceleration_structure_info.accelerationStructureCount = 1;
        descriptor_acceleration_structure_info.pAccelerationStructures = &vkAS;
        m_CachedDescriptorInfo.m_vecAccelerationStructureInfo.push_back(descriptor_acceleration_structure_info);

        VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrite.dstSet = m_pGlobalBindlessSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        // The acceleration structure descriptor has to be chained via pNext
        descriptorWrite.pNext = &descriptor_acceleration_structure_info;
        m_vecDirtyDescriptorWrite.push_back(descriptorWrite);
        return true;
    }
    bool KVulkanBindlessManager::Flush()
    {
        if (m_vecDirtyDescriptorWrite.size() > 0)
        {
            VkDevice pDevice = GetVkDevice();
            vks::vkUpdateDescriptorSets(pDevice, (uint32_t)m_vecDirtyDescriptorWrite.size(), m_vecDirtyDescriptorWrite.data(), 0, nullptr);
            m_vecDirtyDescriptorWrite.clear();
        }
        return true;
    }

    const VkDescriptorSet KVulkanBindlessManager::GetGlobalBindlessSet() const
    {
        return m_pGlobalBindlessSet;
    }

    const VkDescriptorSetLayout KVulkanBindlessManager::GetBindlessSetLayout() const
    {
        return m_pDescriptorSetLayout;
    }


    KVulkanBindlessManager::KBindlessDescriptorPoolCreation& KVulkanBindlessManager::KBindlessDescriptorPoolCreation::AddPoolItem(enumDescriptorType descriptorType, uint32_t uCount)
    {
        ASSERT(descriptorType != gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        ASSERT(descriptorType != gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
        m_vecDescriptorPoolSize.push_back(vks::initializers::DescriptorPoolSize(BindlessHelper::GetDescriptorType(descriptorType), uCount));
        return *this;
    }

}

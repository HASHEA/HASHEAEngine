#pragma once
#include "../IGFX_Private.h"
#include "KVulkanFunc.h"
#include "KVulkanTexture.h"
//////////////////////////////////////////////////////////////////////////
#include "optick.h"
#include "KVulkanDefine.h"
#include <KMemory/Public/KAutoRefPtr.h>

class KShaderResourceVK;

namespace gfx
{
    using KVulkanBindlessSlot = uint32_t;
    struct KVulkanBindlessAllocator
    {
        KVulkanBindlessSlot RequestBindlessSlot();
        void ReleaseBindlessSlot(KVulkanBindlessSlot _slot);
        void DelayReleaseResourceBindlessSlot(KVulkanBindlessSlot _slot);
        KVulkanBindlessAllocator()
        {
            m_stackDelayRelease.resize(4);
        }
        void Tick();
    private:
        KVulkanBindlessSlot m_uCurrentAllocatedSlotBound = 0;  // 0 can always be the default error texture
        std::vector<KVulkanBindlessSlot> m_vecFreeSlot;
        std::vector<std::vector<uint32_t>> m_stackDelayRelease;
        uint32_t m_uCurrentDeletionQueueIndex = 0;
    };
    class KVulkanBindlessManager
    {
    public:
        KVulkanBindlessManager() = default;
        ~KVulkanBindlessManager() = default;
    public:
        bool Init_InRenderThread();
        /// <summary>
        /// Tick for delay release
        /// </summary>
        /// <returns></returns>
        bool FrameMove();
        bool Uninit();
    public:
        KVulkanBindlessSlot RequestResourceBindlessSolt();
        KVulkanBindlessSlot RequestSamplerBindlessSolt();
        void ReleaseSamplerBindlessSlot(KVulkanBindlessSlot _slot);
        void ReleaseResourceBindlessSlot(KVulkanBindlessSlot _slot);
        void DelayReleaseResourceBindlessSlot(KVulkanBindlessSlot _slot);
    public:
        bool AddBindlessSRV(IKGFX_TextureView* bindlessSRV);
        bool AddBindlessUAV(IKGFX_TextureView* bindlessUAV);
        bool AddBindlessSRV(IKGFX_BufferView* bindlessSRV);
        bool AddBindlessUAV(IKGFX_BufferView* bindlessUAV);
        bool AddBindlessCBV(IKGFX_BufferView* bindlessCBV);
        bool AddBindlessSampler (IKGFX_SamplerBindlessView* pSamplerState);
        bool AddBindlessRayTracingScene(KVulkanBindlessSlot _slot, KRayTracingScene* pScene);
        bool Flush();
    public:
        const VkDescriptorSet GetGlobalBindlessSet() const;
        const VkDescriptorSetLayout GetBindlessSetLayout() const;
    private:
        VkDescriptorSetLayout m_pDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet m_pGlobalBindlessSet = VK_NULL_HANDLE;
        VkDescriptorPool m_pDescriptorPool = VK_NULL_HANDLE;
        KVulkanBindlessAllocator m_resourceAllocator{};
        KVulkanBindlessAllocator m_samplerAllocator{};
    private:
        struct DescriptorInfoCache
        {
            std::vector<VkDescriptorImageInfo> m_vecImageAndSamplerInfo;
            std::vector<VkDescriptorBufferInfo> m_vecBufferInfo;
            std::vector<VkWriteDescriptorSetAccelerationStructureKHR> m_vecAccelerationStructureInfo;
            void ClearCache();
        }m_CachedDescriptorInfo;
        std::vector<VkWriteDescriptorSet> m_vecDirtyDescriptorWrite;
    private:
        struct KBindlessDescriptorPoolCreation
        {
            std::vector<VkDescriptorPoolSize> m_vecDescriptorPoolSize;
            KBindlessDescriptorPoolCreation& AddPoolItem(enumDescriptorType descriptorType, uint32_t uCount);
        };
    };
}

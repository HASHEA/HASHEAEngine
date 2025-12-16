#pragma once
#include <map>
#include <functional>
#include <vulkan/vulkan_core.h>
#include "KEnginePub/Public/IGFX_Public.h"

namespace gfx
{
    class KVulkanTexture;

    static KGfxSubresourceRange VK_RESOURCE_ALL_SUBRESOURCES = KGfxSubresourceRange(0, 0, UINT32_MAX, UINT32_MAX);
    constexpr uint32_t VK_RESOURCE_BARRIER_ALL_SUBRESOURCES = 0xffffffff;

    /**
     * 资源的layout
     * 1.所有贴图的默认状态由创建时候的各种flag决定
     */
    class KGFX_ResourceLayoutTrackerVK
    {
    public:
        KGFX_ResourceLayoutTrackerVK() = default;
        KGFX_ResourceLayoutTrackerVK(KGfxAccess eInitAccessState)
            : m_AllResourceState(eInitAccessState)
        {
        }

    public:
        inline bool HasSubResourceTransition() const
        {
            return !m_SubresourceState.empty();
        }

        inline KGfxAccess GetAllResourceState() const
        {
            return m_AllResourceState;
        }

        inline void SetAllResourceState(KGfxAccess DstState)
        {
            ASSERT(m_SubresourceState.empty());
            m_AllResourceState = DstState;
        }

        inline void SetTextureSubresourceState(KGfxAccess DstState, uint32_t uSubResourceIndex)
        {
            m_SubresourceState[uSubResourceIndex] = DstState;
        }

        void TravalTextureAllSubResource(const KVulkanTexture* pVulkanTex, KGfxAccess InSrcAccess, KGfxAccess InDstAccess, std::function<void(uint32_t, uint32_t, KGfxAccess, KGfxAccess)> const& TransitionFunction);
        void TravalTextureSubResource(const KVulkanTexture* pVulkanTex, const KGfxSubresourceRange& InSubRange, KGfxAccess InSrcAccess, KGfxAccess InDstAccess, std::function<void(uint32_t, uint32_t, KGfxAccess, KGfxAccess)> const& TransitionFunction);

        inline void ClearSubResourceState()
        {
            m_SubresourceState.clear();
        }

    private:
        KGfxAccess m_AllResourceState = KGfxAccess::Unknown;
        std::map<uint32_t, KGfxAccess> m_SubresourceState = {};
    };
}



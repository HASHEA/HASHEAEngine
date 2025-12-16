#include "KGFX_ResourceLayoutTracker.h"

namespace gfx
{
    void KGFX_ResourceLayoutTracker<false>::SetSubresourceState(D3D12_RESOURCE_STATES state, uint32_t subresource)
    {
        if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            m_AllResourceState = state;
            m_SubresourceState.clear();
        }
        else
        {
            m_SubresourceState[subresource] = state;
        }
    }

    D3D12_RESOURCE_STATES KGFX_ResourceLayoutTracker<false>::GetSubresourceState(uint32_t subresource) const
    {
        D3D12_RESOURCE_STATES state = m_AllResourceState;
        const auto iter = m_SubresourceState.find(subresource);
        if (iter != m_SubresourceState.end())
        {
            state = iter->second;
        }
        return state;
    }

    bool KGFX_ResourceLayoutTracker<false>::GetIsAutoRes() const
    {
        return m_bAutoTranslate;
    }

    const std::map<uint32_t, D3D12_RESOURCE_STATES>& KGFX_ResourceLayoutTracker<false>::GetSubResStateMap()
    {
        return m_SubresourceState;
    }

    bool KGFX_ResourceLayoutTracker<false>::AllSubResHasSameLayout()
    {
        bool bSame = true;

        for (auto& subresource : m_SubresourceState)
        {
            bSame &= subresource.second == m_AllResourceState;
        }

        if (bSame)
        {
            m_SubresourceState.clear();
        }

        return bSame;
    }


    void KGFX_ResourceLayoutTracker<true>::SetSubresourceState(D3D12_RESOURCE_STATES state, uint32_t subresource)
    {

        m_AllResourceState = state;

    }

    D3D12_RESOURCE_STATES KGFX_ResourceLayoutTracker<true>::GetSubresourceState(uint32_t subresource) const
    {
        D3D12_RESOURCE_STATES state = m_AllResourceState;
        return state;
    }

    bool KGFX_ResourceLayoutTracker<true>::GetIsAutoRes() const
    {
        return m_bAutoTranslate;
    }

    const std::map<uint32_t, D3D12_RESOURCE_STATES>& KGFX_ResourceLayoutTracker<true>::GetSubResStateMap()
    {
        static std::map<uint32_t, D3D12_RESOURCE_STATES> emptyMap;
        return emptyMap;
    }

    bool KGFX_ResourceLayoutTracker<true>::AllSubResHasSameLayout()
    {
        bool bSame = true;
        return bSame;
    }
}

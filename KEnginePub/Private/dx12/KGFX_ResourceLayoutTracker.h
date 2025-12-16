#pragma once
#include <assert.h>
#include <bitset>
#include <d3d12.h>
#include <map>

namespace gfx
{
    template<bool bBufLayoutTracker>
    class KGFX_ResourceLayoutTracker;

    /**
     * 资源的layout
     * 1.所有贴图的默认状态由创建时候的各种flag决定
     */

    template<>
    class KGFX_ResourceLayoutTracker<false>
    {
    public:
        KGFX_ResourceLayoutTracker() = default;
        KGFX_ResourceLayoutTracker(D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, bool AutoTranslate = true)
            : m_AllResourceState(state), m_bAutoTranslate(AutoTranslate)
        {
        }

        /**
         * 当提交一次barrier之后，必须更改CPU端的资源状态
         * @param state
         * @param subresource 需要修改的资源子资源索引，如果是D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES则修改所有的子资源（具体计算方式在dx12Header.h文件可以找到）
         */
        void SetSubresourceState(D3D12_RESOURCE_STATES state, uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

        D3D12_RESOURCE_STATES GetSubresourceState(uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const;

        bool GetIsAutoRes() const;

        const std::map<uint32_t, D3D12_RESOURCE_STATES>& GetSubResStateMap();

        bool AllSubResHasSameLayout();

        bool GetSubResBitSet(uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            assert(subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES || subresource < m_SubresourceStateBitSet.size());

            if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                return m_AllResStateBitSet;
            }
            return m_SubresourceStateBitSet[subresource];
        }

        void SetSubResBitSet(uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool value = false)
        {
            assert(subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES || subresource < m_SubresourceStateBitSet.size());

            if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                m_AllResStateBitSet = value;
            }
            else
            {
                m_SubresourceStateBitSet[subresource] = value;
            }
        }

        bool m_bUAVOverLap = false;
    private:
        D3D12_RESOURCE_STATES m_AllResourceState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        std::map<uint32_t, D3D12_RESOURCE_STATES> m_SubresourceState = {};
        std::bitset<32> m_SubresourceStateBitSet = { 0 };
        bool m_AllResStateBitSet = false;
        /**
         * 由于自动转换回接管所有资源的layout切换
         * 这可能导致多余的UAV转换，引起性能问题
         * 所以如果某个UAV不需要转换，设置为false，那么其所有的转换就需要手动去处理
         */
        bool m_bAutoTranslate = true;
    };


    template<>
    class KGFX_ResourceLayoutTracker<true>
    {
    public:
        KGFX_ResourceLayoutTracker() = default;
        KGFX_ResourceLayoutTracker(D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, bool AutoTranslate = true)
            : m_AllResourceState(state), m_bAutoTranslate(AutoTranslate)
        {
        }

        /**
         * 当提交一次barrier之后，必须更改CPU端的资源状态
         * @param state
         * @param subresource 需要修改的资源子资源索引，如果是D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES则修改所有的子资源（具体计算方式在dx12Header.h文件可以找到）
         */
        void SetSubresourceState(D3D12_RESOURCE_STATES state, uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

        D3D12_RESOURCE_STATES GetSubresourceState(uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const;

        bool GetIsAutoRes() const;

        const std::map<uint32_t, D3D12_RESOURCE_STATES>& GetSubResStateMap();

        bool AllSubResHasSameLayout();

        bool GetSubResBitSet(uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            assert(subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            return m_AllResStateBitSet;
        }

        void SetSubResBitSet(uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool value = false)
        {
            assert(subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            m_AllResStateBitSet = value;
        }

        bool m_bUAVOverLap = false;
    private:
        D3D12_RESOURCE_STATES m_AllResourceState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        bool m_AllResStateBitSet = false;
        /**
         * 由于自动转换回接管所有资源的layout切换
         * 这可能导致多余的UAV转换，引起性能问题
         * 所以如果某个UAV不需要转换，设置为false，那么其所有的转换就需要手动去处理
         */
        bool m_bAutoTranslate = true;
    };

}

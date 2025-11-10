#pragma once
#include "Base/hcore.h"
#include "Graphics/RHIResource.h"
#include <map>
#include <functional>
namespace RHI
{
	class VulkanTexture;
	static AshSubresourceRange VK_RESOURCE_ALL_SUBRESOURCES = AshSubresourceRange(0, 0, UINT32_MAX, UINT32_MAX);
	constexpr uint32_t VK_RESOURCE_BARRIER_ALL_SUBRESOURCES = 0xffffffff;
	class VulkanResourceTracker
	{
	public:
		VulkanResourceTracker() = default;
		VulkanResourceTracker(AshResourceState initialState) : m_AllResourceState(initialState){}
	public:
		inline bool has_subresource_transition() const
		{
			return !m_SubresourceState.empty();
		}
		inline AshResourceState get_all_resource_state() const
		{
			return m_AllResourceState;
		}
		inline void set_all_resource_state(AshResourceState DstState)
		{
			H_ASSERT(m_SubresourceState.empty());
			m_AllResourceState = DstState;
		}
		inline void set_texture_subresource_state(AshResourceState DstState, uint32_t uSubResourceIndex)
		{
			m_SubresourceState[uSubResourceIndex] = DstState;
		}
	public:
		void traverse_texture_all_subresource(std::shared_ptr<VulkanTexture> pVulkanTex, AshResourceState InSrcAccess, AshResourceState InDstAccess,
			std::function<void(uint32_t, uint32_t, AshResourceState, AshResourceState)> const& TransitionFunction);
		void traverse_texture_subresource(std::shared_ptr<VulkanTexture>, const AshSubresourceRange& InSubRange, AshResourceState InSrcAccess, AshResourceState InDstAccess,
			std::function<void(uint32_t, uint32_t, AshResourceState, AshResourceState)> const& TransitionFunction);
		inline void clear_subresource_state()
		{
			m_SubresourceState.clear();
		}

	private:
		AshResourceState m_AllResourceState = AshResourceState::Unknown;
		std::map<uint32_t, AshResourceState> m_SubresourceState;
	};
}
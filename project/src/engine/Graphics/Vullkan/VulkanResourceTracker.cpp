#include "VulkanResourceTracker.h"
#include "VulkanTexture.h"
namespace RHI
{
	void VulkanResourceTracker::traverse_texture_all_subresource(std::shared_ptr<VulkanTexture> pVulkanTex, AshResourceState InSrcAccess, AshResourceState InDstAccess, std::function<void(uint32_t, uint32_t, AshResourceState, AshResourceState)> const& TransitionFunction)
	{

		if (InSrcAccess == AshResourceState::Unknown)
		{
			auto& texDesc = pVulkanTex->get_desciption();
			uint32_t SubResourceNum = texDesc.mip_level_count * texDesc.array_layer_count;
			std::vector<AshResourceState> resourceStates;
			resourceStates.resize(SubResourceNum, m_AllResourceState);

			for (const auto& iter : m_SubresourceState)
			{
				resourceStates[iter.first] = iter.second;
			}

			uint32_t Mip = 0;
			uint32_t Slice = 0;
			for (uint32_t i = 0; i < SubResourceNum; ++i)
			{
				TransitionFunction(Mip, Slice, resourceStates[i], InDstAccess);
				++Mip;
				if (Mip >= texDesc.mip_level_count)
				{
					Mip = 0;
					++Slice;
				}
			}
		}
		else
		{
			TransitionFunction(-1, -1, InSrcAccess, InDstAccess);
		}
	}
	void VulkanResourceTracker::traverse_texture_subresource(std::shared_ptr<VulkanTexture> pVulkanTex, const AshSubresourceRange& InSubRange, AshResourceState InSrcAccess, AshResourceState InDstAccess, std::function<void(uint32_t, uint32_t, AshResourceState, AshResourceState)> const& TransitionFunction)
	{
		uint32_t uSliceEnd = InSubRange.uBaseArraySlice + InSubRange.uArrayCount;
		uint32_t uMipEnd = InSubRange.uBaseMipLevel + InSubRange.uMipCount;

		if (InSrcAccess == AshResourceState::Unknown)
		{
			AshSubresourceRange DstSub = InSubRange;
			DstSub = pVulkanTex->resolve_subresource_range(DstSub);

			uint32_t uMipCount = pVulkanTex->get_desciption().mip_level_count;
			for (uint32_t Slice = InSubRange.uBaseArraySlice; Slice < uSliceEnd; ++Slice)
			{
				for (uint32_t Mip = InSubRange.uBaseMipLevel; Mip < uMipEnd; ++Mip)
				{
					uint32_t Index = Mip + Slice * uMipCount;
					AshResourceState SrcAccess = m_AllResourceState;

					const auto ifind = m_SubresourceState.find(Index);
					if (ifind != m_SubresourceState.end())
					{
						SrcAccess = ifind->second;
					}

					TransitionFunction(Mip, Slice, SrcAccess, InDstAccess);
				}
			}
		}
		else
		{
			for (uint32_t Slice = InSubRange.uBaseArraySlice; Slice < uSliceEnd; ++Slice)
			{
				for (uint32_t Mip = InSubRange.uBaseMipLevel; Mip < uMipEnd; ++Mip)
				{
					TransitionFunction(Mip, Slice, InSrcAccess, InDstAccess);
				}
			}
		}
	}
};
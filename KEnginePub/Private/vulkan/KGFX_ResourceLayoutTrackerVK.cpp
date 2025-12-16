#include "KGFX_ResourceLayoutTrackerVK.h"
#include "KVulkanTexture.h"
#include "KBase/Public/KG3D_Base/KG3D_Vector.h"

namespace gfx
{
    void KGFX_ResourceLayoutTrackerVK::TravalTextureAllSubResource(const KVulkanTexture* pVulkanTex, KGfxAccess InSrcAccess, KGfxAccess InDstAccess, std::function<void(uint32_t, uint32_t, KGfxAccess, KGfxAccess)> const& TransitionFunction)
    {
        if (InSrcAccess == KGfxAccess::Unknown)
        {
            auto texDesc = pVulkanTex->GetDesc();
            uint32_t SubResourceNum = texDesc->uMipLevels * texDesc->uArraySize;

            KG3D_Vector<KGfxAccess, 16> ResourceStates;
            ResourceStates.resize(SubResourceNum, m_AllResourceState);

            for (const auto& iter : m_SubresourceState)
            {
                ResourceStates[iter.first] = iter.second;
            }

            uint32_t Mip = 0;
            uint32_t Slice = 0;
            for (uint32_t i = 0; i < SubResourceNum; ++i)
            {
                TransitionFunction(Mip, Slice, ResourceStates[i], InDstAccess);
                ++Mip;
                if (Mip >= texDesc->uMipLevels)
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

    void KGFX_ResourceLayoutTrackerVK::TravalTextureSubResource(
        const KVulkanTexture* pVulkanTex, const KGfxSubresourceRange& InSubRange, KGfxAccess InSrcAccess, KGfxAccess InDstAccess, std::function<void(uint32_t, uint32_t, KGfxAccess, KGfxAccess)> const& TransitionFunction
    )
    {
        uint32_t uSliceEnd = InSubRange.uBaseArraySlice + InSubRange.uArrayCount;
        uint32_t uMipEnd = InSubRange.uBaseMipLevel + InSubRange.uMipCount;

        if (InSrcAccess == KGfxAccess::Unknown)
        {
            KGfxSubresourceRange DstSub = InSubRange;
            DstSub = pVulkanTex->ResolveSubresourceRange(DstSub);

            uint32_t uMipCount = pVulkanTex->GetDesc()->uMipLevels;
            for (uint32_t Slice = InSubRange.uBaseArraySlice; Slice < uSliceEnd; ++Slice)
            {
                for (uint32_t Mip = InSubRange.uBaseMipLevel; Mip < uMipEnd; ++Mip)
                {
                    uint32_t Index = Mip + Slice * uMipCount;
                    KGfxAccess SrcAccess = m_AllResourceState;

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

}

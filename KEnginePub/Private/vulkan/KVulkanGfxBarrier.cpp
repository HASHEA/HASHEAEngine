#include "../stdafx.h"
#include "../IGFX_Private.h"
#include "GFXVulkan.h"
#include "KVulkanDevice.h"
#include "KVulkanInitializers.h"
#include "KVulkanGraphicDevice.h"
#include "KVulkanTexture.h"
#include "kVulkanBuffer.h"
#include "KVulkanCommandBuffer.h"

#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    static VkAccessFlags GetVkAccessMaskForLayout(VkImageLayout eVkLayout, BOOL bUAVUsage = false)
    {
        VkAccessFlags uFlags = 0;

        switch (eVkLayout)
        {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            uFlags = VK_ACCESS_SHADER_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            uFlags = VK_ACCESS_TRANSFER_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            uFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            uFlags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
#ifdef _WIN32
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL_KHR:
        case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL_KHR:
#endif
            uFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR:
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL_KHR:
            uFlags = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
#ifdef _WIN32
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL_KHR:
        case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL_KHR:
#endif
            uFlags = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            uFlags = VK_ACCESS_MEMORY_READ_BIT;
            break;

        case VK_IMAGE_LAYOUT_GENERAL:
            uFlags = bUAVUsage ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
            break;

        case VK_IMAGE_LAYOUT_UNDEFINED:
            uFlags = 0;
            break;
        default:
            DEBUG_BREAK();
            break;
        }

        return uFlags;
    }

    static VkPipelineStageFlags GetVkStageFlagsForLayout(VkImageLayout eVkLayout)
    {
        VkAccessFlags uFlags = 0;

        switch (eVkLayout)
        {
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            uFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            uFlags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            uFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
#ifdef _WIN32
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL_KHR:
        case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL_KHR:
#endif
            uFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR:
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL_KHR:
#ifdef _WIN32
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL_KHR:
        case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL_KHR:
#endif
            uFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            uFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;

        case VK_IMAGE_LAYOUT_GENERAL:
        case VK_IMAGE_LAYOUT_UNDEFINED:
            uFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;

        default:
            DEBUG_BREAK();
            break;
        }

        return uFlags;
    }

    static void GetVkStageAndAccessFlags(KGfxAccess RHIAccess, KGfxBarrier::EType ResourceType, uint32_t UsageFlags, bool bIsDepthStencil, VkPipelineStageFlags& StageFlags, VkAccessFlags& AccessFlags, VkImageLayout& Layout, bool bIsSourceState)
    {
        // From Vulkan's point of view, when performing a multisample resolve via a render pass attachment, resolve targets are the same as render targets .
        // The caller signals this situation by setting both the RTV and ResolveDst flags, and we simply remove ResolveDst in that case,
        // to treat the resource as a render target.
        const KGfxAccess ResolveAttachmentAccess = (KGfxAccess)(KGfxAccess::RTV | KGfxAccess::ResolveDst);
        if (RHIAccess == ResolveAttachmentAccess)
        {
            RHIAccess = KGfxAccess::RTV;
        }

        Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        // The layout to use if SRV access is requested. In case of depth/stencil buffers, we don't need to worry about different states for the separate aspects, since that's handled explicitly elsewhere,
        // and this function is never called for depth-only or stencil-only transitions.
        const VkImageLayout SRVLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;//bIsDepthStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // States which cannot be combined.
        switch (RHIAccess)
        {
        case KGfxAccess::Unknown:
            // We don't know where this is coming from, so we'll stall everything.
            StageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            AccessFlags = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            return;

        case KGfxAccess::CPURead:
            // FIXME: is this correct?
            StageFlags = VK_PIPELINE_STAGE_HOST_BIT;
            AccessFlags = VK_ACCESS_HOST_READ_BIT;
            Layout = VK_IMAGE_LAYOUT_GENERAL;
            return;

        case KGfxAccess::Present:
            StageFlags = bIsSourceState ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            AccessFlags = VK_ACCESS_MEMORY_READ_BIT;
            Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            return;

        case KGfxAccess::RTV:
            StageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            AccessFlags = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            return;

        case KGfxAccess::CopyDst:
            StageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            AccessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
            Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            return;

        case KGfxAccess::ResolveDst:
            // Used when doing a resolve via RHICopyToResolveTarget. For us, it's the same as CopyDst.
            StageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            AccessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
            Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            return;

        case KGfxAccess::SBTRead :
            StageFlags = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
            AccessFlags = VK_ACCESS_MEMORY_READ_BIT;
            return;
        }

        // If DSVWrite is set, we ignore everything else because it decides the layout.
        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::DSVWrite))
        {
            CHECK_ASSERT(bIsDepthStencil);
            StageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            AccessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            return;
        }

        // The remaining flags can be combined.
        StageFlags = 0;
        AccessFlags = 0;

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::IndirectArgs))
        {
            CHECK_ASSERT(ResourceType != KGfxBarrier::EType::Texture);
            StageFlags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
            AccessFlags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::ConstBuffer))
        {
            CHECK_ASSERT(ResourceType != KGfxBarrier::EType::Texture);
            StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            AccessFlags |= VK_ACCESS_UNIFORM_READ_BIT;
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::VertexBuffer) || NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::IndexBuffer))
        {
            CHECK_ASSERT(ResourceType != KGfxBarrier::EType::Texture);
            StageFlags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            switch (ResourceType)
            {
            case KGfxBarrier::EType::Buffer:
                if ((UsageFlags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0)
                {
                    AccessFlags |= VK_ACCESS_INDEX_READ_BIT;
                }
                if ((UsageFlags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0)
                {
                    AccessFlags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                }
                break;
            default:
                DEBUG_BREAK();
                break;
            }
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::DSVRead))
        {
            CHECK_ASSERT(bIsDepthStencil);
            StageFlags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            AccessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

            // If any of the SRV flags is set, the code below will set Layout to SRVLayout again, but it's fine since
            // SRVLayout takes into account bIsDepthStencil and ends up being the same as what we set here.
            Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::SRVGraphics))
        {
            StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            AccessFlags |= VK_ACCESS_SHADER_READ_BIT;

            Layout = SRVLayout;
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::SRVCompute))
        {
            StageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            AccessFlags |= VK_ACCESS_SHADER_READ_BIT;
            // There are cases where we ping-pong images between UAVCompute and SRVCompute. In that case it may be more efficient to leave the image in VK_IMAGE_LAYOUT_GENERAL
            // (at the very least, it will mean fewer image barriers). There's no good way to detect this though, so it might be better if the high level code just did UAV
            // to UAV transitions in that case, instead of SRV <-> UAV.
            Layout = SRVLayout;
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::UAVGraphics))
        {
            StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            AccessFlags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            Layout = VK_IMAGE_LAYOUT_GENERAL;
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)KGfxAccess::UAVCompute))
        {
            StageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            AccessFlags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            Layout = VK_IMAGE_LAYOUT_GENERAL;
        }

        if (NSKMath::HasAnyFlags((uint32_t)RHIAccess, (uint32_t)(KGfxAccess::CopySrc | KGfxAccess::ResolveSrc)))
        {
            // ResolveSrc is used when doing a resolve via RHICopyToResolveTarget. For us, it's the same as CopySrc.
            StageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            AccessFlags = VK_ACCESS_TRANSFER_READ_BIT;
            if (ResourceType == KGfxBarrier::EType::Texture || ResourceType == KGfxBarrier::EType::RenderTarget || ResourceType == KGfxBarrier::EType::TextureView)
            {
                // If this is requested for a texture, make sure it's not combined with other access flags which require a different layout. It's important
                // that this block is last, so that if any other flags set the layout before, we trigger the assert below.
                CHECK_ASSERT(Layout == VK_IMAGE_LAYOUT_UNDEFINED);
                Layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            }
        }
    }

    BOOL KVulkanTransitionImp(KVulkanCommandBuffer* pkvkCmdBuffer, const KGfxBarrier* pBarrierInfos, uint32_t uBarrierCount)
    {
        CHECK_ASSERT(pkvkCmdBuffer);

        VkPipelineStageFlags srcFinalStageMask = 0;
        VkPipelineStageFlags dstFinalStageMask = 0;

        VkMemoryBarrier memBarrier;
        memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier.pNext = nullptr;
        memBarrier.dstAccessMask = 0;
        memBarrier.srcAccessMask = 0;

        BOOL bMemBarrier = false;
        std::vector<VkImageMemoryBarrier> TexBarriers;
        std::vector<VkBufferMemoryBarrier> BufBarriers;

        uint32_t uMemBarriersCount = 0;
        uint32_t uTexBarriersCount = 0;
        uint32_t uBufBarriersCount = 0;

        for (uint32_t i = 0; i < uBarrierCount; i++)
        {
            auto& iter = pBarrierInfos[i];
            if (!iter.pResource)
                continue;

            switch (iter.eType)
            {
            case KGfxBarrier::EType::Texture:
            case KGfxBarrier::EType::RenderTarget:
            case KGfxBarrier::EType::TextureView:
                ++uTexBarriersCount;
                break;
            case KGfxBarrier::EType::Buffer:
                ++uBufBarriersCount;
                break;
            default:
                break;
            }
        }

        if (uTexBarriersCount > 0)
            TexBarriers.reserve(uTexBarriersCount);

        if (uBufBarriersCount > 0)
            BufBarriers.reserve(uBufBarriersCount);

        for (uint32_t i = 0; i < uBarrierCount; i++)
        {
            const auto &iter = pBarrierInfos[i];
            if (!iter.pResource)
                continue;

            const KGfxSubresourceRange &DstSub = static_cast<KGfxSubresourceRange>(iter);
            uint32_t uUsageFlags = 0;
            VkPipelineStageFlags srcStageMask = 0;
            VkPipelineStageFlags dstStageMask = 0;
            VkAccessFlags srcAccessFlags = 0;
            VkAccessFlags dstAccessFlags = 0;
            VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            KVulkanBuffer* pGfxBuffer = nullptr;
            KVulkanTexture* pGfxTexture = nullptr;

            bool bTexR64 = false;
            VkImageAspectFlags aspectMask;

            switch (iter.eType)
            {
            case KGfxBarrier::EType::Buffer:
            {
                uUsageFlags = iter.pBuffer->GetDesc()->uUsageFlags;
                pGfxBuffer = (KVulkanBuffer*)iter.pBuffer;
            }
            break;
            case KGfxBarrier::EType::Texture:
            {
                uUsageFlags = iter.pTexture->GetDesc()->uUsageFlags;
                pGfxTexture = (KVulkanTexture*)iter.pTexture;
                if (iter.pTexture->GetDesc()->eFormat == TEX_FORMAT_R64_UINT)
                {
                    bTexR64 = true;
                }
                aspectMask = pGfxTexture->GetAspectFlags();
            }
            break;
            case KGfxBarrier::EType::TextureView:
            {
                pGfxTexture = ((KVulkanTextureView*)iter.pTextureView)->GetGfxResource();
                uUsageFlags = pGfxTexture->GetDesc()->uUsageFlags;
                if (pGfxTexture->GetDesc()->eFormat == TEX_FORMAT_R64_UINT)
                {
                    bTexR64 = true;
                }
                aspectMask = pGfxTexture->GetAspectFlags();
            }
            break;
            case KGfxBarrier::EType::RenderTarget:
            {
                uUsageFlags = iter.pRenderTarget->GetDesc().uUsageFlags;
                pGfxTexture = (KVulkanTexture*)iter.pRenderTarget->GetTextureResource();
                aspectMask = iter.pRenderTarget->IsForDepth() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                if (iter.pRenderTarget->IsHasStencil())
                {
                    aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
            }
            break;
            case KGfxBarrier::EType::Unknown:
                assert(false);
                break;
            default:
                assert(false);
                break;
    
            }

            if (pGfxTexture)
            {
                KGfxAccess srcAccess = KGfxAccess::Unknown;
                bool bHasLastTextureBarrier = false;
                const bool bIsDepthStencil = NSKMath::HasAnyFlags(uUsageFlags, TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
                const auto texDesc = pGfxTexture->GetDesc();
                VkImage vkTextureImage = pGfxTexture->GetVkHandle();
                auto& TextureLayoutTracker = pGfxTexture->GetLayoutTracker();

                // 对Texture的指定Mip收集其ImageBarrier.
                // 若Mip == (uint32_t)-1 && Slice == (uint32_t)-1，则对Texture整体收集
                auto TextureMipBarrierCollection = [&](uint32_t Mip, uint32_t Slice, KGfxAccess InSrcAccess, KGfxAccess InDstAccess)
                {
                    if (!bTexR64)
                    {
                        GetVkStageAndAccessFlags(InSrcAccess, iter.eType, uUsageFlags, bIsDepthStencil, srcStageMask, srcAccessFlags, srcLayout, true);
                        GetVkStageAndAccessFlags(InDstAccess, iter.eType, uUsageFlags, bIsDepthStencil, dstStageMask, dstAccessFlags, dstLayout, false);
                    }
                    else
                    {
                        GetVkStageAndAccessFlags(NSKMath::HasAnyFlags((uint32_t)InSrcAccess, (uint32_t)KGfxAccess::SRVMask) ? KGfxAccess::UAVMask : InSrcAccess, iter.eType, uUsageFlags, bIsDepthStencil, srcStageMask, srcAccessFlags, srcLayout, true);
                        GetVkStageAndAccessFlags(NSKMath::HasAnyFlags((uint32_t)InDstAccess, (uint32_t)KGfxAccess::SRVMask) ? KGfxAccess::UAVMask : InDstAccess, iter.eType, uUsageFlags, bIsDepthStencil, dstStageMask, dstAccessFlags, dstLayout, false);
                    }

                    srcFinalStageMask |= srcStageMask;
                    dstFinalStageMask |= dstStageMask;

                    // If we're not transitioning across pipes and we don't need to perform layout transitions, we can express memory dependencies through a global memory barrier.
                    if (srcLayout == dstLayout)
                    {
                        static const VkAccessFlags sc_uReadMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
                            VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_TRANSFER_READ_BIT;

                        // We only need a memory barrier if the previous commands wrote to the buffer. In case of a transition from read, an execution barrier is enough.
                        const bool bSrcAccessIsRead = ((srcAccessFlags & (~sc_uReadMask)) == 0);

                        if (!bSrcAccessIsRead)
                        {
                            memBarrier.srcAccessMask |= srcAccessFlags;
                            memBarrier.dstAccessMask |= dstAccessFlags;
                            bMemBarrier = true;
                        }
                        return;
                    }

                    if (Mip == (uint32_t)-1 && Slice == (uint32_t)-1)
                    {
                        // 对Texture整体做ImageBarrier
                        pGfxTexture->GetLayoutTracker().SetAllResourceState(InDstAccess);

                        VkImageSubresourceRange AllSubresourceRange;
                        AllSubresourceRange.aspectMask = aspectMask;

                        AllSubresourceRange.baseMipLevel = 0;
                        AllSubresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
                        AllSubresourceRange.baseArrayLayer = 0;
                        AllSubresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

                        VkImageMemoryBarrier imageBarrier;
                        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        imageBarrier.pNext = nullptr;
                        imageBarrier.srcAccessMask = srcAccessFlags;
                        imageBarrier.dstAccessMask = dstAccessFlags;
                        imageBarrier.oldLayout = srcLayout;
                        imageBarrier.newLayout = dstLayout;
                        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        imageBarrier.image = vkTextureImage;
                        imageBarrier.subresourceRange = AllSubresourceRange;
                        
                        TexBarriers.emplace_back(imageBarrier);
                    }
                    else
                    {
                        pGfxTexture->GetLayoutTracker().SetTextureSubresourceState(InDstAccess, KVulkanTexture::CalSubresourceIndex(Mip, texDesc->uMipLevels, Slice));

                        bool bNewBarrier = true;
                        if (bHasLastTextureBarrier)
                        {
                            // 若当前Texture产生过ImageBarrier.
                            // 尝试把相同Layout转换的连续Mips合并到一个ImageBarrier上.
                            // 但无法合并连续ArraySlice的ImageBarrier.

                            auto& LastBarrier = TexBarriers.back();
                            if (
                                srcLayout == LastBarrier.oldLayout &&
                                dstLayout == LastBarrier.newLayout &&
                                LastBarrier.subresourceRange.baseArrayLayer == Slice &&
                                LastBarrier.subresourceRange.levelCount + LastBarrier.subresourceRange.baseMipLevel == Mip &&
                                LastBarrier.image == vkTextureImage
                            )
                            {
                                ++LastBarrier.subresourceRange.levelCount;
                                bNewBarrier = false;
                            }
                        }

                        if (bNewBarrier)
                        {
                            // 对Texture产生一个新的Mip的ImageBarrier.
                            VkImageSubresourceRange subresourceRange = {};
                            subresourceRange.aspectMask = aspectMask;

                            subresourceRange.baseMipLevel = Mip;
                            subresourceRange.levelCount = 1;
                            subresourceRange.baseArrayLayer = Slice;
                            subresourceRange.layerCount = 1;

                            VkImageMemoryBarrier imageBarrier;
                            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                            imageBarrier.pNext = nullptr;
                            imageBarrier.srcAccessMask = srcAccessFlags;
                            imageBarrier.dstAccessMask = dstAccessFlags;
                            imageBarrier.oldLayout = srcLayout;
                            imageBarrier.newLayout = dstLayout;
                            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            imageBarrier.image = vkTextureImage;
                            imageBarrier.subresourceRange = subresourceRange;

                            TexBarriers.emplace_back(imageBarrier);

                            bHasLastTextureBarrier = true;
                        }
                    }
                };

                bool bDstAllResourceTransition = DstSub.IsWholeResource() || (DstSub.uArrayCount == texDesc->uArraySize && DstSub.uMipCount == texDesc->uMipLevels);
                if (bDstAllResourceTransition)
                {
                    bool bHasSubResourceTransition = TextureLayoutTracker.HasSubResourceTransition();
                    if (!bHasSubResourceTransition)
                    {
                        srcAccess = iter.eSRCAccess == KGfxAccess::Unknown ? TextureLayoutTracker.GetAllResourceState() : iter.eSRCAccess;
                        if (pGfxTexture->IsEnableUAVOverlap() && NSKMath::HasAnyFlags((uint32_t)srcAccess, (uint32_t)KGfxAccess::UAVMask) && NSKMath::HasAnyFlags((uint32_t)iter.eDSTAccess, (uint32_t)KGfxAccess::UAVMask))
                        {
                            continue;
                        }

                        TextureMipBarrierCollection((uint32_t)-1, (uint32_t)-1, srcAccess, iter.eDSTAccess);
                    }
                    else
                    {
                        TextureLayoutTracker.TravalTextureAllSubResource(pGfxTexture, iter.eSRCAccess, iter.eDSTAccess, TextureMipBarrierCollection);
                        TextureLayoutTracker.ClearSubResourceState();
                        pGfxTexture->GetLayoutTracker().SetAllResourceState(iter.eDSTAccess);
                    }
                }
                else
                {
                    TextureLayoutTracker.TravalTextureSubResource(pGfxTexture, DstSub, iter.eSRCAccess, iter.eDSTAccess, TextureMipBarrierCollection);
                }
            }

            if (pGfxBuffer)
            {
                KGfxAccess srcAccess = iter.eSRCAccess == KGfxAccess::Unknown ? pGfxBuffer->GetLayoutTracker().GetAllResourceState() : iter.eSRCAccess;

                if (pGfxBuffer->IsEnableUAVOverlap() && NSKMath::HasAnyFlags((uint32_t)srcAccess, (uint32_t)KGfxAccess::UAVMask) && NSKMath::HasAnyFlags((uint32_t)iter.eDSTAccess, (uint32_t)KGfxAccess::UAVMask))
                {
                    continue;
                }

                GetVkStageAndAccessFlags(srcAccess, iter.eType, uUsageFlags, false, srcStageMask, srcAccessFlags, srcLayout, true);
                GetVkStageAndAccessFlags(iter.eDSTAccess, iter.eType, uUsageFlags, false, dstStageMask, dstAccessFlags, dstLayout, false);

                srcFinalStageMask |= srcStageMask;
                dstFinalStageMask |= dstStageMask;

                // If we're not transitioning across pipes and we don't need to perform layout transitions, we can express memory dependencies through a global memory barrier.
                if (srcLayout == dstLayout)
                {
                    static const VkAccessFlags sc_uReadMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
                        VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_TRANSFER_READ_BIT;

                    // We only need a memory barrier if the previous commands wrote to the buffer. In case of a transition from read, an execution barrier is enough.
                    const bool bSrcAccessIsRead = ((srcAccessFlags & (~sc_uReadMask)) == 0);

                    if (!bSrcAccessIsRead)
                    {
                        memBarrier.srcAccessMask |= srcAccessFlags;
                        memBarrier.dstAccessMask |= dstAccessFlags;
                        bMemBarrier = true;
                    }
                    continue;
                }

                VkBufferMemoryBarrier bufferBarrier;
                bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                bufferBarrier.pNext = nullptr;
                bufferBarrier.srcAccessMask = srcAccessFlags;
                bufferBarrier.dstAccessMask = dstAccessFlags;
                bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bufferBarrier.buffer = pGfxBuffer->GetVkBuffer();
                bufferBarrier.offset = 0;
                bufferBarrier.size = VK_WHOLE_SIZE;

                BufBarriers.emplace_back(bufferBarrier);
                pGfxBuffer->GetLayoutTracker().SetAllResourceState(iter.eDSTAccess);
                continue;
            }
        }

        if (srcFinalStageMask != 0 || dstFinalStageMask != 0 || bMemBarrier || !BufBarriers.empty() || !TexBarriers.empty())
        {
            vks::vkCmdPipelineBarrier(pkvkCmdBuffer->GetCommandBuffer(),
                srcFinalStageMask,
                dstFinalStageMask,
                0,
                (bMemBarrier ? 1 : 0), (bMemBarrier ? &memBarrier : nullptr),
                (uint32_t)BufBarriers.size(), !BufBarriers.empty() ? BufBarriers.data() : nullptr,
                (uint32_t)TexBarriers.size(), !TexBarriers.empty() ? TexBarriers.data() : nullptr
            );
        }

        return true;
    }
}

#include "KVulkanGraphicDevice.h"

#include "GFXVulkan.h"
#include "kVulkanBuffer.h"
#include "KVulkanRayTracing.h"
#include "KVulkanFunc.h"
#include "KVulkanDevice.h"
#include "KVulkanTools.h"
#include "KVulkanInitializers.h"
#include "KVulkanSwapChain.h"
#include "KVulkanRenderContext.h"
#include "KVulkanDebug.h"
#include "KVulkanStagingManager.h"
#include "KVulkanBindlessManager.h"
#include "KVulkanUploadCmdBufferManager.h"
#include "KVulkanDynamicRingBuffer.h"
#include "KVulkanRenderFrameBuffer.h"
#include "KVulkanCommandBuffer.h"
#include "KBase/Public/str/KUtf8Convert.h"
#include "KBase/Public/KG3D_Base/KG3D_Vector.h"
#include "KEnginePub/Public/switchoption/KEngineSwitchOption.h"
#include "KEnginePub/Public/IGFX_RHIHelper.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KVulkanTexture.h"

//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"
#include "KGFX_GraphicDeviceVK.h"

namespace gfx
{
    const KGfxTextureFormatInfo& GetTextureFormatInfoVk(enumTextureFormat eFormat);

    extern RenderPassMap                                gRenderPassMap;
    extern std::unordered_map<ThreadID, FrameBufferMap> gFrameBufferMap;
    extern std::mutex                                   gFrameBufferMutex;

    KVulkanGraphicDevice::KVulkanGraphicDevice()
    {
        m_bInited = false;
        m_pGlobalDynamicBufferRing = nullptr;
    }

    KVulkanGraphicDevice::~KVulkanGraphicDevice()
    {
        VkDevice pDevice = GetVkDevice();
        vks::vkDestroySamplers(pDevice);
    }

    BOOL KVulkanGraphicDevice::CreateSwapChain(KVulkanSwapChain** ppSwapChain, KWindow* pWindow, uint32_t viewid)
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        VkBool32            validDepthFormat = false;
        std::string         androidProduct;
        // m_pDevice = g_pVulkanDevice->m_pLogicalDevice;
        // m_pPhysicalDevice = g_pVulkanDevice->m_pPhysicalDevice;
        VkPhysicalDevice    pPhysicalDevice = GetVkPhysicalDevice();
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        VkDevice            pDevice = GetVkDevice();

        KVulkanSwapChain* pSwapChain = new KVulkanSwapChain();
        pSwapChain->Connect(pVulkanDevice->m_pInstance, pPhysicalDevice, pDevice);

#if defined(_WIN32)
        pSwapChain->InitSurface(pWindow->m_connection, pWindow->m_window);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
        pSwapChain->InitSurface(pWindow->m_connection, pWindow->m_window);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        pSwapChain->InitSurface(pWindow->m_window);
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
        pSwapChain->InitSurface(pWindow->m_window);
#elif defined(VK_USE_PLATFORM_OHOS)
        pSwapChain->InitSurface(pWindow->m_window);
#endif

        BOOL bRetCode = pSwapChain->Create(pWindow->m_szWindowName, &pWindow->m_uSwapChainWidth, &pWindow->m_uSwapChainHeight, pVulkanDevice->m_Settings.m_bVsync);
        KGLOG_ASSERT_EXIT(bRetCode);

        *ppSwapChain = pSwapChain;

        m_swapChainSurfaceFormat = pSwapChain->GetSurfaceFormat();

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::DestroySwapChain(KVulkanSwapChain* pSwapChain)
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        if (pSwapChain)
        {
            KVulkanSwapChain* pSwapChainProxy = pSwapChain;

            pSwapChainProxy->Cleanup();

            SAFE_DELETE(pSwapChainProxy);
        }

        bRet = TRUE;
        // Exit0:
        return bRet;
    }


    BOOL KVulkanGraphicDevice::DestroyCommandBuffer(KVulkanCommandBuffer*& pRefCmd)
    {
        PROF_CPU();
        ASSERT(IsMainThread());
        // ASSERT(pRefCmd->GetRef() == 1);
        BOOL                  bRetCode = false;
        KVulkanCommandBuffer* pGfxCmdBuffer = nullptr;
        KG_PROCESS_SUCCESS(!pRefCmd);

        // 保证不要多次放入同一个
        ASSERT(pRefCmd->m_delayReleaseCounter == 0);
        pRefCmd->m_delayReleaseCounter = DELAY_RELEASE_FRAME_COUNT;
        CopyCurrentSwapChainLoopCount(pRefCmd->m_uReleaseSwapChainLoopCount, gfx::CONTEXT_COUNT);
        // pRefCmd->Reset(false);

        if (pRefCmd->m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {
            m_lsDelayReleasePrimaryCommandBuffer.push_back(pRefCmd);
        }
        else
        {
            m_lsDelayReleaseSecondaryCommandBuffer.push_back(pRefCmd);
        }
        // 放入延时删除队列，外面不再持有了
        pRefCmd = nullptr;

    Exit1:
        bRetCode = true;
        // Exit0:
        return bRetCode;
    }

    void KVulkanGraphicDevice::CmdUpdateSubResource(
        KVulkanRenderContext* pRenderCtx, IKGFX_Buffer* pGfxBuffer,
        uint32_t uOffset, uint32_t uSize,
        const void* pData, uint32_t option
    )
    {
        PROF_CPU_DETAIL();
        CHECK_ASSERT(uSize != 0);
        CHECK_ASSERT(pRenderCtx);

        BOOL bRetCode = FALSE;
        uint64_t uRenderPassTick = 0;

        auto pkvkCmdBuffer = pRenderCtx->GetVulkanCommandBuffer();
        CHECK_ASSERT(pkvkCmdBuffer);

        KVulkanBuffer* pkvkBuffer = (KVulkanBuffer*)pGfxBuffer;
        CHECK_ASSERT(pkvkBuffer);

        auto& bufDesc = *pkvkBuffer->GetDesc();
        KGLOG_ASSERT_EXIT(bufDesc.eResAccessFlags != KGfxResourceAccessType::KGfxResourceAccess_Read);
        KGLOG_ASSERT_EXIT(bufDesc.uByteWidth > uOffset);
        KGLOG_ASSERT_EXIT(!pkvkBuffer->IsDynamic());

        if (uSize == 0)
        {
            uSize = bufDesc.uByteWidth - uOffset;
        }
        else
        {
            KGLOG_ASSERT_EXIT(bufDesc.uByteWidth >= (uOffset + uSize));
        }
        KGLOG_ASSERT_EXIT(uSize);

        uRenderPassTick = pRenderCtx->GetCurRenderPassCount();
        if (uRenderPassTick != (uint64_t)-1)
        {
            // 若在RenderPass中触发更新
            uint64_t uLastBufferUpdateTickInRenderPass = pkvkBuffer->GetUpdateRenderPassTick();
            if (uLastBufferUpdateTickInRenderPass >= uRenderPassTick)
            {
                // RenderPass中只能更新一次
                DEBUG_BREAK();
            }
            pkvkBuffer->SetUpdateRenderPassTick(uRenderPassTick);
        }

        if (bufDesc.eResAccessFlags == KGfxResourceAccessType::KGfxResourceAccess_GPUOnly)
        {
            KGLOG_ASSERT_EXIT(pkvkCmdBuffer);
            KGLOG_ASSERT_EXIT(pkvkCmdBuffer->m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY);
            KVulkanStagingBuffer* StagingBuffer = nullptr;

            // alloc temp transform staging buffer for writing, then data will be commit on unmapping operation.
            StagingBuffer = m_pStagingMgr->AllocBuffer(uSize, nullptr, false);
            KGLOG_ASSERT_EXIT(StagingBuffer);

            BOOL bMapped = StagingBuffer->Map(0, uSize);
            memcpy((uint8_t*)StagingBuffer->GetMappedMemoryPtr(), pData, uSize);
            StagingBuffer->Unmap();

            if (bMapped)
            {
                VkBufferCopy copyRegion;
                copyRegion.dstOffset = uOffset;
                copyRegion.srcOffset = 0;
                copyRegion.size = uSize;
                VkBuffer       srcBuffer = StagingBuffer->GetVkBufferHandle();
                VkBuffer       destBuffer = pkvkBuffer->GetVkBuffer();
                auto           pGfxDevice = gfx::GetGraphicDevice();
                const uint32_t accessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

                pRenderCtx->Transition({ pkvkBuffer, KGfxAccess::Unknown, KGfxAccess::CopyDst });

                vks::vkCmdCopyBuffer(pkvkCmdBuffer->GetCommandBuffer(), StagingBuffer->GetVkBufferHandle(), pkvkBuffer->GetVkBuffer(), 1, &copyRegion);
            }
            m_pStagingMgr->FreeBuffer(pkvkCmdBuffer, StagingBuffer);
        }
        else
        {
            if (!DrvOption::bX3D_VK_USE_VMA)
            {
                KG_ASSERT_EXIT(pkvkBuffer->GetVkMemorySize() >= uSize && uSize);

                VkDevice pDevice = GetVkDevice();
                void* p = nullptr;
                if (vks::vkMapMemory(pDevice, pkvkBuffer->GetVkMemory(), pkvkBuffer->GetVkMemoryOffset(), pkvkBuffer->GetVkMemorySize(), 0, &p) == VK_SUCCESS)
                {
                    if (p)
                    {
                        if (uOffset)
                        {
                            uint8_t* pDst = (uint8_t*)p;
                            memcpy(pDst + uOffset, pData, uSize);
                        }
                        else
                        {
                            memcpy(p, pData, uSize);
                        }
                    }
                    pkvkBuffer->FlushMappedRanges();
                }

                vks::vkUnmapMemory(pDevice, pkvkBuffer->GetVkMemory());
            }
            else
            {
                VmaAllocation pVMAllocation = pkvkBuffer->VMAGetAllocation();
                ASSERT(pVMAllocation);
                vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
                ASSERT(pVulkanDevice);

                void* pMapData = nullptr;
                bRetCode = pVulkanDevice->VMAMapMemory(pVMAllocation, &pMapData);
                pMapData = (char*)pMapData + uOffset;
                KGLOG_ASSERT_EXIT(bRetCode && pMapData);
                memcpy(pMapData, pData, uSize);
                pkvkBuffer->FlushMappedRanges();
                pVulkanDevice->VMAUnmapMemory(pVMAllocation);
            }
        }
    Exit0:
        return;
    }

    void KVulkanGraphicDevice::CmdUpdateSubResource(
        KVulkanRenderContext* pRenderCtx,
        IKGFX_TextureResource* pGfxTexture,
        uint32_t uDstMipLevel, uint32_t uDstArraySlice, const KGfxCopyRegion* pDstRegion,
        const void* pSrcData, uint32_t uSrcRowPitch, uint32_t uSrcDepthPitch
    )
    {
        PROF_CPU();
        // pCmdBuffer can not be in render pass scope, using upload cmd buffer instead temporary.
        // todo:
        KVulkanStagingBuffer* StagingBuffer = nullptr;
        KGfxCopyRegion        dstCopyRegion = {};
        uint32_t              uSubTexWidth = 0;
        uint32_t              uSubTexHeight = 0;
        uint32_t              uSubTexDepth = 0;
        uint32_t              uSubTexBlockWidth = 0;
        uint32_t              uSubTexBlockHeight = 0;
        uint32_t              uSubTexByteSlice = 0;
        uint32_t              uSubTexByteRowPitch = 0;
        uint32_t              uStagingSize = 0;
        uint32_t              uDstBlockWidth = 0;
        uint32_t              uDstBlockHeight = 0;
        uint32_t              uDstDepth = 0;
        uint32_t              uStagingByteRowPitch = 0;
        uint32_t              uStagingByteSlice = 0;
        KGFX_TextureDesc      texDesc;
        KGfxTextureFormatInfo texFormatInfo;

        KVulkanTexture* pkvkTexture = (KVulkanTexture*)pGfxTexture;
        KGLOG_ASSERT_EXIT(pkvkTexture);
        KGLOG_ASSERT_EXIT(pRenderCtx);

        texDesc = *pkvkTexture->GetDesc();
        texFormatInfo = GetTextureFormatInfoVk(texDesc.eFormat);

        {
            uSubTexWidth = texDesc.uWidth >> uDstMipLevel;
            uSubTexWidth = std::max(uSubTexWidth, 1u);

            if (texDesc.eDimension != TextureDimensionType::Texture1D)
            {
                uSubTexHeight = texDesc.uHeight >> uDstMipLevel;
                uSubTexHeight = std::max(uSubTexHeight, 1u);
            }
            else
            {
                uSubTexHeight = 1;
            }

            if (texDesc.eDimension == TextureDimensionType::Texture3D)
            {
                uSubTexDepth = texDesc.uDepth >> uDstMipLevel;
                uSubTexDepth = std::max(uSubTexDepth, 1u);
            }
            else
            {
                uSubTexDepth = 1;
            }

            uSubTexBlockWidth = NSKMath::CeilIntDIV(uSubTexWidth, texFormatInfo.uWidthPerBlock);
            uSubTexBlockWidth = std::max(uSubTexBlockWidth, 1u);

            uSubTexBlockHeight = NSKMath::CeilIntDIV(uSubTexHeight, texFormatInfo.uHeightPerBlock);
            uSubTexBlockHeight = std::max(uSubTexBlockHeight, 1u);

            KGLOG_ASSERT_EXIT(texDesc.uArraySize > uDstArraySlice);
            KGLOG_ASSERT_EXIT(texDesc.uMipLevels > uDstMipLevel);

            if (pDstRegion)
            {
                dstCopyRegion = *pDstRegion;
                KGLOG_ASSERT_EXIT(dstCopyRegion.left < uSubTexWidth);
                KGLOG_ASSERT_EXIT(dstCopyRegion.left < dstCopyRegion.right);

                KGLOG_ASSERT_EXIT(dstCopyRegion.top < uSubTexHeight);
                KGLOG_ASSERT_EXIT(dstCopyRegion.top < dstCopyRegion.bottom);

                KGLOG_ASSERT_EXIT(dstCopyRegion.front < uSubTexDepth);
                KGLOG_ASSERT_EXIT(dstCopyRegion.front < dstCopyRegion.back);

                KGLOG_ASSERT_EXIT(dstCopyRegion.right <= uSubTexWidth);
                KGLOG_ASSERT_EXIT(dstCopyRegion.bottom <= uSubTexHeight);
                KGLOG_ASSERT_EXIT(dstCopyRegion.back <= uSubTexDepth);

                uDstBlockWidth = NSKMath::CeilIntDIV(pDstRegion->right - pDstRegion->left, texFormatInfo.uWidthPerBlock);
                uDstBlockWidth = std::max(uDstBlockWidth, 1u);

                if (texDesc.eDimension != TextureDimensionType::Texture1D)
                {
                    uDstBlockHeight = NSKMath::CeilIntDIV(pDstRegion->bottom - pDstRegion->top, texFormatInfo.uHeightPerBlock);
                    uDstBlockHeight = std::max(uDstBlockHeight, 1u);
                }
                else
                {
                    uDstBlockHeight = 1;
                }

                uDstDepth = texDesc.eDimension == TextureDimensionType::Texture3D ? pDstRegion->back - pDstRegion->front : 1;
                KGLOG_ASSERT_EXIT(uDstDepth > 0);

                uStagingSize = uDstBlockWidth * uDstBlockHeight * uDstDepth * texFormatInfo.uBytesPerBlock;
                if (texFormatInfo.uWidthPerBlock > 1 || texFormatInfo.uHeightPerBlock > 1)
                {
                    KGLOG_ASSERT_EXIT(dstCopyRegion.left % texFormatInfo.uWidthPerBlock == 0);
                    KGLOG_ASSERT_EXIT(dstCopyRegion.top % texFormatInfo.uHeightPerBlock == 0);
                    KGLOG_ASSERT_EXIT(dstCopyRegion.right % texFormatInfo.uWidthPerBlock == 0);
                    KGLOG_ASSERT_EXIT(dstCopyRegion.bottom % texFormatInfo.uHeightPerBlock == 0);
                }
            }
            else
            {
                dstCopyRegion.left = 0;
                dstCopyRegion.top = 0;
                dstCopyRegion.front = 0;
                dstCopyRegion.right = uSubTexWidth;
                dstCopyRegion.bottom = uSubTexHeight;
                dstCopyRegion.back = uSubTexDepth;

                uStagingSize = uSubTexBlockWidth * uSubTexBlockHeight * uSubTexDepth * texFormatInfo.uBytesPerBlock;

                uDstBlockWidth = uSubTexBlockWidth;
                uDstBlockHeight = uSubTexBlockHeight;
                uDstDepth = uSubTexDepth;
            }

            uSubTexByteRowPitch = uSubTexBlockWidth * texFormatInfo.uBytesPerBlock;
            uSubTexByteSlice = uSubTexByteRowPitch * uSubTexBlockHeight;
        }

        // alloc temp transform staging buffer for writing, then data will be commit on unmapping operation.
        StagingBuffer = m_pStagingMgr->AllocBuffer(uStagingSize, nullptr, false);
        KGLOG_ASSERT_EXIT(StagingBuffer);

        if (uSrcDepthPitch == 0)
        {
            uSrcDepthPitch = uSubTexByteSlice;
        }

        if (uSrcRowPitch == 0)
        {
            uSrcRowPitch = uSubTexByteRowPitch;
        }

        uStagingByteRowPitch = texFormatInfo.uBytesPerBlock * uDstBlockWidth;
        uStagingByteSlice = uStagingByteRowPitch * uDstBlockHeight;
        KGLOG_ASSERT_EXIT(uSrcRowPitch >= uStagingByteRowPitch);

        StagingBuffer->Map(0, uStagingSize);
        for (uint32_t d = 0; d < uDstDepth; ++d)
        {
            uint8_t* pDstMem = (uint8_t*)StagingBuffer->GetMappedMemoryPtr() + d * uStagingByteSlice;
            uint8_t* pSrcMem = (uint8_t*)pSrcData + d * uSrcDepthPitch;

            for (uint32_t y = 0; y < uDstBlockHeight; ++y)
            {
                uint8_t* pDstRowMem = pDstMem + y * uStagingByteRowPitch;
                uint8_t* pSrcRowMem = pSrcMem + y * uSrcRowPitch;

                memcpy(pDstRowMem, pSrcRowMem, uStagingByteRowPitch);
            }
        }
        StagingBuffer->Unmap();

        {
            VkBufferImageCopy copyRegion;
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.baseArrayLayer = uDstArraySlice;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageSubresource.mipLevel = uDstMipLevel;
            copyRegion.imageSubresource.aspectMask = pkvkTexture->GetAspectFlags();
            copyRegion.imageOffset.x = dstCopyRegion.left;
            copyRegion.imageOffset.y = dstCopyRegion.top;
            copyRegion.imageOffset.z = dstCopyRegion.front;
            copyRegion.imageExtent.width = dstCopyRegion.right - dstCopyRegion.left;
            copyRegion.imageExtent.height = dstCopyRegion.bottom - dstCopyRegion.top;
            copyRegion.imageExtent.depth = dstCopyRegion.back - dstCopyRegion.front;

            pRenderCtx->Transition({ pkvkTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst, uDstMipLevel, uDstArraySlice, 1, 1 });

            vks::vkCmdCopyBufferToImage(
                pRenderCtx->GetCommandBufferVk(),
                StagingBuffer->GetVkBufferHandle(),
                pkvkTexture->GetVkHandle(), VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copyRegion
            );
        }

        m_pStagingMgr->FreeBuffer(pRenderCtx->GetVulkanCommandBuffer(), StagingBuffer);
    Exit0:
        return;
    }

    void KVulkanGraphicDevice::CmdUpdateAllResource(KVulkanRenderContext* pRenderCtx, IKGFX_TextureResource* pGfxTexture, std::vector<gfx::KGfxSubResourceData>& data)
    {
        PROF_CPU();

        KVulkanStagingBuffer* StagingBuffer = nullptr;
        uint32_t                       uStagingSize = 0;
        uint32_t                       uStagingMemOffset = 0;
        uint32_t                       uSubTexIndex = 0;
        uint32_t                       uSubResourceCount = (uint32_t)data.size();
        uint8_t* pStagingMemory = nullptr;
        KGFX_TextureDesc               texDesc;
        KGfxTextureFormatInfo          texFormatInfo;
        std::vector<VkBufferImageCopy> copyRegions;

        struct SUB_RESOURCE_INTO
        {
            uint16_t uSubTexWidth = 0;
            uint16_t uSubTexHeight = 0;
            uint16_t uSubTexDepth = 0;

            uint16_t uSubTexBlockWidth = 0;
            uint16_t uSubTexBlockHeight = 0;

            uint32_t uByteRowPitch = 0;
            uint32_t uByteSlice = 0;
            uint32_t uByteFullSize = 0;
        };
        KG3D_Vector<SUB_RESOURCE_INTO, 10> SubResourceInfos;

        KVulkanTexture* pkvkTexture = (KVulkanTexture*)pGfxTexture;
        KGLOG_ASSERT_EXIT(pkvkTexture);
        KGLOG_ASSERT_EXIT(pRenderCtx);

        texDesc = *pkvkTexture->GetDesc();
        texFormatInfo = GetTextureFormatInfoVk(texDesc.eFormat);

        KGLOG_ASSERT_EXIT(uSubResourceCount == texDesc.uArraySize * texDesc.uMipLevels);
        SubResourceInfos.resize(uSubResourceCount);

        for (uint32_t uArr = 0; uArr < texDesc.uArraySize; ++uArr)
        {
            for (uint32_t uMip = 0; uMip < texDesc.uMipLevels; uMip++)
            {
                auto& iter = SubResourceInfos[uSubTexIndex];
                ++uSubTexIndex;

                iter.uSubTexWidth = texDesc.uWidth >> uMip;
                iter.uSubTexWidth = std::max<uint16_t>(iter.uSubTexWidth, 1u);

                if (texDesc.eDimension != TextureDimensionType::Texture1D)
                {
                    iter.uSubTexHeight = texDesc.uHeight >> uMip;
                    iter.uSubTexHeight = std::max<uint16_t>(iter.uSubTexHeight, 1u);
                }
                else
                {
                    iter.uSubTexHeight = 1;
                }

                if (texDesc.eDimension == TextureDimensionType::Texture3D)
                {
                    iter.uSubTexDepth = texDesc.uDepth >> uMip;
                    iter.uSubTexDepth = std::max<uint16_t>(iter.uSubTexDepth, 1u);
                }
                else
                {
                    iter.uSubTexDepth = 1;
                }

                iter.uSubTexBlockWidth = NSKMath::CeilIntDIV((uint32_t)iter.uSubTexWidth, texFormatInfo.uWidthPerBlock);
                iter.uSubTexBlockWidth = std::max<uint16_t>(iter.uSubTexBlockWidth, 1u);

                iter.uSubTexBlockHeight = NSKMath::CeilIntDIV((uint32_t)iter.uSubTexHeight, texFormatInfo.uHeightPerBlock);
                iter.uSubTexBlockHeight = std::max<uint16_t>(iter.uSubTexBlockHeight, 1u);

                iter.uByteRowPitch = texFormatInfo.uBytesPerBlock * iter.uSubTexBlockWidth;
                iter.uByteSlice = iter.uByteRowPitch * iter.uSubTexBlockHeight;
                iter.uByteFullSize = iter.uByteSlice * iter.uSubTexDepth;

                uStagingSize += iter.uByteFullSize;
            }
        }

        // alloc temp transform staging buffer for writing, then data will be commit on unmapping operation.
        StagingBuffer = m_pStagingMgr->AllocBuffer(uStagingSize, nullptr, false);
        KGLOG_ASSERT_EXIT(StagingBuffer);

        StagingBuffer->Map(0, uStagingSize);
        pStagingMemory = (uint8_t*)StagingBuffer->GetMappedMemoryPtr();

        uSubTexIndex = 0;
        for (uint32_t uArr = 0; uArr < texDesc.uArraySize; ++uArr)
        {
            for (uint32_t uMip = 0; uMip < texDesc.uMipLevels; uMip++)
            {
                const auto& iter = SubResourceInfos[uSubTexIndex];
                const auto& subResourceData = data[uSubTexIndex];
                ++uSubTexIndex;

                if (texDesc.eDimension == TextureDimensionType::Texture2D)
                {
                    if (subResourceData.uMemByteRowPitch == 0)
                    {
                        memcpy(pStagingMemory, subResourceData.pMemData, iter.uByteFullSize);
                    }
                    else
                    {
                        uint8_t* pSrcMem = (uint8_t*)subResourceData.pMemData;

                        for (uint32_t y = 0; y < iter.uSubTexBlockHeight; ++y)
                        {
                            uint8_t* pDstRowMem = pStagingMemory + y * iter.uByteRowPitch;
                            uint8_t* pSrcRowMem = pSrcMem + y * subResourceData.uMemByteRowPitch;

                            memcpy(pDstRowMem, pSrcRowMem, iter.uByteRowPitch);
                        }
                    }
                }
                else if (texDesc.eDimension == TextureDimensionType::Texture3D)
                {
                    if (subResourceData.uMemByteRowPitch == 0 && subResourceData.uMemByteDepthPitch == 0)
                    {
                        memcpy(pStagingMemory, subResourceData.pMemData, iter.uByteFullSize);
                    }
                    else
                    {
                        CHECK_ASSERT(subResourceData.uMemByteRowPitch > 0);
                        CHECK_ASSERT(subResourceData.uMemByteDepthPitch > 0);

                        for (uint32_t d = 0; d < iter.uSubTexDepth; ++d)
                        {
                            uint8_t* pDstMem = pStagingMemory + d * iter.uByteSlice;
                            uint8_t* pSrcMem = (uint8_t*)subResourceData.pMemData + d * subResourceData.uMemByteDepthPitch;

                            for (uint32_t y = 0; y < iter.uSubTexBlockHeight; ++y)
                            {
                                uint8_t* pDstRowMem = pDstMem + y * iter.uByteRowPitch;
                                uint8_t* pSrcRowMem = pSrcMem + y * subResourceData.uMemByteRowPitch;

                                memcpy(pDstRowMem, pSrcRowMem, iter.uByteRowPitch);
                            }
                        }
                    }
                }
                else
                {
                    memcpy(pStagingMemory, subResourceData.pMemData, iter.uByteFullSize);
                }


                VkBufferImageCopy copyRegion;
                copyRegion.bufferOffset = uStagingMemOffset;
                copyRegion.bufferRowLength = 0;
                copyRegion.bufferImageHeight = 0;
                copyRegion.imageSubresource.baseArrayLayer = uArr;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageSubresource.mipLevel = uMip;
                copyRegion.imageSubresource.aspectMask = pkvkTexture->GetAspectFlags();
                copyRegion.imageOffset.x = 0;
                copyRegion.imageOffset.y = 0;
                copyRegion.imageOffset.z = 0;
                copyRegion.imageExtent.width = iter.uSubTexWidth;
                copyRegion.imageExtent.height = iter.uSubTexHeight;
                copyRegion.imageExtent.depth = iter.uSubTexDepth;

                copyRegions.push_back(copyRegion);

                uStagingMemOffset += iter.uByteFullSize;
                pStagingMemory += iter.uByteFullSize;
            }
        }

        CHECK_ASSERT(uStagingMemOffset == uStagingSize);
        StagingBuffer->Unmap();

        pRenderCtx->Transition({ pkvkTexture, gfx::KGfxAccess::Unknown, gfx::KGfxAccess::CopyDst });

        vks::vkCmdCopyBufferToImage(
            pRenderCtx->GetCommandBufferVk(),
            StagingBuffer->GetVkBufferHandle(),
            pkvkTexture->GetVkHandle(),
            VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            (uint32_t)copyRegions.size(),
            copyRegions.data()
        );

        m_pStagingMgr->FreeBuffer(pRenderCtx->GetVulkanCommandBuffer(), StagingBuffer);
    Exit0:
        return;
    }

    BOOL KVulkanGraphicDevice::Init(const gfx::RenderSystemInfo& renderSysteInfo)
    {
        BOOL           bRet = false;
        BOOL           bRetCode = false;


        KG_PROCESS_SUCCESS(m_bInited);
        m_pGlobalDynamicBufferRing = nullptr;
        bRetCode = CreateVulkanDevice(renderSysteInfo, m_Physicallimits);
        if (!bRetCode)
        {
#ifdef _WIN32
            if (!renderSysteInfo.bByShaderBuilderCmdTools && renderSysteInfo.bMessageBox)
            {
                ::MessageBox(nullptr, "Vulkan初始化失败，请升级显卡驱动，或联系客服咨询", "错误", MB_OK);
            }
#endif
            if (!renderSysteInfo.bByShaderBuilderCmdTools)
            {
                const char* pGpuName = "GPU(未知)";
                if (DrvOption::szGPU && DrvOption::szGPU[0])
                {
                    pGpuName = DrvOption::szGPU;
                }
                KGLogPrintf(KGLOG_ERR, "设备 %s Vulkan 初始化失败，请升级显卡驱动，或联系客服咨询", pGpuName);
            }
        }

        DrvOption::bSupportSampledImageGreaterThan16 = (m_Physicallimits.maxPerStageDescriptorSampledImages > 16) ? TRUE : FALSE;

        if (!renderSysteInfo.bByShaderBuilderCmdTools)
        {
            KGLOG_ASSERT_EXIT(bRetCode);
        }
        vks::tools::ShaderLoaderInit();

        KG_PROCESS_ERROR(!renderSysteInfo.bByShaderBuilderCmdTools);
        KGLOG_PROCESS_ERROR(bRetCode);

        m_pStagingMgr = new KVulkanStagingManager(this);

        m_pUploadCmdBufferMgr = new KVulkanUploadCmdBufferManager;
        m_pUploadCmdBufferMgr->Init();

        bRetCode = ReCreateDynamicBufferPool();
        KGLOG_PROCESS_ERROR(bRetCode);

        {
            auto pRenderCtx = gfx::GetRenderContext();
            CHECK_ASSERT(pRenderCtx);

            pRenderCtx->BeginCommandBuffer();
        }

        if (IS_BINDLESS_ENABLED)
        {
            m_pVulkanBindlessManager = new KVulkanBindlessManager();
            bRetCode = m_pVulkanBindlessManager->Init_InRenderThread();
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        m_bInited = true;
    Exit1:
        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::ReCreateDynamicBufferPool()
    {
        if (m_pGlobalDynamicBufferRing)
        {
            //m_pGlobalDynamicBufferRing->Destroy();
            //SAFE_DELETE(m_pGlobalDynamicBufferRing);

            auto piDevice = gfx::KGFX_GetGraphicDeviceVKInternal();
            CHECK_ASSERT(piDevice);
            piDevice->GC_DelayReleaseObject(m_pGlobalDynamicBufferRing, [&]() {m_pGlobalDynamicBufferRing->Destroy();});
        }
        //自动扩容机制
        m_uMultiple ++;
        ASSERT(m_uMultiple < 5); //如果很大倍数了，应该是出了啥问题吧？
        const int      maxSwapBufferCount = 3;
        const uint32_t constantBuffersMemSize = maxSwapBufferCount * 8 * 1024 * 1024 * m_uMultiple;
        //const uint32_t constantBuffersMemSize = maxSwapBufferCount * 1 * 1024  * 200 * m_uMultiple;
        m_pGlobalDynamicBufferRing = new KDynamicBufferRing();
        VkResult  res = m_pGlobalDynamicBufferRing->Create(GetVulkanDevice(), maxSwapBufferCount, constantBuffersMemSize, "DynamicUniforms");
        ASSERT(res == VK_SUCCESS);
        return res == VK_SUCCESS;
    }

    IKGFX_Buffer* KVulkanGraphicDevice::CreateDynamicBuffer(uint32_t uSize, gfx::BufferUsageFlags uUsageFlags /* = gfx::BUFFER_USAGE_UNIFORM_BUFFER_BIT*/, BOOL bShareMode)
    {
        PROF_CPU();
        ASSERT(uSize != 0);
        KVulkanDynamicBuffer* pBuffer = new KVulkanDynamicBuffer(uSize, uUsageFlags, bShareMode);
        return pBuffer;
    }

    int KVulkanGraphicDevice::GetDynamicBufferCount()
    {
        if (m_pGlobalDynamicBufferRing)
        {
            return m_pGlobalDynamicBufferRing->GetAllocCount();
        }

        return 0;
    }

    void KVulkanGraphicDevice::ClearUploadCommandNotifyList()
    {
        if (m_pUploadCmdBufferMgr)
        {
            m_pUploadCmdBufferMgr->ClearNotifyList();
        }
    }

    void KVulkanGraphicDevice::TrimCommandPool()
    {
        PROF_CPU();

        KEngineSwitchOption* pEngineSwitch = NSEngine::GetEngineSwitchOptions();
        if (pEngineSwitch && pEngineSwitch->GetEnableTrimCommandPool())
        {
            vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
            if (pVulkanDevice)
            {
                pVulkanDevice->TrimCommandPool();
            }

            if (m_pUploadCmdBufferMgr)
            {
                m_pUploadCmdBufferMgr->TrimCommandPool();
            }
        }
    }

    void KVulkanGraphicDevice::Uninit()
    {
        if (m_bInited)
        {
            for (int32_t i = (int32_t)m_SubmittedPrimaryCommandBuffers.size() - 1; i >= 0; --i)
            {
                KVulkanCommandBuffer*& iter = m_SubmittedPrimaryCommandBuffers[i];
                iter->Release();
            }
            m_SubmittedPrimaryCommandBuffers.clear();

            DeviceWaitIdle();
            if (m_pVulkanBindlessManager)
            {
                m_pVulkanBindlessManager->Uninit();
                SAFE_DELETE(m_pVulkanBindlessManager);
            }

            if (m_pGlobalDynamicBufferRing)
            {
                m_pGlobalDynamicBufferRing->Destroy();
                SAFE_DELETE(m_pGlobalDynamicBufferRing);
            }

            if (m_pUploadCmdBufferMgr)
            {
                m_pUploadCmdBufferMgr->Uninit();
                SAFE_DELETE(m_pUploadCmdBufferMgr);
            }

            if (m_pStagingMgr)
            {
                m_pStagingMgr->Uninit();
                SAFE_DELETE(m_pStagingMgr);
            }

            for (auto it : m_mapStateKey2Sampler)
            {
                gfx::KVulkanSampler* pSampler = (gfx::KVulkanSampler*)it.second;
                ASSERT(pSampler);
                pSampler->Destroy();
                SAFE_DELETE(pSampler);
            }
            m_mapStateKey2Sampler.clear();

            KGraphicDevice::Uninit();

            RenderPassMap& r = gRenderPassMap;
            for (auto itt : r)
            {
                KVulkanRenderPass* pRenderPass = itt.second;
                SAFE_DELETE(pRenderPass);
            }

            for (auto it : gFrameBufferMap)
            {
                FrameBufferMap& r = it.second;
                for (auto itt : r)
                {
                    KVulkanRenderFrameBuffer* pRenderFrameBuffer = itt.second;
                    // pRenderFrameBuffer->Destroy();
                    // SAFE_DELETE(pRenderFrameBuffer);
                    SAFE_RELEASE(pRenderFrameBuffer);
                }
            }
            gFrameBufferMap.clear();
            vks::tools::ShaderLoaderUnInit();

            m_bInited = false;
        }
    }

    void KVulkanGraphicDevice::Reset()
    {
        PROF_CPU();
        DeviceWaitIdle();

        if (m_pUploadCmdBufferMgr)
            m_pUploadCmdBufferMgr->Reset();
    }

    void KVulkanGraphicDevice::_CmdExecuteCommands(gfx::enumGraphicContext eGraphicContext, KVulkanCommandBuffer* pPrimaryCommand, KVulkanCommandBuffer* pSecondCommands[], uint32_t uCount)
    {
        PROF_CPU_DETAIL();
        ASSERT(IsMainThread());
        KVulkanCommandBuffer* pkPrimayCmd = pPrimaryCommand;

        ASSERT(uCount > 0);
        KGLOG_ASSERT_EXIT(pkPrimayCmd);
        KGLOG_ASSERT_EXIT(pkPrimayCmd->m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        if (uCount == 1)
        {
            KVulkanCommandBuffer* pCmd = pSecondCommands[0];
            KGLOG_ASSERT_EXIT(pCmd->m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_SECONDARY);

            VkCommandBuffer pBuffer = pCmd->GetCommandBuffer();

            vks::vkCmdExecuteCommands(pkPrimayCmd->GetCommandBuffer(), 1, &pBuffer);

            AddIndexDrawCount(eGraphicContext, pCmd->m_uDrawIndexCount, pCmd->m_uDrawPointCount);
            AddIndirectDrawCount(eGraphicContext, pCmd->m_uRedirectIndexCount);

            if (pCmd->m_uRecordFrameId != pCmd->m_uCommitRecordFrameId)
            {
                AddUnBakedDrawCall(eGraphicContext, pCmd->m_uDrawCallCount);
                pCmd->m_uCommitRecordFrameId = pCmd->m_uRecordFrameId;
            }
            else
            {
                AddBakedDrawCall(eGraphicContext, pCmd->m_uDrawCallCount);
            }
            pCmd->ClearCounter();

            pCmd->OnSCBExecute(pkPrimayCmd);
            pCmd->AddRef();
            pkPrimayCmd->m_SubmittedSCBs.push_back(pCmd);
            pPrimaryCommand->m_uCmmitCode = 1;
        }
        else if (uCount > 1)
        {
            m_vecSecondbuffers.clear();
            m_vecSecondbuffers.resize(uCount);

            // validate cmd buffer.
            for (uint32_t i = 0; i < uCount; ++i)
            {
                KVulkanCommandBuffer* pCmd = pSecondCommands[i];
                VkCommandBuffer       pBuffer = pCmd->GetCommandBuffer();

                KGLOG_ASSERT_EXIT(pCmd->m_commandLevel == VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_SECONDARY);
                m_vecSecondbuffers[i] = pBuffer;
            }

            for (uint32_t i = 0; i < uCount; ++i)
            {
                KVulkanCommandBuffer* pCmd = pSecondCommands[i];

                AddIndexDrawCount(eGraphicContext, pCmd->m_uDrawIndexCount, pCmd->m_uDrawPointCount);
                AddIndirectDrawCount(eGraphicContext, pCmd->m_uRedirectIndexCount);

                if (pCmd->m_uRecordFrameId != pCmd->m_uCommitRecordFrameId)
                {
                    AddUnBakedDrawCall(eGraphicContext, pCmd->m_uDrawCallCount);
                    pCmd->m_uCommitRecordFrameId = pCmd->m_uRecordFrameId;
                }
                else
                {
                    AddBakedDrawCall(eGraphicContext, pCmd->m_uDrawCallCount);
                }

                pCmd->OnSCBExecute(pkPrimayCmd);
            }

            vks::vkCmdExecuteCommands(pkPrimayCmd->GetCommandBuffer(), uCount, m_vecSecondbuffers.data());
            pPrimaryCommand->m_uCmmitCode = 1;
        }

    Exit0:
        return;
    }

    void KVulkanGraphicDevice::CmdExecuteCommands(gfx::enumGraphicContext eGraphicContext, KVulkanCommandBuffer* pPrimaryCommand, KVulkanCommandBuffer* pSecondCommands[], uint32_t uCount, BOOL bCheckCurFrameBuffer /* = TRUE*/)
    {
        PROF_CPU_DETAIL();
        _CmdExecuteCommands(eGraphicContext, pPrimaryCommand, pSecondCommands, uCount);
    }

    BOOL KVulkanGraphicDevice::IsDirtyDescriptorPool(KVulkanDescriptorPool* pPool)
    {
        return KVulkanDescriptorPool::IsDirtyDescriptorPool(pPool);
    }

    BOOL KVulkanGraphicDevice::AcquireNextImage(const KVulkanSwapChain* pSwapChain, KVulkanSemaphore* pSemaphore, KVulkanFence* pFence, uint32_t* imageIndex)
    {
        PROF_CPU();
        BOOL     bResult = false;
        BOOL     bRetCode = false;
        VkResult vkRetCode = VK_INCOMPLETE;

        // Acquire the next image from the swap chain
        vkRetCode = ((KVulkanSwapChain *)pSwapChain)->AcquireNextImage(pSemaphore, pFence, imageIndex);

        // Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)
        if (vkRetCode != VK_SUCCESS)
        {
            int x = 0;
        }
        KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS);
        // if ((vkRetCode == VK_ERROR_OUT_OF_DATE_KHR) || (vkRetCode == VK_SUBOPTIMAL_KHR))
        //{
        //	goto Exit0;
        // }
        // else
        //{
        //	KGLOG_COM_PROCESS_ERROR(vkRetCode);
        // }

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanGraphicDevice::QueueSubmit(const KVulkanGfxQueue* pQueue, KVulkanCommandBuffer* pCmdBuffer, BOOL bWait, KVulkanSemaphore* pSignalSemaphore)
    {
        PROF_CPU();
        BOOL                  bResult = false;
        BOOL                  bRetCode = false;
        KVulkanCommandBuffer* pGfxCmdBuffer = pCmdBuffer;

        KGLOG_ASSERT_EXIT(!pGfxCmdBuffer->IsManagedUploadingUsage());

        m_pUploadCmdBufferMgr->SubmitUploadCmdBuffer(TRUE);
        m_pUploadCmdBufferMgr->DependOnUploadSemaphores(pGfxCmdBuffer);

        bRetCode = QueueSubmitInternal(pQueue, pGfxCmdBuffer, bWait, pSignalSemaphore);
        KGLOG_PROCESS_ERROR(bRetCode);

        pGfxCmdBuffer->AddRef();
        m_SubmittedPrimaryCommandBuffers.push_back(pGfxCmdBuffer);
        // printf("[XXXXXXXXX] QueueSubmit cmbBuffer:%p \n", pGfxCmdBuffer->GetCommandBuffer());
        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanGraphicDevice::FlushUploadCmd()
    {
        m_pUploadCmdBufferMgr->SubmitUploadCmdBuffer(FALSE);
        m_pUploadCmdBufferMgr->FreeUnusedCmdBuffers(TRUE);
        return TRUE;
    }

    BOOL KVulkanGraphicDevice::QueuePresent(const KVulkanGfxQueue* pQueueProxy, const KVulkanSwapChain* pSwapChain, uint32_t nSwapChainImageIndex, int nWaitSemaphoreCount, KVulkanSemaphore** ppWaitSemaphore)
    {
        OPTICK_ELAPSE_EVENT();
        BOOL     bRet = FALSE;
        VkResult vkResult = VK_INCOMPLETE;

        VkDevice pDevice = GetVkDevice();
        VkQueue  pQueue = pQueueProxy->GetQueue();

        ASSERT(pDevice != VK_NULL_HANDLE);
        ASSERT(pQueue != VK_NULL_HANDLE);

        KGLOG_PROCESS_ERROR(pDevice);
        KGLOG_PROCESS_ERROR(pQueue);

        if (nWaitSemaphoreCount > 0)
        {
            KGLOG_PROCESS_ERROR(ppWaitSemaphore);
        }

        if (nWaitSemaphoreCount == 1)
        {
            VkSemaphore waitSem[] = { ppWaitSemaphore[0]->GetSemaphore() };

            VkPresentInfoKHR present_info = {};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.pNext = NULL;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &pSwapChain->m_pSwapChain;
            present_info.pImageIndices = &nSwapChainImageIndex;
            // Check if a wait semaphore has been specified to wait for before presenting the image

            present_info.pWaitSemaphores = waitSem;
            present_info.waitSemaphoreCount = 1;
            OPTICK_GPU_FLIP(&((KVulkanSwapChain*)(pSwapChain))->m_pSwapChain);

            vkResult = vks::vkQueuePresentKHR(pQueue, &present_info);
            if (vkResult == VK_ERROR_DEVICE_LOST)
            {
                KGLogPrintf(KGLOG_ERR, "完蛋，GPU设备移除了");
#ifdef _WIN32
                MessageBox(NULL, "GPU设备移除了", "错误", MB_OK);
#endif
                RaiseDumpException();
            }
            // if (vkResult != VK_SUCCESS)
            //{
            //	KGLogPrintf(KGLOG_ERR, "vks::vkQueuePresentKHR vkResult:%d", vkResult);
            // }
            // KGLOG_PROCESS_ERROR(vkResult == VK_SUCCESS);
        }
        else
        {
            std::vector<VkSemaphore> waitSem;
            waitSem.reserve(nWaitSemaphoreCount);
            for (int i = 0; i < nWaitSemaphoreCount; i++)
            {
                waitSem.push_back(ppWaitSemaphore[i]->GetSemaphore());
            }

            VkPresentInfoKHR present_info = {};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.pNext = NULL;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &pSwapChain->m_pSwapChain;
            present_info.pImageIndices = &nSwapChainImageIndex;
            // Check if a wait semaphore has been specified to wait for before presenting the image
            if (waitSem.size() > 0)
            {
                present_info.pWaitSemaphores = waitSem.data();
                present_info.waitSemaphoreCount = (uint32_t)waitSem.size();
            }
            OPTICK_GPU_FLIP(&((KVulkanSwapChain*)(pSwapChain))->m_pSwapChain);

            vkResult = vks::vkQueuePresentKHR(pQueue, &present_info);
            if (vkResult == VK_ERROR_DEVICE_LOST)
            {
                KGLogPrintf(KGLOG_ERR, "完蛋，GPU设备移除了");
#ifdef _WIN32
                MessageBox(NULL, "GPU设备移除了", "错误", MB_OK);
#endif
                RaiseDumpException();
            }
            // if (vkResult != VK_SUCCESS)
            //{
            //	KGLogPrintf(KGLOG_ERR, "vks::vkQueuePresentKHR vkResult:%d", vkResult);
            // }
            // KGLOG_PROCESS_ERROR(vkResult == VK_SUCCESS);
        }
        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::WaitForFence(int nFenceCount, KVulkanFence** ppFence, BOOL bWaitAll, uint64_t timeout)
    {
        PROF_CPU();
        BOOL     bRet = FALSE;
        VkResult vkResult = VK_INCOMPLETE;
        VkDevice pDevice = GetVkDevice();

        std::vector<VkFence> fences;

        KGLOG_PROCESS_ERROR(pDevice != VK_NULL_HANDLE);
        KGLOG_PROCESS_ERROR(nFenceCount > 0);
        KGLOG_PROCESS_ERROR(ppFence);

        for (int i = 0; i < nFenceCount; i++)
        {
            auto kVkFence = ppFence[i];
            if (!kVkFence)
                continue;

            KGLOG_PROCESS_ERROR(kVkFence->IsSubmitted());
            fences.push_back(kVkFence->GetFence());
        }

        if (!fences.empty())
        {
            vkResult = vks::vkWaitForFences(pDevice, (uint32_t)fences.size(), fences.data(), bWaitAll, timeout);
            if (vkResult != VK_SUCCESS)
            {
                KGLogPrintf(KGLOG_WARNING, "Warning：vkWaitForFences not success %d", vkResult);

                if (vkResult == VK_ERROR_DEVICE_LOST)
                {
                    KGLogPrintf(KGLOG_ERR, "Fatal，GPU Device Lost");
#ifdef _WIN32
                    MessageBox(NULL, "GPU Device Lost", "Error", MB_OK);
#endif
                    RaiseDumpException();
                }
            }
            KGLOG_ASSERT_EXIT(vkResult == VK_SUCCESS);
        }

        for (int i = 0; i < nFenceCount; i++)
        {
            auto iter = ppFence[i];
            iter->Query();
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::ResetFences(uint32_t uFenceCount, KVulkanFence* pFencs[])
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        VkDevice pDevice = GetVkDevice();

        KGLOG_PROCESS_ERROR(pDevice != VK_NULL_HANDLE);
        KGLOG_PROCESS_ERROR(uFenceCount > 0);
        KGLOG_PROCESS_ERROR(pFencs);

        for (uint32_t i = 0; i < uFenceCount; i++)
        {
            pFencs[i]->Reset();
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::GetFenceStatus(KVulkanFence* pFence, FenceStatus* pFenceStatus)
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        VkDevice pDevice = GetVkDevice();
        VkFence pvkFence = VK_NULL_HANDLE;

        KGLOG_PROCESS_ERROR(pDevice);
        KGLOG_PROCESS_ERROR(pFence);

        pvkFence = pFence->GetFence();
        KGLOG_PROCESS_ERROR(pvkFence);

        *pFenceStatus = FENCE_STATUS_COMPLETE;

        if (pFence->IsSubmitted())
        {
            VkResult vkRes = vks::vkGetFenceStatus(pDevice, pvkFence);
            *pFenceStatus = vkRes == VK_SUCCESS ? FENCE_STATUS_COMPLETE : FENCE_STATUS_INCOMPLETE;
        }
        else
        {
            *pFenceStatus = FENCE_STATUS_NOTSUBMITTED;
        }

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::BeginCommandBuffer(const KVulkanCommandBuffer* pCommandBuffer, KVulkanRenderFrameBuffer* pFrameBuffer_WhenIsSecondCommandBufferNeeded)
    {
        OPTICK_ELAPSE_EVENT();
        BOOL                  bRet = false;
        BOOL                  bRetCode = false;
        KVulkanCommandBuffer* pCmdBuffer = (KVulkanCommandBuffer*)pCommandBuffer;

        bRetCode = pCmdBuffer->Reset(true);
        KGLOG_ASSERT_EXIT(bRetCode);

        bRetCode = pCmdBuffer->Begin(pFrameBuffer_WhenIsSecondCommandBufferNeeded);
        KGLOG_ASSERT_EXIT(bRetCode);

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::EndCommandBuffer(KVulkanCommandBuffer* pCommandBuffer, std::function<void()> pfunBeforeEndCall)
    {
        OPTICK_ELAPSE_EVENT();
        BOOL bRet = false;

        KVulkanCommandBuffer* pCmdBuffer = (KVulkanCommandBuffer*)pCommandBuffer;

        BOOL bRetCode = pCmdBuffer->End(pfunBeforeEndCall);
        KGLOG_ASSERT_EXIT(bRetCode);

        bRet = true;
    Exit0:
        return bRet;
    }

    uint32_t KVulkanGraphicDevice::GetQueueFamilyIndex(enumForProcessType commandType)
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        uint32_t            ret = 0;
        switch (commandType)
        {
        case FOR_GRPAHIC:
            ret = pVulkanDevice->m_QueueFamilyIndices.graphics;
            break;
        case FOR_COMPUTE:
            ret = pVulkanDevice->m_QueueFamilyIndices.compute;
            break;
        case FOR_TRANSFER:
            ret = pVulkanDevice->m_QueueFamilyIndices.transfer;
            break;
        default:
            break;
        }
        return ret;
    }

    void KVulkanGraphicDevice::GetTimestampFrequency(double* pFrequency)
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();

        VkPhysicalDeviceProperties vkPhyProp = pVulkanDevice->GetPhysicalDeviceProperties();

        *pFrequency = 1.0f / ((double)vkPhyProp.limits.timestampPeriod /*ns/tick number of nanoseconds required for a timestamp query to be incremented by 1*/
            * 1e-9);                                 // convert to ticks/sec (DX12 standard)
    }

    VkQueryType GetQueryType(KQueryType type)
    {
        switch (type)
        {
        case QUERY_TYPE_TIMESTAMP:
            return VK_QUERY_TYPE_TIMESTAMP;
        case QUERY_TYPE_PIPELINE_STATISTICS:
            return VK_QUERY_TYPE_PIPELINE_STATISTICS;
        case QUERY_TYPE_OCCLUSION:
            return VK_QUERY_TYPE_OCCLUSION;
        default:
            ASSERT(false && "Invalid query heap type");
            return VK_QUERY_TYPE_MAX_ENUM;
        }
    }


    void KVulkanGraphicDevice::CreateQueryHeap(const KQueryHeapDesc* pDesc, KQueryHeap** ppQueryHeap)
    {
        VkDevice pDevice = GetVkDevice();

        KVulkanQueryHeap* pVulkanQueryHeap = new KVulkanQueryHeap;

        VkQueryPoolCreateInfo createInfo = vks::initializers::QueryPoolCreateInfo();
        createInfo.queryCount = pDesc->uQueryCount;
        createInfo.queryType = GetQueryType(pDesc->eType);
        vks::vkCreateQueryPool(pDevice, &createInfo, NULL, &pVulkanQueryHeap->pVkQueryPool);

        *ppQueryHeap = pVulkanQueryHeap;
    }

    void KVulkanGraphicDevice::DestroyQueryHeap(KQueryHeap* pQueryHeap)
    {
        if (pQueryHeap)
        {
            VkDevice pDevice = GetVkDevice();

            KVulkanQueryHeap* pVulkanQueryHeap = (KVulkanQueryHeap*)pQueryHeap;
            vks::vkDestroyQueryPool(pDevice, pVulkanQueryHeap->pVkQueryPool, NULL);
            SAFE_DELETE(pVulkanQueryHeap);
        }
    }

    void KVulkanGraphicDevice::CmdBeginQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, KQueryDesc* pQuery)
    {
        KVulkanCommandBuffer* pCommandBuffer = pCmd;
        KVulkanQueryHeap* pVulkanQueryHeap = (KVulkanQueryHeap*)pQueryHeap;
        KQueryType            type = pVulkanQueryHeap->desc.eType;
        switch (type)
        {
        case QUERY_TYPE_TIMESTAMP:
            vks::vkCmdWriteTimestamp(pCommandBuffer->GetCommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pVulkanQueryHeap->pVkQueryPool, pQuery->uIndex);
            break;
        case QUERY_TYPE_PIPELINE_STATISTICS:
            break;
        case QUERY_TYPE_OCCLUSION:
            break;
        default:
            break;
        }
    }

    void KVulkanGraphicDevice::CmdEndQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, KQueryDesc* pQuery)
    {
        KVulkanCommandBuffer* pCommandBuffer = pCmd;
        KVulkanQueryHeap* pVulkanQueryHeap = (KVulkanQueryHeap*)pQueryHeap;
        KQueryType            type = pVulkanQueryHeap->desc.eType;
        switch (type)
        {
        case QUERY_TYPE_TIMESTAMP:
            vks::vkCmdWriteTimestamp(pCommandBuffer->GetCommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pVulkanQueryHeap->pVkQueryPool, pQuery->uIndex);
            break;
        case QUERY_TYPE_PIPELINE_STATISTICS:
            break;
        case QUERY_TYPE_OCCLUSION:
            break;
        default:
            break;
        }
    }

    void KVulkanGraphicDevice::CmdResolveQuery(KVulkanCommandBuffer* pCmd, KQueryHeap* pQueryHeap, IKGFX_Buffer* pReadbackBufferProxy, uint32_t startQuery, uint32_t queryCount)
    {
        KVulkanCommandBuffer* pCommandBuffer = pCmd;
        KVulkanQueryHeap* pVulkanQueryHeap = (KVulkanQueryHeap*)pQueryHeap;
        KVulkanBuffer* pReadbackBuffer = (KVulkanBuffer*)pReadbackBufferProxy;


        vks::vkCmdCopyQueryPoolResults(pCommandBuffer->GetCommandBuffer(), pVulkanQueryHeap->pVkQueryPool, startQuery, queryCount, pReadbackBuffer->GetVkBuffer(), 0, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    }

    void KVulkanGraphicDevice::DeviceWaitIdle()
    {
        VkDevice pvkDevice = GetVkDevice();
        if (pvkDevice)
            vks::vkDeviceWaitIdle(pvkDevice);
    }

    void KVulkanGraphicDevice::QueueWaitIdle(enumForProcessType commandType)
    {
        PROF_CPU_DETAIL("VKQueueWaitIdle");
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        VkQueue             pQueue = nullptr;
        switch (commandType)
        {
        case gfx::FOR_GRPAHIC:
            pQueue = ::GetGraphicQueue();
            break;
        case gfx::FOR_COMPUTE:
            pQueue = ::GetComputeQueue();
            break;
        case gfx::FOR_TRANSFER:
            pQueue = ::GetTransferQueue();
            break;
        default:
            break;
        }
        if (pQueue)
        {
            vks::vkQueueWaitIdle(pQueue);
        }
    }

    BOOL KVulkanGraphicDevice::CreateGraphicQueue(KVulkanGfxQueue** ppQueue)
    {
        BOOL bRet = FALSE;

        VkQueue          pQueue = ::GetGraphicQueue();
        KVulkanGfxQueue* pGfxQueue = new KVulkanGfxQueue;
        pGfxQueue->SetQueue(pQueue);
        pGfxQueue->SetQueueType(gfx::FOR_GRPAHIC);
        *ppQueue = pGfxQueue;

        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::CreateComputeQueue(KVulkanGfxQueue** ppQueue)
    {
        BOOL bRet = FALSE;

        VkQueue          pQueue = ::GetComputeQueue();
        KVulkanGfxQueue* pGfxQueue = new KVulkanGfxQueue;
        pGfxQueue->SetQueue(pQueue);
        pGfxQueue->SetQueueType(gfx::FOR_COMPUTE);
        *ppQueue = pGfxQueue;

        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicDevice::CreateTransferQueue(KVulkanGfxQueue** ppQueue)
    {
        BOOL bRet = FALSE;

        VkQueue          pQueue = ::GetTransferQueue();
        KVulkanGfxQueue* pGfxQueue = new KVulkanGfxQueue;
        pGfxQueue->SetQueue(pQueue);
        pGfxQueue->SetQueueType(gfx::FOR_TRANSFER);
        *ppQueue = pGfxQueue;

        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    gfx::IKGFX_Sampler* KVulkanGraphicDevice::GetSamplerByState(gfx::KSamplerState* pSamplerState)
    {
        BOOL                bRetCode = FALSE;        
        gfx::KVulkanSampler* pRetSampler = nullptr;

        KGLOG_ASSERT_EXIT(&pSamplerState);

        {
            std::lock_guard<decltype(m_mtxMapStateKey2Sampler)> _lock(m_mtxMapStateKey2Sampler);

            const_pool_str strStateKey = pSamplerState->GetKey();
            auto              itFind = m_mapStateKey2Sampler.find(strStateKey);
            if (itFind != m_mapStateKey2Sampler.end())
            {
                return itFind->second;
            }

            // Create New
            pRetSampler = new gfx::KVulkanSampler();
            ASSERT(!pSamplerState->bNeedShaderInit);
            bRetCode = ((gfx::KVulkanSampler*)pRetSampler)->Create(pSamplerState);
            KGLOG_ASSERT_EXIT(bRetCode);

            m_mapStateKey2Sampler.insert(std::make_pair(strStateKey, pRetSampler));
        }

    Exit0:
        if (!bRetCode && pRetSampler)
        {
            ((gfx::KVulkanSampler*)pRetSampler)->Destroy();
            SAFE_DELETE(pRetSampler);
        }
        return pRetSampler;
    }


    uint32_t KVulkanGraphicDevice::GetMinUniformBufferOffsetAlignment()
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        return pVulkanDevice->GetMinUniformBufferOffsetAlignment();
    }

    BOOL KVulkanGraphicDevice::IsSubGoupQuadSupported() const
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        return pVulkanDevice->IsSubGoupQuadSupported();
    }

    BOOL KVulkanGraphicDevice::IsSubGoupF16Supported() const
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        return pVulkanDevice->IsSubGoupF16Supported();
    }

    BOOL KVulkanGraphicDevice::IsFp16Supported() const
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        return pVulkanDevice->IsFp16Supported();
    }

    BOOL KVulkanGraphicDevice::IsAtomicUint64Supported() const
    {
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        return pVulkanDevice->IsAtomicUint64Supported();
    }


    void KVulkanGraphicDevice::BeginRegion(gfx::KVulkanCommandBuffer* cmdbuffer, const char* pMarkerName, NSKMath::KVec4& color)
    {
        if (vks::debugmarker::active)
        {
            gfx::KVulkanCommandBuffer* pCmd = (gfx::KVulkanCommandBuffer*)cmdbuffer;
            vks::debugmarker::_BeginRegion(pCmd->GetCommandBuffer(), pMarkerName, color);
        }
    }

    // Insert a new debug marker into the command buffer
    void KVulkanGraphicDevice::Insert(gfx::KVulkanCommandBuffer* cmdbuffer, std::string markerName, NSKMath::KVec4& color)
    {
        if (vks::debugmarker::active)
        {
            gfx::KVulkanCommandBuffer* pCmd = (gfx::KVulkanCommandBuffer*)cmdbuffer;
            vks::debugmarker::_Insert(pCmd->GetCommandBuffer(), markerName, color);
        }
    }

    // End the current debug marker region
    void KVulkanGraphicDevice::EndRegion(gfx::KVulkanCommandBuffer* cmdBuffer)
    {
        if (vks::debugmarker::active)
        {
            gfx::KVulkanCommandBuffer* pCmd = (gfx::KVulkanCommandBuffer*)cmdBuffer;
            vks::debugmarker::_EndRegion(pCmd->GetCommandBuffer());
        }
    }

    void KVulkanGraphicDevice::SetCommandBufferName(gfx::KVulkanCommandBuffer* cmdBuffer, const char* szName)
    {
        if (vks::debugmarker::active && szName && strlen(szName) > 0)
        {
            char name[MAX_PATH];
            strncpy(name, szName, MAX_PATH);
            name[MAX_PATH - 1] = '\0';
            KCONV::Convert2Utf8n(name, MAX_PATH);
            gfx::KVulkanCommandBuffer* pCmd = (gfx::KVulkanCommandBuffer*)cmdBuffer;
            vks::debugmarker::SetCommandBufferName(GetVkDevice(), pCmd->GetCommandBuffer(), name);
        }
    }

    void KVulkanGraphicDevice::SetImageName(gfx::KGfxTexture* pTexture, const char* szName)
    {
        if (vks::debugmarker::active && szName && strlen(szName) > 0)
        {
            char name[MAX_PATH];
            strncpy(name, szName, MAX_PATH);
            name[MAX_PATH - 1] = '\0';
            KCONV::Convert2Utf8n(name, MAX_PATH);
            vks::debugmarker::SetImageName(GetVkDevice(), (VkImage)pTexture->GetNativeImageHandle(), name);
        }
    }
    void KVulkanGraphicDevice::SetSamplerName(gfx::IKGFX_Sampler* pSampler, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            KVulkanSampler* pVulkanSampler = (KVulkanSampler*)pSampler;
            vks::debugmarker::SetSamplerName(GetVkDevice(), pVulkanSampler->GetVKSampler(), szName);
        }
    }
    void KVulkanGraphicDevice::SetBufferName(gfx::IKGFX_Buffer* pGfxBuffer, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            KVulkanBuffer* pVulkanGfxBuffer = (KVulkanBuffer*)pGfxBuffer;
            vks::debugmarker::SetBufferName(GetVkDevice(), pVulkanGfxBuffer->GetVkBuffer(), szName);
        }
    }
    void KVulkanGraphicDevice::SetShaderModuleName(gfx::KShaderStage* pShaderStage, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            KVulkanShaderStage* pVulkanShaderStage = (KVulkanShaderStage*)pShaderStage;
            const VkPipelineShaderStageCreateInfo& createInfo = pVulkanShaderStage->GetCreateInfo();
            vks::debugmarker::SetShaderModuleName(GetVkDevice(), createInfo.module, szName);
        }
    }
    void KVulkanGraphicDevice::SetPipelineName(gfx::KPipeline* pPipeline, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            VkPipeline pipeline = (VkPipeline)pPipeline->GetVkPipeline();
            vks::debugmarker::SetPipelineName(GetVkDevice(), pipeline, szName);
        }
    }
    void KVulkanGraphicDevice::SetPipelineLayoutName(gfx::KVulkanLayout* pLayout, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            VkPipelineLayout pPipelineLayout = pLayout->GetPipelineLayout();
            vks::debugmarker::SetPipelineLayoutName(GetVkDevice(), pPipelineLayout, szName);
        }
    }

    void KVulkanGraphicDevice::SetRenderPassName(gfx::KVulkanRenderPass* pRenderPass, const char* szName)
    {
        VkRenderPass pPass = pRenderPass->GetPass();
        vks::debugmarker::SetRenderPassName(GetVkDevice(), pPass, szName);
    }
    void KVulkanGraphicDevice::SetDescriptorSetName(gfx::KVulkanDescriptorSet* pDescriptorSet, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            uint32_t uCount = pDescriptorSet->GetSetCount();
            for (uint32_t i = 0; i < uCount; ++i)
            {
                VkDescriptorSet pVkDescriptorSet = pDescriptorSet->GetDescriptorSet(i);
                vks::debugmarker::SetDescriptorSetName(GetVkDevice(), pVkDescriptorSet, szName);
            }
        }
    }
    void KVulkanGraphicDevice::SetDescriptorSetLayoutName(gfx::KVulkanLayout* pLayout, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            uint32_t uCount = pLayout->GetLayoutSetCount();
            for (uint32_t i = 0; i < uCount; ++i)
            {
                VkDescriptorSetLayout pVkDescriptorSetLayout = pLayout->GetDesriptorSetLayout(i);
                vks::debugmarker::SetDescriptorSetLayoutName(GetVkDevice(), pVkDescriptorSetLayout, szName);
            }
        }
    }
    void KVulkanGraphicDevice::SetSemaphoreName(gfx::KVulkanSemaphore* pSemaphore, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            VkSemaphore pVkSemphone = pSemaphore->GetSemaphore();
            vks::debugmarker::SetSemaphoreName(GetVkDevice(), pVkSemphone, szName);
        }
    }
    void KVulkanGraphicDevice::SetFenceName(gfx::KVulkanFence* pFence, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            VkFence pVkFence = pFence->GetFence();
            vks::debugmarker::SetFenceName(GetVkDevice(), pVkFence, szName);
        }
    }
    void KVulkanGraphicDevice::SetFrameBufferName(gfx::KVulkanRenderFrameBuffer* pRenderFrameBuffer, const char* szName)
    {
        VkFramebuffer pVkFrameBuffer = pRenderFrameBuffer->GetFrameBuffer();
        vks::debugmarker::SetFramebufferName(GetVkDevice(), pVkFrameBuffer, szName);
    }
    void KVulkanGraphicDevice::SetQueueName(gfx::KVulkanGfxQueue* pQueue, const char* szName)
    {
        if (vks::debugmarker::active)
        {
            VkQueue pVkQueque = pQueue->GetQueue();
            vks::debugmarker::SetQueueName(GetVkDevice(), pVkQueque, szName);
        }
    }

    KVulkanCommandBuffer* KVulkanGraphicDevice::GetUploadCommandBuffer()
    {
        return GetUploadCmdBufferInternal();
    }

    KVulkanCommandBuffer* KVulkanGraphicDevice::GetUploadCmdBufferInternal()
    {
        return m_pUploadCmdBufferMgr->GetUploadCmdBuffer();
    }

    void KVulkanGraphicDevice::SubmitCommandBuffer(const KVulkanGfxQueue* pQueue, KVulkanCommandBuffer* pCmdBuffer, BOOL bWait, KVulkanSemaphore* pSignalSemaphore)
    {
        ASSERT(pQueue && pCmdBuffer);

        if (pCmdBuffer)
        {
            QueueSubmitInternal(pQueue, pCmdBuffer, bWait, pSignalSemaphore);
        }
    }

    BOOL KVulkanGraphicDevice::QueueSubmitInternal(
        const KVulkanGfxQueue*  pQueue,
        KVulkanCommandBuffer*   pCmdBuffer,
        BOOL                    bWait,
        KVulkanSemaphore*       pSignalSemaphore
    )
    {
        static uint32_t uLost = 0;
        BOOL            bRet = FALSE;

        ASSERT(pCmdBuffer);
        VkCommandBuffer pvkCmdBuffer = pCmdBuffer->GetCommandBuffer();
        VkFence         pvkFence = pCmdBuffer->m_fence ? pCmdBuffer->m_fence->GetFence() : nullptr;
        VkQueue         pvkQueue = pQueue->GetQueue();

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = NULL;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &pvkCmdBuffer;

        VkSemaphore signalSem = pSignalSemaphore ? pSignalSemaphore->GetSemaphore() : nullptr;

        uint32_t uWaitSemaphoreCount = (uint32_t)pCmdBuffer->m_WaitSemaphores.size();
        if (uWaitSemaphoreCount > 0)
        {
            std::vector<VkSemaphore> WaitSemaphores(pCmdBuffer->m_WaitSemaphores.size());
            for (size_t i = 0; i < pCmdBuffer->m_WaitSemaphores.size(); ++i)
            {
                WaitSemaphores[i] = pCmdBuffer->m_WaitSemaphores[i]->GetSemaphore();
            }

            submitInfo.waitSemaphoreCount = uWaitSemaphoreCount;
            submitInfo.pWaitSemaphores = WaitSemaphores.data();
            submitInfo.pWaitDstStageMask = pCmdBuffer->m_WaitDstStageMasks.data();
            submitInfo.signalSemaphoreCount = signalSem ? 1 : 0;
            submitInfo.pSignalSemaphores = signalSem ? &signalSem : nullptr;

            VkResult vkResult = vks::vkQueueSubmit(pvkQueue, 1, &submitInfo, pvkFence);
            ASSERT(vkResult == VK_SUCCESS);
            if (vkResult == VK_ERROR_DEVICE_LOST)
            {
                KGLogPrintf(KGLOG_ERR, "Fatal，GPU Device Lost");
#ifdef _WIN32
                MessageBox(NULL, "GPU Device Lost", "Error", MB_OK);
#endif
                // if (uLost > 100)
                //{
                RaiseDumpException();
                //}
                ++uLost;
            }
            // if (vkResult != VK_SUCCESS)
            //{
            //	KGLogPrintf(KGLOG_ERR, "vks::vkQueueSubmit vkResult:%d", vkResult);
            // }
            // KGLOG_PROCESS_ERROR(vkResult == VK_SUCCESS);
            KGLOG_COM_PROCESS_ERROR(vkResult);
        }
        else
        {
            submitInfo.waitSemaphoreCount = 0;
            submitInfo.pWaitSemaphores = nullptr;
            submitInfo.pWaitDstStageMask = nullptr;
            submitInfo.signalSemaphoreCount = signalSem ? 1 : 0;
            submitInfo.pSignalSemaphores = signalSem ? &signalSem : nullptr;

            VkResult vkResult = vks::vkQueueSubmit(pvkQueue, 1, &submitInfo, pvkFence);
            ASSERT(vkResult == VK_SUCCESS);
            if (vkResult == VK_ERROR_DEVICE_LOST)
            {
                KGLogPrintf(KGLOG_ERR, "完蛋，GPU设备移除了");
#ifdef _WIN32
                MessageBox(NULL, "GPU设备移除了", "错误", MB_OK);
#endif
                // if (uLost > 100)
                //{
                RaiseDumpException();
                //}
                ++uLost;
            }
            // if (vkResult != VK_SUCCESS)
            //{
            //	KGLogPrintf(KGLOG_ERR, "vks::vkQueueSubmit vkResult:%d", vkResult);
            // }
            //  KGLOG_PROCESS_ERROR(vkResult == VK_SUCCESS);
            KGLOG_COM_PROCESS_ERROR(vkResult);
        }

        ASSERT(pCmdBuffer->m_SubmittedWaitSemaphores.empty());

        std::swap(pCmdBuffer->m_SubmittedWaitSemaphores, pCmdBuffer->m_WaitSemaphores);
        pCmdBuffer->m_WaitSemaphores.clear();
        pCmdBuffer->m_WaitDstStageMasks.clear();

        if (pSignalSemaphore)
        {
            pCmdBuffer->m_pSignalSemaphore = pSignalSemaphore;
            pCmdBuffer->m_pSignalSemaphore->AddRef();
        }

        pCmdBuffer->m_eLifecycleState = KCommandBufferStates::Pending;

        if (pCmdBuffer->m_fence)
        {
            pCmdBuffer->m_fence->Submit();
            pCmdBuffer->m_uSubmittedFenceCounter = pCmdBuffer->m_fence->GetSignalFenceCounter();

            if (bWait)
            {
                KVulkanFence* pkFence = pCmdBuffer->m_fence;
                WaitForFence(1, &pkFence, true, UINT64_MAX);
                ResetFences(1, &pkFence);

                pCmdBuffer->m_eLifecycleState = KCommandBufferStates::Executable;
            }
        }
        bRet = TRUE;
    Exit0:
        return bRet;
    }

    KGFX_PHYSICAL_DEVICE_LIMITS* KVulkanGraphicDevice::GetPhysicalDeviceLimits()
    {
        return &m_Physicallimits;
    }

    TextureFormatInfo KVulkanGraphicDevice::GetTextureFormatInfo(enumTextureFormat eFormat) const
    {
        TextureFormatInfo texFmtInfo;
        auto& kvkTexFmtInfo = GetTextureFormatInfoVk(eFormat);

        texFmtInfo.uBytesPerBlock = kvkTexFmtInfo.uBytesPerBlock;
        texFmtInfo.uWidthPerBlock = kvkTexFmtInfo.uWidthPerBlock;
        texFmtInfo.uHeightPerBlock = kvkTexFmtInfo.uHeightPerBlock;

        return texFmtInfo;
    }

    void KVulkanGraphicDevice::FrameMove(BOOL bFrameRendered)
    {
        for (int32_t i = (int32_t)m_SubmittedPrimaryCommandBuffers.size() - 1; i >= 0; --i)
        {
            KVulkanCommandBuffer*& iter = m_SubmittedPrimaryCommandBuffers[i];
            iter->RefreshPendingState();

            if (iter->m_eLifecycleState != KCommandBufferStates::Pending)
            {
                iter->Release();
                iter = nullptr;

                if (i < (int32_t)m_SubmittedPrimaryCommandBuffers.size() - 1)
                    std::swap(m_SubmittedPrimaryCommandBuffers[i], m_SubmittedPrimaryCommandBuffers.back());

                m_SubmittedPrimaryCommandBuffers.pop_back();
            }
        }

        if (m_pStagingMgr)
        {
            m_pStagingMgr->FrameMove();
        }

        if (m_pUploadCmdBufferMgr)
        {
            m_pUploadCmdBufferMgr->FreeUnusedCmdBuffers();
        }

        if (m_pGlobalDynamicBufferRing)
        {
            m_pGlobalDynamicBufferRing->BeginFrame();
        }

        if (m_pVulkanBindlessManager)
        {
            m_pVulkanBindlessManager->FrameMove();
        }

        KGraphicDevice::FrameMove(bFrameRendered);
    }
}

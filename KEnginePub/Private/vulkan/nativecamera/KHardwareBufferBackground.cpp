//
// Created by Ant on 2024/8/16.
//
#include "KHardwareBufferBackground.h"

#if defined(__ANDROID__)
#include "Engine/KGLog.h"
#include "../KVulkanDevice.h"
#include "../../../../KG3D_Imgui/Vulkan/func/KVulkanFunc.h"
#include "../GFXVulkan.h"
#include <array>
#include "KEngine/Private/core/KEngineCore.h"
#include "../KGraphicDevice.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "optick.h"

BOOL KHardwareCameraBackground::Init()
{
    BOOL                 bResult              = FALSE;
    BOOL                 bRetCode             = FALSE;
    VkDevice             device               = VK_NULL_HANDLE;
    gfx::KGraphicDevice* pGraphicDevice       = nullptr;
    uint32_t             uSwapChainImageCount = g_GetCameraHandler().GetSwapChainImageCount();
    KGLOG_PROCESS_ERROR(uSwapChainImageCount > 0);

    m_vCameraImages.resize(uSwapChainImageCount);
    m_vDescriptorSets.resize(uSwapChainImageCount);
    m_vSecondaryCmdBuffers.resize(uSwapChainImageCount);

    device = GetVkDevice();
    KGLOG_PROCESS_ERROR(device != VK_NULL_HANDLE);

    pGraphicDevice = gfx::GetGraphicDevice();
    KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

    bRetCode = KNativeCameraUtility::CreateCommandPool(pGraphicDevice, gfx::FOR_GRPAHIC, &m_pCommandPool);
    KGLOG_PROCESS_ERROR(bRetCode);

    for (uint32_t i = 0; i < uSwapChainImageCount; i++)
    {
        bRetCode = KNativeCameraUtility::CreateCommandBuffers(gfx::COMMAND_BUFFER_LEVEL_SECONDARY, &m_vSecondaryCmdBuffers[i], gfx::FOR_GRPAHIC, m_pCommandPool);
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    bRetCode = _SetCurrentFrameVerticesAndIndices();
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_SetCurrentFrameVerticesAndIndices()
{
    BOOL bResult  = FALSE;
    BOOL bRetCode = FALSE;

    std::vector<Vertex> vVertices(4);

    if (m_pVertexDescriptor == nullptr)
    {
        vVertices[0] = {-1.0f, -1.0f, 0.0f, 0.0f};
        vVertices[1] = {1.0f, -1.0f, 1.0f, 0.0f};
        vVertices[2] = {1.0f, 1.0f, 1.0f, 1.0f};
        vVertices[3] = {-1.0f, 1.0f, 0.0f, 1.0f};

        bRetCode = _CreateVertexBuffer(vVertices.size(), &m_pVertexBufferDefault, vVertices);
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    if (m_pVertexBufferPortrait == nullptr)
    {
        vVertices[0] = {-1.0f, -1.0f, 0.0f, 1.0f};
        vVertices[1] = {1.0f, -1.0f, 0.0f, 0.0f};
        vVertices[2] = {1.0f, 1.0f, 1.0f, 0.0f};
        vVertices[3] = {-1.0f, 1.0f, 1.0f, 1.0f};

        bRetCode = _CreateVertexBuffer(vVertices.size(), &m_pVertexBufferPortrait, vVertices);
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    if (m_pVertexBufferReverse == nullptr)
    {
        vVertices[0] = {-1.0f, -1.0f, 1.0f, 1.0f};
        vVertices[1] = {1.0f, -1.0f, 0.0f, 1.0f};
        vVertices[2] = {1.0f, 1.0f, 0.0f, 0.0f};
        vVertices[3] = {-1.0f, 1.0f, 1.0f, 0.0f};

        bRetCode = _CreateVertexBuffer(vVertices.size(), &m_pVertexBufferReverse, vVertices);
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    bRetCode = _CreateIndexBuffer(6, &m_pIndexBuffer);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = TRUE;
Exit0:
    if (!bResult)
    {
        _DestroyVertexBuffer();
        _DestroyIndexBuffer();
    }

    return bResult;
}

BOOL KHardwareCameraBackground::_CreateVertexBuffer(size_t vertexSize, gfx::KGfxBuffer** ppVertexBuffer, const std::vector<Vertex>& vertices)
{
    BOOL                 bResult           = FALSE;
    BOOL                 bRetCode          = FALSE;
    size_t               nVertexBufferSize = vertexSize * sizeof(Vertex);
    gfx::KGraphicDevice* pGraphicDevice    = gfx::GetGraphicDevice();

    KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

    if (*ppVertexBuffer == nullptr)
    {
        gfx::KGfxBufferDesc vertexBufferDesc{};
        vertexBufferDesc.eResAccessFlags      = gfx::KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        vertexBufferDesc.uByteWidth           = nVertexBufferSize;
        vertexBufferDesc.uUsageFlags          = gfx::BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vertexBufferDesc.uStructureByteStride = 0;

        bRetCode = pGraphicDevice->CreateBuffer(ppVertexBuffer, vertexBufferDesc, vertices.data());
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_CreateIndexBuffer(size_t indexSize, gfx::KGfxBuffer** ppIndexBuffer)
{
    BOOL                 bResult          = FALSE;
    BOOL                 bRetCode         = FALSE;
    size_t               nIndexBufferSize = indexSize * sizeof(uint16_t);
    gfx::KGraphicDevice* pGraphicDevice   = gfx::GetGraphicDevice();

    KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

    if (*ppIndexBuffer == nullptr)
    {
        gfx::KGfxBufferDesc indexBufferDesc{};
        indexBufferDesc.eResAccessFlags      = gfx::KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        indexBufferDesc.uByteWidth           = nIndexBufferSize;
        indexBufferDesc.uUsageFlags          = gfx::BUFFER_USAGE_INDEX_BUFFER_BIT;
        indexBufferDesc.uStructureByteStride = 0;

        bRetCode = pGraphicDevice->CreateBuffer(ppIndexBuffer, indexBufferDesc, m_vIndices.data());
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::Render(const KCameraRenderParam& pParam)
{
    BOOL     bResult  = FALSE;
    BOOL     bRetCode = FALSE;
    uint32_t uSwapChainIndex{0};

    KG_ASSERT(pParam.pFrameBuffer != nullptr && pParam.pPrimaryCmdBuffer != nullptr && pParam.pRenderPass != nullptr);

    bRetCode = KNativeCameraUtility::GetSwapChainImageIndex(uSwapChainIndex);
    KGLOG_PROCESS_ERROR(bRetCode);

    if (m_pFrameBuffer != pParam.pFrameBuffer || m_pRenderPass != pParam.pRenderPass)
    {
        m_pFrameBuffer = pParam.pFrameBuffer;
        m_pRenderPass  = pParam.pRenderPass;
    }

    m_pPrimaryCmdBuffer = pParam.pPrimaryCmdBuffer;
    KGLOG_PROCESS_ERROR(m_pPrimaryCmdBuffer != nullptr);

    if (m_nWidth != pParam.nWidth || m_nHeight != pParam.nHeight)
    {
        m_nWidth  = pParam.nWidth;
        m_nHeight = pParam.nHeight;
    }

    bRetCode = _RenderBackground(m_pPrimaryCmdBuffer, uSwapChainIndex, pParam.fExposure);
    KGLOG_PROCESS_ERROR(bRetCode);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_RenderBackground(gfx::KCommandBuffer* pPrimaryCmdBuffer, uint32_t uSwapChainIndex, float fExposure)
{
    BOOL                 bResult             = FALSE;
    BOOL                 bRetCode            = FALSE;
    BOOL                 bBeginCommandBuffer = FALSE;
    gfx::KGraphicDevice* pGraphicDevice      = gfx::GetGraphicDevice();

    gfx::KCommandBuffer* pSecondaryCmdBuffer = m_vSecondaryCmdBuffers[uSwapChainIndex];
    KGLOG_PROCESS_ERROR(pSecondaryCmdBuffer != nullptr);
    // pSecondaryCmdBuffer = pPrimaryCmdBuffer;
    KGLOG_PROCESS_ERROR(pPrimaryCmdBuffer != nullptr);

    bRetCode = pGraphicDevice->BeginCommandBuffer(pSecondaryCmdBuffer, m_pFrameBuffer);
    KGLOG_PROCESS_ERROR(bRetCode);
    bBeginCommandBuffer = TRUE;

    pSecondaryCmdBuffer->BeginDebugLabel("NativeCameraBackground");
    pSecondaryCmdBuffer->SetObjectName("UploadCmdBuffer_NativeCameraBackground");

    bRetCode = _RenderFromHardwareBuffer(uSwapChainIndex, fExposure);
    KGLOG_PROCESS_ERROR(bRetCode);

    bBeginCommandBuffer = FALSE;
    bRetCode            = pGraphicDevice->EndCommandBuffer(pSecondaryCmdBuffer);
    KGLOG_PROCESS_ERROR(bRetCode);

    pSecondaryCmdBuffer->EndDebugLabel();
    pGraphicDevice->CmdExecuteCommands(pPrimaryCmdBuffer, &pSecondaryCmdBuffer, 1);

    bResult = TRUE;
Exit0:
    if (bBeginCommandBuffer)
    {
        pSecondaryCmdBuffer->EndDebugLabel();
        pGraphicDevice->EndCommandBuffer(pSecondaryCmdBuffer);
    }

    return bResult;
}

BOOL KHardwareCameraBackground::_RenderFromHardwareBuffer(uint32_t uSwapChainIndex, float fExposure)
{
    BOOL                                           bResult        = FALSE;
    BOOL                                           bRetCode       = FALSE;
    gfx::KCommandBuffer*                           pCommandBuffer = m_vSecondaryCmdBuffers[uSwapChainIndex];
    // gfx::KCommandBuffer* pCommandBuffer = m_pPrimaryCmdBuffer;
    gfx::KGraphicDevice*                           pGraphicDevice = nullptr;
    KCameraImage&                                  cameraImage    = m_vCameraImages[uSwapChainIndex];
    VkAndroidHardwareBufferFormatPropertiesANDROID formatProperties{
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
    };

    pGraphicDevice = gfx::GetGraphicDevice();
    KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

    // 创建绑定到 Hardware Buffer 的 Image
    bRetCode = _CreateImage(cameraImage, formatProperties);
    KGLOG_PROCESS_ERROR(bRetCode);

    bRetCode = _TransitionImageLayout(cameraImage.hImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    KGLOG_PROCESS_ERROR(bRetCode);

    // 设置 Ycbcr 的采样器转换， 当 Sample 创建里用了 external format 时需要这个
    bRetCode = _SetupYcbcrConversion(formatProperties);
    KGLOG_PROCESS_ERROR(bRetCode);

    // 更新 image 和 view
    bRetCode = _CreateImageView(cameraImage.hImage, formatProperties.format, cameraImage.hView);
    KGLOG_PROCESS_ERROR(bRetCode);

    // 创建采样器
    bRetCode = _CreateSampler();
    KGLOG_PROCESS_ERROR(bRetCode);

    // 创建 Graphics Pipeline
    bRetCode = _CreateGraphicsPipeline();
    KGLOG_PROCESS_ERROR(bRetCode);

    // 更新 Descriptor Set
    bRetCode = _UpdateDescriptorSets(cameraImage, uSwapChainIndex);
    KGLOG_PROCESS_ERROR(bRetCode);

    // 更新 Viewport 和 Scissor
    bRetCode = _UpdateViewportAndScissor(pCommandBuffer);
    KGLOG_PROCESS_ERROR(bRetCode);

    // 绑定 Graphics Pipeline
    pGraphicDevice->CmdBindPipeline(pCommandBuffer, gfx::PIPELINE_BIND_POINT_GRAPHICS, m_pPipeline);

    // Draw
    {
        int offset = 0;
        if (g_GetCameraHandler().IsPortrait())
        {
            pGraphicDevice->CmdBindVertexBuffers(pCommandBuffer, 0, 1, &m_pVertexBufferPortrait, &offset);
        }
        else if (g_GetCameraHandler().GetDeviceOrientation() == NSKBase::EDeviceOrientation::LandscapeRight)
        {
            pGraphicDevice->CmdBindVertexBuffers(pCommandBuffer, 0, 1, &m_pVertexBufferReverse, &offset);
        }
        else
        {
            pGraphicDevice->CmdBindVertexBuffers(pCommandBuffer, 0, 1, &m_pVertexBufferDefault, &offset);
        }
    }
    pGraphicDevice->CmdBindIndexBuffer(pCommandBuffer, m_pIndexBuffer, 0, gfx::INDEX_TYPE_UINT16);
    pGraphicDevice->CmdBindDescriptorSets(pCommandBuffer, gfx::PIPELINE_BIND_POINT_GRAPHICS, m_pLayout, 0, m_vDescriptorSets[uSwapChainIndex], 0, nullptr);
    pGraphicDevice->CmdPushConstants(pCommandBuffer, m_pLayout, gfx::SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &fExposure);
    pGraphicDevice->CmdDrawIndexed(pCommandBuffer, m_vIndices.size(), 1, 0, 0, 0);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_GetHardwareBufferProperties(AHardwareBuffer* pHardwareBuffer, VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties, VkAndroidHardwareBufferPropertiesANDROID& properties)
{
    BOOL     bResult = FALSE;
    VkResult result;
    VkDevice device = GetVkDevice();

    properties = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
        .pNext = &formatProperties,
    };

    result = vks::vkGetAndroidHardwareBufferPropertiesANDROID(device, pHardwareBuffer, &properties);
    KGLOG_PROCESS_ERROR(result == VK_SUCCESS);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_CreateImage(KCameraImage& cameraImage, VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties)
{
    BOOL                                     bResult  = FALSE;
    BOOL                                     bRetCode = FALSE;
    VkResult                                 result;
    AHardwareBuffer_Desc                     hardwareBufferDesc{};
    VkAndroidHardwareBufferPropertiesANDROID properties{};
    AHardwareBuffer*                         pHardwareBuffer = g_GetCameraHandler().GetHardwareBuffer();
    m_ConversionInfo                                         = {
                                                .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
    };

    _DestroyImage(cameraImage);

    AHardwareBuffer_describe(pHardwareBuffer, &hardwareBufferDesc);

    // Contains how to sample the image
    bRetCode = _GetHardwareBufferProperties(pHardwareBuffer, formatProperties, properties);
    KGLOG_PROCESS_ERROR(bRetCode);

    // Create an image to bind to our AHardwareBuffer
    bRetCode = _BindImageToHardwareBuffer(cameraImage.hImage, hardwareBufferDesc.width, hardwareBufferDesc.height, formatProperties);
    KGLOG_PROCESS_ERROR(bRetCode);

    // Allocate the device memory for the image
    bRetCode = _AllocateDeviceMemoryForImage(cameraImage.hImage, cameraImage.hMemory, properties, pHardwareBuffer);
    KGLOG_PROCESS_ERROR(bRetCode);

    // Bind the allocated memory to the image
    result = kimgui::vks::vkBindImageMemory(GetVkDevice(), cameraImage.hImage, cameraImage.hMemory, 0);
    KGLOG_PROCESS_ERROR(result == VK_SUCCESS);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_BindImageToHardwareBuffer(VkImage& hImage, uint32_t uWidth, uint32_t uHeight, const VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties)
{
    BOOL     bResult = FALSE;
    VkDevice device  = GetVkDevice();
    VkResult result;

    const VkExternalFormatANDROID externalFormatAndroid = {
        .sType          = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
        .externalFormat = formatProperties.externalFormat,
    };

    const VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext       = &externalFormatAndroid,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
    };

    const VkImageCreateInfo createInfo = {
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = &externalMemoryImageCreateInfo,
        .flags                 = 0u,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = formatProperties.format,
        .extent                = {uWidth, uHeight, 1u},
        .mipLevels             = 1u,
        .arrayLayers           = 1u,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0u,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    result = vks::vkCreateImage(device, &createInfo, nullptr, &hImage);
    KGLOG_PROCESS_ERROR(result == VK_SUCCESS);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_AllocateDeviceMemoryForImage(VkImage& hImage, VkDeviceMemory& hDeviceMemory, const VkAndroidHardwareBufferPropertiesANDROID& properties, AHardwareBuffer* pHardwareBuffer)
{
    BOOL             bResult        = FALSE;
    BOOL             bRetCode       = FALSE;
    VkDevice         device         = GetVkDevice();
    VkPhysicalDevice physicalDevice = GetVkPhysicalDevice();
    VkResult         result;
    uint32_t         uMemoryTypeIndex = 0;

    KG_ASSERT_EXIT(pHardwareBuffer);
    KG_ASSERT_EXIT(hImage != VK_NULL_HANDLE);
    KG_ASSERT_EXIT(hDeviceMemory == VK_NULL_HANDLE);

    bRetCode = KNativeCameraUtility::FindMemoryType(properties.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, uMemoryTypeIndex);
    KGLOG_PROCESS_ERROR(bRetCode);

    {
        VkImportAndroidHardwareBufferInfoANDROID const androidHardwareBufferInfo = {
            .sType  = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
            .buffer = pHardwareBuffer,
        };

        VkMemoryDedicatedAllocateInfo const memoryDedicatedAllocateInfo = {
            .sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext  = &androidHardwareBufferInfo,
            .image  = hImage,
            .buffer = VK_NULL_HANDLE,
        };

        const VkMemoryAllocateInfo memoryAllocateInfo = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = &memoryDedicatedAllocateInfo,
            .allocationSize  = properties.allocationSize,
            .memoryTypeIndex = uMemoryTypeIndex,
        };

        result = kimgui::vks::vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &hDeviceMemory);
        KGLOG_PROCESS_ERROR(result == VK_SUCCESS);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_SetupYcbcrConversion(const VkAndroidHardwareBufferFormatPropertiesANDROID& formatProperties)
{
    BOOL     bResult = FALSE;
    VkResult result;
    VkDevice device = GetVkDevice();

    if (m_Conversion == VK_NULL_HANDLE)
    {
        VkExternalFormatANDROID const externalFormat = {
            .sType          = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
            .externalFormat = formatProperties.externalFormat,
        };

        VkSamplerYcbcrConversionCreateInfo const conversionCreateInfo = {
            .sType                       = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
            .pNext                       = &externalFormat,
            .format                      = formatProperties.format,
            .ycbcrModel                  = formatProperties.suggestedYcbcrModel,
            .ycbcrRange                  = formatProperties.suggestedYcbcrRange,
            .components                  = formatProperties.samplerYcbcrConversionComponents,
            .xChromaOffset               = formatProperties.suggestedXChromaOffset,
            .yChromaOffset               = formatProperties.suggestedYChromaOffset,
            .chromaFilter                = VK_FILTER_LINEAR,
            .forceExplicitReconstruction = VK_FALSE,
        };

        result = vks::vkCreateSamplerYcbcrConversion(device, &conversionCreateInfo, nullptr, &m_Conversion);
        KGLOG_PROCESS_ERROR(result == VK_SUCCESS);
    }

    m_ConversionInfo.conversion = m_Conversion;
    bResult                     = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_CreateImageView(VkImage& hImage, VkFormat format, VkImageView& hView)
{
    BOOL     bResult = FALSE;
    VkResult result;
    VkDevice device = GetVkDevice();

    KG_ASSERT_EXIT(hView == VK_NULL_HANDLE);
    KG_ASSERT_EXIT(hImage != VK_NULL_HANDLE);
    KGLOG_PROCESS_ERROR(device != VK_NULL_HANDLE);

    {
        VkImageViewCreateInfo const imageViewCreateInfo = {
            .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext      = &m_ConversionInfo,
            .flags      = 0u,
            .image      = hImage,
            .viewType   = VK_IMAGE_VIEW_TYPE_2D,
            .format     = format,
            .components = {
                           .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                           },
            .subresourceRange = {
                           .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel   = 0u,
                           .levelCount     = 1u,
                           .baseArrayLayer = 0u,
                           .layerCount     = 1u,
                           },
        };

        result = vks::vkCreateImageView(device, &imageViewCreateInfo, nullptr, &hView);
        KGLOG_PROCESS_ERROR(result == VK_SUCCESS);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_CreateSampler()
{
    BOOL                 bResult        = FALSE;
    BOOL                 bRetCode       = FALSE;
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();

    KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

    if (m_pSampler == nullptr)
    {
        gfx::KSamplerState samplerState;
        samplerState.pNext = &m_ConversionInfo;
        if (DrvOption::bCheckYCBCRSupported)
        {
            samplerState.enuMagFilter = gfx::FILTER_CUBIC_IMG;
            samplerState.enuMinFilter = gfx::FILTER_CUBIC_IMG;
        }
        else
        {
            samplerState.enuMagFilter = gfx::FILTER_LINEAR;
            samplerState.enuMinFilter = gfx::FILTER_LINEAR;
        }
        samplerState.enuMipmapMode   = gfx::SAMPLER_MIPMAP_MODE_LINEAR;
        samplerState.enuAddressModeU = gfx::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerState.enuAddressModeV = gfx::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerState.enuAddressModeW = gfx::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerState.fMipLodBias     = 0.0f;
        samplerState.fMaxAnisotropy  = 0.f;
        samplerState.bCompareEnable  = VK_FALSE;
        samplerState.enuCompareFunc  = gfx::SAMPLER_COMPARE_OP_ALWAYS;
        samplerState.fToMinLod       = 0.0f;
        samplerState.fToMaxLod       = 0.0f;
        samplerState.enuBorderColor  = gfx::BORDER_COLOR_INT_OPAQUE_BLACK;

        m_pSampler = new gfx::KVulkanSampler();
        bRetCode   = m_pSampler->Create(&samplerState);
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_CreateGraphicsPipeline()
{
    BOOL                              bResult  = FALSE;
    BOOL                              bRetCode = FALSE;
    std::array<gfx::KShaderStage*, 2> arrShaderStages{nullptr, nullptr};
    gfx::KGraphicDevice*              pGraphicDevice = nullptr;
    gfx::KRenderState                 renderState;
    gfx::GraphicsPipelineDesc         graphicsPipelineDesc;
    uint32_t                          maxSetPages = 1;

    if (m_pPipeline == nullptr)
    {
        pGraphicDevice = gfx::GetGraphicDevice();
        KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

        bRetCode = KNativeCameraUtility::GetSwapChainCount(maxSetPages);
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = KNativeCameraUtility::loadShaderFromMemory(NC_SHADER_SRC, arrShaderStages, NC_BACKGROUND_DEF);
        KGLOG_PROCESS_ERROR(bRetCode);

        if (m_pVertexDescriptor == nullptr)
        {
            bRetCode = pGraphicDevice->CreateVertDescriptor(&m_pVertexDescriptor);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bRetCode = (*m_pVertexDescriptor)
                       .Begin()
                       .AddBindDescription(VERTEX_BUFFER_BIND_ID0, 4 * sizeof(float), gfx::VERTEX_INPUT_RATE_VERTEX)
                       .AddAttribute(VERTEX_BUFFER_BIND_ID0, gfx::KAttribUsage::VERT_POS_INDX, gfx::VERT_FORMAT_R32G32_SFLOAT, 0)
                       .AddAttribute(VERTEX_BUFFER_BIND_ID0, 1, gfx::VERT_FORMAT_R32G32_SFLOAT, 2 * sizeof(float))
                       .End();
        KGLOG_PROCESS_ERROR(bRetCode);

        if (m_DescriptorPoolContainer.m_pDescriptorPool == nullptr)
        {
            bRetCode = pGraphicDevice->CreateDescriptorPool(&m_DescriptorPoolContainer.m_pDescriptorPool);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bRetCode = (*m_DescriptorPoolContainer.m_pDescriptorPool)
                       .Begin(maxSetPages)
                       .AddPoolItem(gfx::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                       .End();
        KGLOG_PROCESS_ERROR(bRetCode);

        if (m_pLayout == nullptr)
        {
            bRetCode = pGraphicDevice->CreateLayout(&m_pLayout);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bRetCode = (*m_pLayout)
                       .Begin()
                       .AddCombinedLayout(0, gfx::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gfx::SHADER_STAGE_FRAGMENT_BIT, 0, 1, &m_pSampler)
                       .AddPushContantRange(gfx::SHADER_STAGE_FRAGMENT_BIT, sizeof(float), 0)
                       .End();
        KGLOG_PROCESS_ERROR(bRetCode);

        // Render State
        {
            // MS Settings
            renderState.ResetDefaultValue();
            renderState.sampleCountFlag             = gfx::SAMPLE_COUNT_1_BIT;
            renderState.sampleShadingEnable         = VK_FALSE;
            renderState.minSampleShading            = 0.f;
            renderState.sampleAlphaToCoverageEnable = VK_FALSE;
            renderState.sampleAlphaToOneEnable      = VK_FALSE;
            renderState.sampleMask                  = 0u;

            // Color blend settings
            renderState.blendAttachment[0].blendEnable = VK_FALSE;
            renderState.blendAttachment[0].writeA      = TRUE;
            renderState.blendAttachment[0].writeR      = TRUE;
            renderState.blendAttachment[0].writeG      = TRUE;
            renderState.blendAttachment[0].writeB      = TRUE;
            renderState.blendAttachCount               = 1;

            // Rasterization settings
            renderState.depthClampEnable        = VK_FALSE;
            renderState.rasterizerDiscardEnable = VK_FALSE;
            renderState.polygonMode             = gfx::POLYGON_MODE_FILL;
            renderState.cullMode                = gfx::CULL_MODE_NONE;
            renderState.frontFaceMode           = gfx::FRONT_FACE_CLOCKWISE;
            renderState.depthBiasEnable         = VK_FALSE;
            renderState.lineWidth               = 1.f;

            // Input Assembly settings
            renderState.drawMode = gfx::PT_TRIANGLE_STRIP;

            // Depth Stencil settings
            renderState.depthTestEnable       = VK_FALSE;
            renderState.depthWriteEnable      = VK_FALSE;
            renderState.depthCompareOp        = gfx::DEPTH_TEST_LEQUAL;
            renderState.depthBoundsTestEnable = VK_FALSE;
            renderState.stencilTestEnable     = VK_FALSE;

            // Viewport settings
            renderState.defaultViewPortEnable = VK_FALSE;
            renderState.defaultScissorEnable  = VK_FALSE;
            renderState.viewPort.x            = 0.f;
            renderState.viewPort.y            = 0.f;
            renderState.viewPort.width        = static_cast<float>(m_nWidth);
            renderState.viewPort.height       = static_cast<float>(m_nHeight);
            renderState.viewPort.minDepth     = 0.f;
            renderState.viewPort.maxDepth     = 1.f;

            renderState.scissor.offsetX      = 0;
            renderState.scissor.offsetY      = 0;
            renderState.scissor.extendWidth  = static_cast<uint32_t>(m_nWidth);
            renderState.scissor.extendHeight = static_cast<uint32_t>(m_nHeight);
        }

        graphicsPipelineDesc.pLayout           = m_pLayout;
        graphicsPipelineDesc.pRenderPass       = m_pRenderPass;
        graphicsPipelineDesc.pStage            = arrShaderStages.data();
        graphicsPipelineDesc.pVertexDescriptor = m_pVertexDescriptor;
        graphicsPipelineDesc.pRenderState      = &renderState;
        graphicsPipelineDesc.uStageCount       = arrShaderStages.size();
        bRetCode                               = pGraphicDevice->CreateGraphicsPipeline(&m_pPipeline, &graphicsPipelineDesc);
        KGLOG_PROCESS_ERROR(bRetCode);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KHardwareCameraBackground::_TransitionImageLayout(VkImage& hImage, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    BOOL            bResult = FALSE;
    VkResult        result;
    VkDevice        device         = VK_NULL_HANDLE;
    VkQueue         hQueue         = VK_NULL_HANDLE;
    VkFence         hFence         = VK_NULL_HANDLE;
    VkCommandBuffer hCommandBuffer = VK_NULL_HANDLE;

    device = GetVkDevice();
    KGLOG_PROCESS_ERROR(device != VK_NULL_HANDLE);

    hQueue = GetGraphicQueue();
    KGLOG_PROCESS_ERROR(hQueue != VK_NULL_HANDLE);

    {
        VkCommandBufferAllocateInfo allocateInfo = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = dynamic_cast<gfx::KVulkanCommandPool*>(m_pCommandPool)->GetCmdPool(),
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        result = vks::vkAllocateCommandBuffers(device, &allocateInfo, &hCommandBuffer);
        KGLOG_PROCESS_ERROR(result == VK_SUCCESS);

        {
            VkCommandBufferBeginInfo beginInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };

            result = vks::vkBeginCommandBuffer(hCommandBuffer, &beginInfo);
            KGLOG_PROCESS_ERROR(result == VK_SUCCESS);
        }

        VkImageSubresourceRange subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout           = oldLayout,
            .newLayout           = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = hImage,
            .subresourceRange    = subresourceRange,
        };

        vks::vkCmdPipelineBarrier(hCommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        result = vks::vkEndCommandBuffer(hCommandBuffer);
        KGLOG_PROCESS_ERROR(result == VK_SUCCESS);

        {
            VkSubmitInfo submitInfo = {
                .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers    = &hCommandBuffer,
            };

            VkFenceCreateInfo fenceCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = 0,
            };

            result = vks::vkCreateFence(device, &fenceCreateInfo, nullptr, &hFence);
            KGLOG_PROCESS_ERROR(result == VK_SUCCESS);

            result = vks::vkQueueSubmit(hQueue, 1, &submitInfo, hFence);
            KGLOG_PROCESS_ERROR(result == VK_SUCCESS);

            result = vks::vkWaitForFences(device, 1, &hFence, VK_TRUE, UINT64_MAX);
            KGLOG_PROCESS_ERROR(result == VK_SUCCESS);
        }
    }

    bResult = TRUE;

Exit0:

    if (hFence != VK_NULL_HANDLE)
    {
        vks::vkDestroyFence(device, hFence, nullptr);
    }

    if (hCommandBuffer != VK_NULL_HANDLE)
    {
        vks::vkFreeCommandBuffers(device, dynamic_cast<gfx::KVulkanCommandPool*>(m_pCommandPool)->GetCmdPool(), 1, &hCommandBuffer);
    }

    return bResult;
}

BOOL KHardwareCameraBackground::_UpdateDescriptorSets(KCameraImage& cameraImage, uint32_t uSwapChainIndex)
{
    BOOL                 bResult        = FALSE;
    gfx::KGraphicDevice* pGraphicDevice = nullptr;

    pGraphicDevice = gfx::GetGraphicDevice();
    KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

    if (m_vDescriptorSets[uSwapChainIndex] == nullptr)
    {
        bResult = pGraphicDevice->CreateDescriptorSet(&m_vDescriptorSets[uSwapChainIndex], m_pLayout, &m_DescriptorPoolContainer);
        KGLOG_PROCESS_ERROR(bResult);
    }

    bResult = m_vDescriptorSets[uSwapChainIndex]->Begin().AddBindCombinedSampler(0, 0, 1, &m_pSampler, &cameraImage.hView).End();
    KGLOG_PROCESS_ERROR(bResult);

    bResult = TRUE;

Exit0:

    return bResult;
}

BOOL KHardwareCameraBackground::_UpdateViewportAndScissor(gfx::KCommandBuffer* pCommandBuffer)
{
    BOOL                 bResult        = FALSE;
    gfx::KGraphicDevice* pGraphicDevice = nullptr;
    int32_t              windowWidth    = 0;
    int32_t              windowHeight   = 0;
    float                windowAspect   = 0.f;
    float                imageAspect    = 0.f;

    pGraphicDevice = gfx::GetGraphicDevice();
    KGLOG_PROCESS_ERROR(pGraphicDevice != nullptr);

    pGraphicDevice->CmdSetViewport(pCommandBuffer, 0, 0, static_cast<float>(m_nWidth), static_cast<float>(m_nHeight), 0.f, 1.f);
    pGraphicDevice->CmdSetScissor(pCommandBuffer, 0, 0, m_nWidth, m_nHeight);

    bResult = TRUE;

Exit0:
    return bResult;
}

void KHardwareCameraBackground::Recreate()
{
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    m_DescriptorPoolContainer.Clear();
    SAFE_RELEASE(m_pLayout);
    SAFE_RELEASE(m_pVertexDescriptor);
    SAFE_RELEASE(m_pPipeline);


    for (auto& cameraImage : m_vCameraImages)
    {
        _DestroyImage(cameraImage);
    }

    for (auto& descriptorSet : m_vDescriptorSets)
    {
        if (descriptorSet)
        {
            pGraphicDevice->DestroyDescriptorSet(descriptorSet);
        }
    }

    if (m_pSampler != nullptr)
    {
        m_pSampler->Destroy();
        SAFE_RELEASE(m_pSampler)
    }
}

void KHardwareCameraBackground::_DestroyVertexBuffer()
{
    SAFE_RELEASE(m_pVertexBufferDefault)
    SAFE_RELEASE(m_pVertexBufferPortrait)
    SAFE_RELEASE(m_pVertexBufferReverse)
}

void KHardwareCameraBackground::_DestroyIndexBuffer()
{
    SAFE_RELEASE(m_pIndexBuffer)
}

void KHardwareCameraBackground::_DestroyImage(KCameraImage& cameraImage)
{
    VkDevice device = GetVkDevice();

    if (cameraImage.hView != VK_NULL_HANDLE)
    {
        vks::vkDestroyImageView(device, cameraImage.hView, nullptr);
        cameraImage.hView = VK_NULL_HANDLE;
    }

    if (cameraImage.hMemory != VK_NULL_HANDLE)
    {
        kimgui::vks::vkFreeMemory(device, cameraImage.hMemory, nullptr);
        cameraImage.hMemory = VK_NULL_HANDLE;
    }

    if (cameraImage.hImage != VK_NULL_HANDLE)
    {
        vks::vkDestroyImage(device, cameraImage.hImage, nullptr);
        cameraImage.hImage = VK_NULL_HANDLE;
    }
}

void KHardwareCameraBackground::UnInit()
{
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();

    for (auto& cameraImage : m_vCameraImages)
    {
        _DestroyImage(cameraImage);
    }

    for (auto& descriptorSet : m_vDescriptorSets)
    {
        if (descriptorSet)
        {
            pGraphicDevice->DestroyDescriptorSet(descriptorSet);
        }
    }
    m_DescriptorPoolContainer.Clear();

    SAFE_RELEASE(m_pLayout)
    SAFE_RELEASE(m_pVertexDescriptor)
    SAFE_RELEASE(m_pPipeline)

    if (m_pSampler)
    {
        m_pSampler->Destroy();
        SAFE_RELEASE(m_pSampler)
    }

    for (auto& secondaryCmdBuffer : m_vSecondaryCmdBuffers)
    {
        pGraphicDevice->DestroyCommandBuffer(secondaryCmdBuffer);
        SAFE_RELEASE(secondaryCmdBuffer)
    }
    pGraphicDevice->DestroyCommandPool(m_pCommandPool);
    SAFE_RELEASE(m_pCommandPool)

    _DestroyVertexBuffer();
    _DestroyIndexBuffer();

    m_pFrameBuffer = nullptr;
    m_pRenderPass  = nullptr;
}
#endif

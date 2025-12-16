//
// Created by Ant on 2024/8/16.
//
#include "KNativeCameraUtility.h"
#include "Engine/KGLog.h"
#include "../KVulkanDevice.h"
#include "../../../../KG3D_Imgui/Vulkan/func/KVulkanFunc.h"
#include "../KVulkanInitializers.h"
#include "../KVulkanTools.h"
#include "../GFXVulkan.h"
#include "../KGraphicDevice.h"

BOOL KNativeCameraUtility::CreateCommandPool(gfx::KGraphicDevice *pGfxDevice, gfx::enumForProcessType eCommandType, gfx::KCommandPool **ppCommandPool)
{
    BOOL bResult = FALSE;
    auto pkvkDevice = GetVulkanDevice();
    uint32_t uQueueFamilyIndex = pkvkDevice->GetQueueFamilyIndexProcessType(eCommandType);
    KGLOG_ASSERT_EXIT(uQueueFamilyIndex != UINT32_MAX);

    pGfxDevice->CreateCommandPool(ppCommandPool, uQueueFamilyIndex);
    KGLOG_ASSERT_EXIT(*ppCommandPool);
    (*ppCommandPool)->SetObjectName("ArCommandPool");

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KNativeCameraUtility::CreateCommandBuffers(gfx::enumCommandBufferLevel eLevel, gfx::KCommandBuffer **pCommandBuffers, gfx::enumForProcessType eCommandType, gfx::KCommandPool *pCommandPool)
{
    BOOL bResult = FALSE;

    if (!(*pCommandBuffers))
    {
        *pCommandBuffers = new gfx::KVulkanCommandBuffer;
        bResult = (*pCommandBuffers)->Create(eLevel, eCommandType, pCommandPool);
        KGLOG_PROCESS_ERROR(bResult);
    }

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KNativeCameraUtility::loadShaderFromMemory(const std::string &filepath, std::array<gfx::KShaderStage *, 2> &arrShaderStage, const char *szShaderDef)
{
    BOOL bResult = FALSE;
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    KGLOG_PROCESS_ERROR(pGraphicDevice);

    bResult = pGraphicDevice->LoadShaderVSAndFS(arrShaderStage.data(), filepath.c_str(), UNUSED_FILE_PATH, szShaderDef, UNUSED_MACRO,
                                                nullptr, false);
    KGLOG_PROCESS_ERROR(bResult);

    bResult = TRUE;
Exit0:
    return bResult;
}

BOOL KNativeCameraUtility::GetSwapChainImageIndex(uint32_t &uIndex)
{
    BOOL bResult = FALSE;
    gfx::KGraphicContext* pContext = nullptr;

    bResult = GetGraphicContext(gfx::MAIN_CONTEXT, &pContext);
    KGLOG_PROCESS_ERROR(bResult);

    uIndex = pContext->GetSwapChainImageIndex();

    bResult = TRUE;

Exit0:
    return bResult;
}

BOOL KNativeCameraUtility::GetGraphicContext(gfx::enumGraphicContext eType, gfx::KGraphicContext** ppGraphicContext)
{
    BOOL bResult = FALSE;
    gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    KGLOG_PROCESS_ERROR(pGraphicDevice);

    *ppGraphicContext = pGraphicDevice->GetGraphicContext(eType);
    KGLOG_PROCESS_ERROR(*ppGraphicContext);

    bResult = TRUE;

    Exit0:
    return bResult;
}

BOOL KNativeCameraUtility::FindMemoryType(uint32_t uTypeFilter, VkMemoryPropertyFlags eProperties, uint32_t &uMemoryType)
{
    BOOL bResult = FALSE;
    VkPhysicalDevice hPhysicalDevice = GetVkPhysicalDevice();
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vks::vkGetPhysicalDeviceMemoryProperties(hPhysicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((uTypeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & eProperties) == eProperties)
        {
            uMemoryType = i;
            bResult = TRUE;
            goto Exit0;
        }
    }

    KGLogPrintf(KGLOG_ERR, "Failed to find suitable memory type.");

Exit0:
    return bResult;
}

BOOL KNativeCameraUtility::GetSwapChainCount(uint32_t &uCount)
{
    BOOL bResult = FALSE;
    gfx::KGraphicContext* pContext = nullptr;

    bResult = GetGraphicContext(gfx::MAIN_CONTEXT, &pContext);
    KGLOG_PROCESS_ERROR(bResult);

    uCount = pContext->GetSwapChainImageCount();

    bResult = TRUE;

Exit0:
    return bResult;
}
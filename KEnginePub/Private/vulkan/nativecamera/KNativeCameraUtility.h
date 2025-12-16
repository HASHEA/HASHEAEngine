//
// Created by Ant on 2024/8/16.
//
#pragma once

#include "../KVulkanFunc.h"
#include "../../IGFX_Private.h"
#include "../KVulkanDefine.h"

#ifndef NC_SHADER_SRC
 #define NC_SHADER_SRC R"(/data/material/shader_mb/NativeCamera.glvk)"
#endif

#ifndef NC_BACKGROUND_DEF
#define NC_BACKGROUND_DEF "background"
#endif

struct KCameraRenderParam
{
    gfx::KRenderPass* pRenderPass{nullptr};
    gfx::KRenderFrameBuffer* pFrameBuffer{nullptr};
    gfx::KCommandBuffer* pPrimaryCmdBuffer{nullptr};

    int32_t nWidth = 0;
    int32_t nHeight = 0;

    float fExposure = 1.0f;
};

struct KCameraImage
{
    VkImageView hView{VK_NULL_HANDLE};
    VkDeviceMemory hMemory{VK_NULL_HANDLE};
    VkImage hImage{VK_NULL_HANDLE};
};

enum class BackgroundRenderState
{
    RENDER_WITH_HARDWARE_BUFFER,
    RENDER_WITH_CAMERA_IMAGE
};

class KNativeCameraUtility
{
public:
    static BOOL CreateCommandPool(gfx::KGraphicDevice *pGfxDevice, gfx::enumForProcessType eCommandType, gfx::KCommandPool** ppCommandPool);
    static BOOL CreateCommandBuffers(gfx::enumCommandBufferLevel eLevel, gfx::KCommandBuffer** pCommandBuffers, gfx::enumForProcessType eCommandType, gfx::KCommandPool* pCommandPool);
    static BOOL loadShaderFromMemory(const std::string& filepath, std::array<gfx::KShaderStage*, 2>& arrShaderStage, const char* szShaderDef);
    static BOOL GetSwapChainImageIndex(uint32_t& uIndex);
    static BOOL GetSwapChainCount(uint32_t& uCount);
    static BOOL FindMemoryType(uint32_t uTypeFilter, VkMemoryPropertyFlags eProperties, uint32_t& uMemoryType);

private:
    static BOOL GetGraphicContext(gfx::enumGraphicContext eType, gfx::KGraphicContext** ppGraphicContext);

private:
    KNativeCameraUtility() = default;
    ~KNativeCameraUtility() = default;
};

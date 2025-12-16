#pragma once

#include "KVulkanFunc.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include <math.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <set>
#if defined(_WIN32)
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif
#if defined(__ANDROID__)
#include "KVulkan.h"
#include <android/asset_manager.h>
#endif

// Custom define for better code readability
#define VK_FLAGS_NONE         0
// Default fence timeout in nanoseconds
#define DEFAULT_FENCE_TIMEOUT 100000000000

// Macro to check and display Vulkan return results
#if defined(__ANDROID__)
#define VK_CHECK_RESULT(f)                                                                                                                     \
    {                                                                                                                                          \
        VkResult res = (f);                                                                                                                    \
        if (res != VK_SUCCESS)                                                                                                                 \
        {                                                                                                                                      \
            KGLogPrintf(KGLOG_ERR, "Fatal : VkResult is \" %s \" in %s at line %d", vks::tools::ErrorString(res).c_str(), __FILE__, __LINE__); \
            assert(res == VK_SUCCESS);                                                                                                         \
        }                                                                                                                                      \
    }
#else
#define VK_CHECK_RESULT(f)                                                                                                                         \
    {                                                                                                                                              \
        VkResult res = (f);                                                                                                                        \
        if (res != VK_SUCCESS)                                                                                                                     \
        {                                                                                                                                          \
            std::cout << "Fatal : VkResult is \"" << vks::tools::ErrorString(res) << "\" in " << __FILE__ << " at line " << __LINE__ << std::endl; \
            assert(res == VK_SUCCESS);                                                                                                             \
        }                                                                                                                                          \
    }
#endif

namespace gfx
{
    enum enumForProcessType : uint8_t;
    struct IShaderReflector;
} // namespace gfx

namespace vks
{
    class KBuffer;
    namespace tools
    {
        /** @brief Disable message boxes on fatal errors */
        extern bool errorModeSilent;

        /** @brief Returns an error code as a string */
        std::string ErrorString(VkResult errorCode);

        /** @brief Returns the device type as a string */
        std::string PhysicalDeviceTypeString(VkPhysicalDeviceType type);

        // Selected a suitable supported depth format starting with 32 bit down to 16 bit
        // Returns false if none of the depth formats in the list is supported by the device
        VkBool32 GetSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat);

        // Put an image memory barrier for setting an image layout on the sub resource into the given command buffer
        void SetImageLayout(
            VkCommandBuffer         cmdbuffer,
            VkImage                 image,
            VkImageLayout           oldImageLayout,
            VkImageLayout           newImageLayout,
            VkImageSubresourceRange subresourceRange,
            VkPipelineStageFlags    srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VkPipelineStageFlags    dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        );
        // Uses a fixed sub resource layout with first mip level and layer
        void SetImageLayout(
            VkCommandBuffer      cmdbuffer,
            VkImage              image,
            VkImageAspectFlags   aspectMask,
            VkImageLayout        oldImageLayout,
            VkImageLayout        newImageLayout,
            VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        );

        void SetImageLayout(
            VkCommandBuffer         cmdBuffer,
            VkImage                 image,
            VkImageAspectFlags      aspectMask,
            VkImageLayout           oldImageLayout,
            VkImageLayout           newImageLayout,
            VkImageSubresourceRange subresourceRange
        );

        // VkResult vkAllocateMemory(
        //	VkDevice                                    device,
        //	const VkMemoryAllocateInfo* pAllocateInfo,
        //	const VkAllocationCallbacks* pAllocator,
        //	VkDeviceMemory* pMemory);

        // VkResult vkCreateImage(
        //	VkDevice					device,
        //	const VkImageCreateInfo* pCreateInfo,
        //	const VkAllocationCallbacks* pAllocator,
        //	VkImage* pImage);

        // VkResult vkCreateImageView(
        //	VkDevice                                    device,
        //	const VkImageViewCreateInfo* pCreateInfo,
        //	const VkAllocationCallbacks* pAllocator,
        //	VkImageView* pView);


        // VkResult  vkCreateSampler(
        //	VkDevice                                    device,
        //	const VkSamplerCreateInfo* pCreateInfo,
        //	const VkAllocationCallbacks* pAllocator,
        //	VkSampler* pSampler);

        // void vkDestroySampler(
        //	VkDevice									device,
        //	VkSampler									sampler,
        //	const VkAllocationCallbacks* pAllocator
        //);

        // void vkDestroySamplers(VkDevice	               device);


        // void vkCmdDraw(
        //	VkCommandBuffer                             commandBuffer,
        //	uint32_t                                    vertexCount,
        //	uint32_t                                    instanceCount,
        //	uint32_t                                    firstVertex,
        //	uint32_t                                    firstInstance);

        // void vkCmdDrawIndexed(
        //	VkCommandBuffer                             commandBuffer,
        //	uint32_t                                    indexCount,
        //	uint32_t                                    instanceCount,
        //	uint32_t                                    firstIndex,
        //	int32_t                                     vertexOffset,
        //	uint32_t                                    firstInstance);

        // void vkCmdDrawIndirect(
        //	VkCommandBuffer                             commandBuffer,
        //	VkBuffer                                    buffer,
        //	VkDeviceSize                                offset,
        //	uint32_t                                    drawCount,
        //	uint32_t                                    stride);

        // void vkCmdDrawIndexedIndirect(
        //	VkCommandBuffer                             commandBuffer,
        //	VkBuffer                                    buffer,
        //	VkDeviceSize                                offset,
        //	uint32_t                                    drawCount,
        //	uint32_t                                    stride);

        // void vkCmdDispatch(
        //	VkCommandBuffer                             commandBuffer,
        //	uint32_t                                    groupCountX,
        //	uint32_t                                    groupCountY,
        //	uint32_t                                    groupCountZ);

        /** @brief Inser an image memory barrier into the command buffer */
        void InsertImageMemoryBarrier(
            VkCommandBuffer         cmdbuffer,
            VkImage                 image,
            VkAccessFlags           srcAccessMask,
            VkAccessFlags           dstAccessMask,
            VkImageLayout           oldImageLayout,
            VkImageLayout           newImageLayout,
            VkPipelineStageFlags    srcStageMask,
            VkPipelineStageFlags    dstStageMask,
            VkImageSubresourceRange subresourceRange
        );

        void InsertBufferMemoryBarrier(
            VkCommandBuffer      cmdbuffer,
            VkBuffer             buffer,
            VkAccessFlags        srcAccessMask,
            VkAccessFlags        dstAccessMask,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask,
            VkDeviceSize         offset,
            VkDeviceSize         size
        );

        void InsertMemoryBarrier(VkCommandBuffer cmdbuffer);

        // Display error message and exit on fatal error
        void ExitFatal(std::string message, int32_t exitCode);
        void ExitFatal(std::string message, VkResult resultCode);

        VkShaderModule CreateShaderString(
            const char*            szShaderName,
            const char*            pcszEntrypoint,
            const char*            pcszShaderString,
            VkShaderStageFlagBits  stage,
            VkDevice               device,
            gfx::IShaderReflector* pReflector,
            std::set<uint32_t>&    errorLines,
            uint32_t*              pRetHash,
            BOOL*                  pRealBuild,
            BOOL                   bCreateModule
        );

        // BOOL OnlyReflectShader(
        //	const char* szShaderName,
        //	const char* pcszEntrypoint,
        //	const char* pcszShaderString,
        //	VkShaderStageFlagBits  stage,
        //	VkDevice               device,
        //	gfx::IShaderReflector* pReflector,
        //	std::set<uint32_t>& errorLines,
        //	uint32_t* pRetHash,
        //	BOOL* pRealBuild
        //);

        void ShaderLoaderInit();
        void ShaderLoaderUnInit();


        /** @brief Checks if a file exists */
        bool FileExists(const std::string& filename);
    } // namespace tools
} // namespace vks

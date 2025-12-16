#include "gli/gli.hpp"
#include "../IGFX_Private.h"
#include "GFXVulkan.h"
#include "kVulkanBuffer.h"
#include "KVulkanRayTracing.h"
#include "KVulkanFunc.h"
#include "KVulkanDevice.h"
#include "KVulkanTools.h"
#include "KVulkanInitializers.h"
#include "KVulkanSwapChain.h"
#include "KVulkanRenderContext.h"
#include "KVulkanRenderFrameBuffer.h"
#include "KVulkanBindlessManager.h"
#include "KVulkanCommandBuffer.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "Engine/KGLog.h"
#include "KBase/Public/time/KTimer.h"
#include "KBase/Public/async_task/KAsyncTaskManager.h"
#include "KVulkanDebug.h"
#include "KEnginePub/Public/IKTexture.h"
#include <mutex>
#include "KBase/Public/thread/KThread.h"
#include "../loader/KTexturePool.h"
#include "KBase/Public/io/KFile.h"
#include "../loader/KTextureRaw.h"
#include "../loader/KTextureMask.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KVulkanStagingManager.h"
#include "KVulkanUploadCmdBufferManager.h"
#include "KVulkanDynamicRingBuffer.h"
#include "KBase/Public/str/KUtf8Convert.h"
#include "KBase/Public/KG3D_Base/KG3D_Vector.h"
#include "KEngine/Public/KEngineCore.h"
#include "KSpirv/Private/KSpirvBuilder.h"
#include "KEnginePub/Public/KEsDrv.h"
#include "KEnginePub/Public/switchoption/KEngineSwitchOption.h"
#include "KVulkanGraphicDevice.h"
#include "KEnginePub/Private/vulkan/KShaderResourcePoolVK.h"
#include "KEnginePub/Private/vulkan/KShaderResourceVK.h"
#include "KMaterialSystem/Public/IKMaterialSystem.h"
#include "KBase/Public/io/IFile.h"
#include "KEnginePub/Public/KProfileTools.h"
#include "KEnginePub/Public/IGFX_RHIHelper.h"
#include "KVulkanTexture.h"
#include "KEnginePub/Private/loader/KGFX_FileTexture.h"

#ifdef _WIN32
#include <windows.h>
#endif

//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

#ifndef PROP_NAME_MAX
#define PROP_NAME_MAX 32
#endif

#ifndef PROP_VALUE_MAX
#define PROP_VALUE_MAX 92
#endif

#undef FreeMemory

#define BIND_OPTIMIZE_ON 1

namespace gfx
{
    /// This map takes care of hashing a render pass based on the render targets passed to cmdBeginRender

    // RenderPass map per thread (this will make lookups lock free and we only need a lock when inserting a RenderPass Map for the first time)
    RenderPassMap                                gRenderPassMap;
    // FrameBuffer map per thread (this will make lookups lock free and we only need a lock when inserting a FrameBuffer map for the first time)
    std::unordered_map<ThreadID, FrameBufferMap> gFrameBufferMap;
    std::mutex                                   gFrameBufferMutex;
    // std::mutex gRenderpassMutex;

    ThreadID GetCurrentThreadID()
    {
        return GetThreadId();
    }

    RenderPassMap& _get_render_pass_map()
    {
        ThreadID threadid = GetCurrentThreadID();
        return gRenderPassMap;
    }

    RenderPassMap& get_render_pass_map()
    {
        ASSERT(IsMainThread()); // 当前仅主线程可调用
        return _get_render_pass_map();
    }

    /*FrameBufferMap& get_frame_buffer_map()
    {
        ASSERT(IsMainThread());     // 当前仅主线程可调用

        const auto& it = gFrameBufferMap.find(GetCurrentThreadID());
        if (it == gFrameBufferMap.end())
        {
            // Only need a lock when creating a new framebuffer map for this thread
            //gFrameBufferMutex.lock();     // 单线程使用时应该不用加锁，先注释掉
            FrameBufferMap p{};
            static FrameBufferMap& r = gFrameBufferMap.insert({ GetCurrentThreadID(), p }).first->second;
            //gFrameBufferMutex.unlock();
            return r;
        }
        else
        {
            return it->second;
        }
    }*/

#if defined(__cplusplus)
#define DECLARE_ZERO(type, var) \
    type var = {};
#else
#define DECLARE_ZERO(type, var) \
    type var = {0};
#endif

    VkAccessFlags GetAccessFlag(enumAccessFlag flag)
    {
        VkAccessFlags ret = ACCESS_NONE;

        switch (flag)
        {
        case ACCESS_INDIRECT_COMMAND_READ_BIT:
            ret = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            break;
        case ACCESS_INDEX_READ_BIT:
            ret = VK_ACCESS_INDEX_READ_BIT;
            break;
        case ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
            ret = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            break;
        case ACCESS_UNIFORM_READ_BIT:
            ret = VK_ACCESS_UNIFORM_READ_BIT;
            break;
        case ACCESS_INPUT_ATTACHMENT_READ_BIT:
            ret = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
            break;
        case ACCESS_SHADER_BIT:
            ret = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case ACCESS_SHADER_READ_BIT:
            ret = VK_ACCESS_SHADER_READ_BIT;
            break;
        case ACCESS_SHADER_WRITE_BIT:
            ret = VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE:
            ret = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case ACCESS_COLOR_ATTACHMENT:
            ret = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case ACCESS_DEPTH_STENCIL_ATTACHMENT:
            ret = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case ACCESS_TRANSFER_READ_BIT:
            ret = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case ACCESS_TRANSFER_WRITE_BIT:
            ret = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case ACCESS_HOST_READ_BIT:
            ret = VK_ACCESS_HOST_READ_BIT;
            break;
        case ACCESS_HOST_WRITE_BIT:
            ret = VK_ACCESS_HOST_WRITE_BIT;
            break;
        case ACCESS_MEMORY_READ_BIT:
            ret = VK_ACCESS_MEMORY_READ_BIT;
            break;
        case ACCESS_MEMORY_WRITE_BIT:
            ret = VK_ACCESS_MEMORY_WRITE_BIT;
            break;
        case ACCESS_COMMAND_PROCESS_READ_BIT_NVX:
            ret = 0x00020000; // VK_ACCESS_COMMAND_PROCESS_READ_BIT_NVX;
            break;
        case ACCESS_COMMAND_PROCESS_WRITE_BIT_NVX:
            ret = 0x00040000; // VK_ACCESS_COMMAND_PROCESS_WRITE_BIT_NVX;
            break;
        case ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT:
            ret = VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT;
            break;
        case ACCESS_FLAG_BITS_MAX_ENUM:
            ret = VK_ACCESS_FLAG_BITS_MAX_ENUM;
            break;
        case ACCESS_VERTEX_INDEX_READ_BIT:
            ret = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        default:
            break;
        }
        return ret;
    }

    VkImageLayout GetImageLayout(enumImageLayout layout)
    {
        VkImageLayout ret = VK_IMAGE_LAYOUT_UNDEFINED;
        switch (layout)
        {
        case IMAGE_LAYOUT_UNDEFINED:
            ret = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        case IMAGE_LAYOUT_GENERAL:
            ret = VK_IMAGE_LAYOUT_GENERAL;
            break;
        case IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            ret = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            break;
        case IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            ret = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            break;
        case IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            ret = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            break;
        case IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            ret = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;
        case IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            ret = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;
        case IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            ret = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            break;
        case IMAGE_LAYOUT_PRESENT_SRC_KHR:
            ret = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            break;
        case IMAGE_LAYOUT_SHARED_PRESENT_KHR:
            ret = VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR;
            break;
        default:
            break;
        }
        return ret;
    }

    enumTextureFormat GetTextureEnumFromFormat(VkFormat texFormat)
    {
        enumTextureFormat retFmt = TEX_FORMAT_R8G8B8A8_UNORM;
        switch (texFormat)
        {
        case VK_FORMAT_R8G8B8A8_UNORM:
            {
                retFmt = TEX_FORMAT_R8G8B8A8_UNORM;
            }
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
            {
                retFmt = TEX_FORMAT_B8G8R8A8_UNORM;
            }
            break;
        case VK_FORMAT_R8_UNORM:
            {
                retFmt = TEX_FORMAT_R8_UNORM;
            }
            break;
        case VK_FORMAT_R8G8_UNORM:
            {
                retFmt = TEX_FORMAT_R8G8_UNORM;
            }
            break;
        case VK_FORMAT_B8G8R8_UNORM:
            {
                retFmt = TEX_FORMAT_B8G8R8_UNORM;
            }
            break;
        case VK_FORMAT_R16_SFLOAT:
            {
                retFmt = TEX_FORMAT_R16_SFLOAT;
            }
            break;
        case VK_FORMAT_R16_UINT:
            {
                retFmt = TEX_FORMAT_R16_UINT;
            }
            break;
        case VK_FORMAT_R16G16_UINT:
            {
                retFmt = TEX_FORMAT_R16G16_UINT;
            }
            break;
        case VK_FORMAT_R16G16B16A16_UNORM:
            {
                retFmt = TEX_FORMAT_R16G16B16A16_UNORM;
            }
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            {
                retFmt = TEX_FORMAT_R16G16B16A16_SFLOAT;
            }
            break;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            {
                retFmt = TEX_FORMAT_D24_UNORM_S8_UINT;
            }
            break;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            {
                retFmt = TEX_FORMAT_D32_SFLOAT_S8_UINT;
            }
            break;
        case VK_FORMAT_D16_UNORM:
            {
                retFmt = TEX_FORMAT_D16_UNORM;
            }
            break;
        case VK_FORMAT_D32_SFLOAT:
            {
                retFmt = TEX_FORMAT_D32_SFLOAT;
            }
            break;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            {
                retFmt = TEX_FORMAT_A2R10G10B10_UNORM_PACK32;
            }
            break;

        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
            {
                retFmt = TEX_FORMAT_B10G11R11_UFLOAT_PACK32;
            }
            break;
        case VK_FORMAT_R32_SFLOAT:
            {
                retFmt = TEX_FORMAT_R32_FLOAT;
            }
            break;
        default:
            break;
        }
        return retFmt;
    }

    VkFormat GetTextureFormatFromTargetFormat(enumTextureFormat srcfmt, BOOL& bColorAttach, BOOL& bDepth, BOOL& bStencil, uint32_t& bytesStride)
    {
        VkFormat fmt = VK_FORMAT_UNDEFINED;
        bColorAttach = false;
        bDepth       = false;
        bStencil     = false;
        bytesStride  = 4;
        switch (srcfmt)
        {
        case TEX_FORMAT_R8G8B8A8_UNORM:
            {
                fmt          = VK_FORMAT_R8G8B8A8_UNORM;
                bytesStride  = 4;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_R8G8B8A8_SNORM:
            {
                fmt          = VK_FORMAT_R8G8B8A8_SNORM;
                bytesStride  = 4;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_R8G8B8A8_SRGB:
            {
                fmt          = VK_FORMAT_R8G8B8A8_SRGB;
                bytesStride  = 4;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_B8G8R8_UNORM:
            {
                fmt          = VK_FORMAT_B8G8R8_UNORM;
                bytesStride  = 3;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_B5G6R5_UNORM_PACK16:
            {
                fmt          = VK_FORMAT_B5G6R5_UNORM_PACK16;
                bytesStride  = 2;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_B8G8R8A8_UNORM:
            {
                fmt          = VK_FORMAT_B8G8R8A8_UNORM;
                bytesStride  = 4;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_B8G8R8A8_SRGB:
            {
                fmt          = VK_FORMAT_B8G8R8A8_SRGB;
                bytesStride  = 4;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_R8_UNORM:
            {
                fmt          = VK_FORMAT_R8_UNORM;
                bytesStride  = 1;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_R8G8_UNORM:
            {
                fmt          = VK_FORMAT_R8G8_UNORM;
                bytesStride  = 2;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_R16G16_UINT:
            {
                fmt          = VK_FORMAT_R16G16_UINT;
                bytesStride  = 4;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_R16G16B16A16_UNORM:
            {
                fmt          = VK_FORMAT_R16G16B16A16_UNORM;
                bytesStride  = 8;
                bColorAttach = true;
                bDepth       = false;
                bStencil     = false;
            }
            break;
        case TEX_FORMAT_R16G16B16A16_SFLOAT:
            {
                bytesStride  = 8;
                fmt          = VK_FORMAT_R16G16B16A16_SFLOAT;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_R32G32B32A32_SFLOAT:
            {
                bytesStride  = 16;
                fmt          = VK_FORMAT_R32G32B32A32_SFLOAT;
                bColorAttach = TRUE;
            }
            break;
        case TEX_FORMAT_BC7_SRGB_UNORM:
            {
                bytesStride = 16;
                fmt = VK_FORMAT_BC7_SRGB_BLOCK;
                bColorAttach = TRUE;
            }
            break;
        case TEX_FORMAT_R32G32_UINT:
            bytesStride  = 8;
            fmt          = VK_FORMAT_R32G32_UINT;
            bColorAttach = TRUE;
            break;
        case TEX_FORMAT_R32G32B32A32_UINT:
            bytesStride  = 16;
            fmt          = VK_FORMAT_R32G32B32A32_UINT;
            bColorAttach = TRUE;
            break;
        case TEX_FORMAT_A2R10G10B10_UNORM_PACK32:
            {
                bytesStride  = 8;
                fmt          = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_B10G11R11_UFLOAT_PACK32:
            {
                bytesStride  = 8;
                fmt          = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_R16G16_SFLOAT:
            {
                bytesStride  = 8;
                fmt          = VK_FORMAT_R16G16_SFLOAT;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_R16_SFLOAT:
            {
                bytesStride  = 2;
                fmt          = VK_FORMAT_R16_SFLOAT;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_R16_UINT:
            {
                bytesStride  = 2;
                fmt          = VK_FORMAT_R16_UINT;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_R32_SINT:
            {
                bytesStride  = 4;
                fmt          = VK_FORMAT_R32_SINT;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_R32_UINT:
            {
                bytesStride  = 4;
                fmt          = VK_FORMAT_R32_UINT;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_R32_FLOAT:
            {
                bytesStride  = 4;
                fmt          = VK_FORMAT_R32_SFLOAT;
                bColorAttach = true;
            }
            break;
        case TEX_FORMAT_D24_UNORM_S8_UINT:
            {
                bytesStride = 4;
                bDepth      = true;
                bStencil    = true;
                fmt         = VK_FORMAT_D24_UNORM_S8_UINT;
            }
            break;
        case TEX_FORMAT_D16_UNORM:
            {
                bytesStride = 2;
                bDepth      = true;
                fmt         = VK_FORMAT_D16_UNORM;
            }
            break;
        case TEX_FORMAT_D32_SFLOAT:
            {
                bytesStride = 4;
                bDepth      = true;
                fmt         = VK_FORMAT_D32_SFLOAT;
            }
            break;
        case TEX_FORMAT_D32_SFLOAT_S8_UINT:
            {
                bytesStride = 4;
                bDepth      = true;
                bStencil    = true;
                fmt         = VK_FORMAT_D32_SFLOAT_S8_UINT;
            }
            break;
        case TEX_FORMAT_R16G16_UNORM:
            bytesStride  = 4;
            bColorAttach = true;
            bDepth       = false;
            bStencil     = false;
            fmt          = VK_FORMAT_R16G16_UNORM;
            break;
        case TEX_FORMAT_R16_UNORM:
            bytesStride  = 2;
            bColorAttach = true;
            bDepth       = false;
            bStencil     = false;
            fmt          = VK_FORMAT_R16_UNORM;
            break;
        case TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
            bytesStride = 8;
            bDepth      = false;
            bStencil    = false;
            fmt         = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
            break;
        case TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
            bytesStride = 16;
            bDepth      = false;
            bStencil    = false;
            fmt         = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
            break;
        case TEX_FORMAT_ETC2_RG_UNORM_BLOCK:
            bytesStride = 16;
            fmt         = VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
            break;
        case TEX_FORMAT_BC1_RGB_UNORM:
            bytesStride = 8;
            fmt         = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
            break;
        case TEX_FORMAT_BC3_UNORM:
            bytesStride = 16;
            fmt         = VK_FORMAT_BC3_UNORM_BLOCK;
            break;
        case TEX_FORMAT_BC5_UNORM:
            bytesStride = 16;
            fmt         = VK_FORMAT_BC5_UNORM_BLOCK;
            break;
        case TEX_FORMAT_ASTC_4X4_UNORM_BLOCK:
            bytesStride = 16;
            fmt         = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
            break;
        case TEX_FORMAT_R8_UINT:
            bytesStride = 1;
            fmt         = VK_FORMAT_R8_UINT;
            bDepth      = false;
            bStencil    = false;
            break;
        default:
            ASSERT(0);
            break;
        }
        return fmt;
    }

    VkImageAspectFlags GetImageAspectMask(VkFormat format)
    {
        VkImageAspectFlags result = 0;
        switch (format)
        {
            // Depth
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            result = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
            // Stencil
        case VK_FORMAT_S8_UINT:
            result = VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
            // Depth/stencil
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            // https://vulkan.lunarg.com/doc/view/1.2.154.1/windows/1.2-extensions/vkspec.html#VUID-VkDescriptorImageInfo-imageView-01976
            // image of format VK_FORMAT_D32_SFLOAT_S8_UINT but it has both STENCIL and DEPTH aspects set
            // result = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            result = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
            // Assume everything else is Color
        default:
            result = VK_IMAGE_ASPECT_COLOR_BIT;
            break;
        }
        return result;
    }

    VkImageAspectFlags GetImageViewAspectMask(VkFormat format)
    {
        VkImageAspectFlags result = 0;
        switch (format)
        {
            // Depth
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            result = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
            // Stencil
        case VK_FORMAT_S8_UINT:
            result = VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
            // Depth/stencil
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            // https://vulkan.lunarg.com/doc/view/1.2.154.1/windows/1.2-extensions/vkspec.html#VUID-VkDescriptorImageInfo-imageView-01976
            // image of format VK_FORMAT_D32_SFLOAT_S8_UINT but it has both STENCIL and DEPTH aspects set
            // result = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            result = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
            // Assume everything else is Color
        default:
            result = VK_IMAGE_ASPECT_COLOR_BIT;
            break;
        }
        return result;
    }

    VkFilter GetSamplerFilter(enumSamplerFilter filter)
    {
        VkFilter ft = VK_FILTER_LINEAR;
        switch (filter)
        {
        case FILTER_NEAREST:
            ft = VK_FILTER_NEAREST;
            break;
        case FILTER_LINEAR:
            ft = VK_FILTER_LINEAR;
            break;
        case FILTER_CUBIC_IMG:
            ft = VK_FILTER_CUBIC_IMG;
            break;
        default:
            break;
        }
        return ft;
    }

    VkSamplerMipmapMode GetSamplerMipmapMode(enumMipMapMode mode)
    {
        VkSamplerMipmapMode md = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        switch (mode)
        {
        case SAMPLER_MIPMAP_MODE_NEAREST:
            md = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case SAMPLER_MIPMAP_MODE_LINEAR:
            md = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        default:
            break;
        }
        return md;
    }


    VkSamplerAddressMode GetSamplerAddressMode(enumSamplerAddressMode mode)
    {
        VkSamplerAddressMode md = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        switch (mode)
        {
        case SAMPLER_ADDRESS_MODE_REPEAT:
            md = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            break;
        case SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            md = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            break;
        case SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            md = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            break;
        case SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            md = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            break;
        case SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
            md = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
            break;
        default:
            break;
        }
        return md;
    }

    VkBorderColor GetSamplerBorderColor(enumBorderColor borderColor)
    {
        VkBorderColor bc = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        switch (borderColor)
        {
        case BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
            bc = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            break;
        case BORDER_COLOR_INT_TRANSPARENT_BLACK:
            bc = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
            break;
        case BORDER_COLOR_FLOAT_OPAQUE_BLACK:
            bc = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            break;
        case BORDER_COLOR_INT_OPAQUE_BLACK:
            bc = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            break;
        case BORDER_COLOR_FLOAT_OPAQUE_WHITE:
            bc = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            break;
        case BORDER_COLOR_INT_OPAQUE_WHITE:
            bc = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
            break;
        default:
            break;
        }
        return bc;
    }

    VkCompareOp GetSamplerCompareFunc(enumSamplerCompareFunc func)
    {
        VkCompareOp op = VK_COMPARE_OP_NEVER;

        switch (func)
        {
        case gfx::SAMPLER_COMPARE_OP_NEVER:
            op = VK_COMPARE_OP_NEVER;
            break;
        case gfx::SAMPLER_COMPARE_OP_LESS:
            op = VK_COMPARE_OP_LESS;
            break;
        case gfx::SAMPLER_COMPARE_OP_EQUAL:
            op = VK_COMPARE_OP_EQUAL;
            break;
        case gfx::SAMPLER_COMPARE_OP_LESS_OR_EQUAL:
            op = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case gfx::SAMPLER_COMPARE_OP_GREATER:
            op = VK_COMPARE_OP_GREATER;
            break;
        case gfx::SAMPLER_COMPARE_OP_NOT_EQUAL:
            op = VK_COMPARE_OP_NOT_EQUAL;
            break;
        case gfx::SAMPLER_COMPARE_OP_GREATER_OR_EQUAL:
            op = VK_COMPARE_OP_GREATER_OR_EQUAL;
            break;
        case gfx::SAMPLER_COMPARE_OP_ALWAYS:
            op = VK_COMPARE_OP_ALWAYS;
            break;
        default:
            break;
        }

        return op;
    }

    VkImageUsageFlags GetImageUsage(enumDescriptorType usage)
    {
        VkImageUsageFlags result = 0;
        if (DESCRIPTOR_TYPE_SAMPLED_IMAGE == (usage & DESCRIPTOR_TYPE_SAMPLED_IMAGE))
            result |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (DESCRIPTOR_TYPE_STORAGE_IMAGE == (usage & DESCRIPTOR_TYPE_STORAGE_IMAGE))
            result |= VK_IMAGE_USAGE_STORAGE_BIT;
        return result;
    }

    VkFormatFeatureFlags GetFormatFeaturesFromImageUsage(VkImageUsageFlags usage)
    {
        VkFormatFeatureFlags result = (VkFormatFeatureFlags)0;
        if (VK_IMAGE_USAGE_SAMPLED_BIT == (usage & VK_IMAGE_USAGE_SAMPLED_BIT))
        {
            result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        }
        if (VK_IMAGE_USAGE_STORAGE_BIT == (usage & VK_IMAGE_USAGE_STORAGE_BIT))
        {
            result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        }
        if (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT == (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        {
            result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        }
        if (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT == (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        return result;
    }


    VkBlendFactor GetBlendFactor(enumBlendType blendType)
    {
        VkBlendFactor factor;
        switch (blendType)
        {
        case BLEND_ZERO:
            factor = VK_BLEND_FACTOR_ZERO;
            break;
        case BLEND_ONE:
            factor = VK_BLEND_FACTOR_ONE;
            break;
        case BLEND_SRC_COLOR:
            factor = VK_BLEND_FACTOR_SRC_COLOR;
            break;
        case BLEND_ONE_MINUS_SRC_COLOR:
            factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            break;
        case BLEND_DST_COLOR:
            factor = VK_BLEND_FACTOR_DST_COLOR;
            break;
        case BLEND_ONE_MINUS_DST_COLOR:
            factor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            break;
        case BLEND_SRC_ALPHA:
            factor = VK_BLEND_FACTOR_SRC_ALPHA;
            break;
        case BLEND_ONE_MINUS_SRC_ALPHA:
            factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BLEND_DST_ALPHA:
            factor = VK_BLEND_FACTOR_DST_ALPHA;
            break;
        case BLEND_ONE_MINUS_DST_ALPHA:
            factor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            break;
        case BLEND_CONSTANT_COLOR:
            factor = VK_BLEND_FACTOR_CONSTANT_COLOR;
            break;
        case BLEND_ONE_MINUS_CONSTANT_COLOR:
            factor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            break;
        case BLEND_CONSTANT_ALPHA:
            factor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
            break;
        case BLEND_ONE_MINUS_CONSTANT_ALPHA:
            factor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
            break;
        case BLEND_SRC_ALPHA_SATURATE:
            factor = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            break;
        default:
            factor = VK_BLEND_FACTOR_ONE;
            break;
        }
        return factor;
    }

    VkBlendOp GetBlendOp(enumBlendEquationType optype)
    {
        VkBlendOp op;
        switch (optype)
        {
        case BLEND_EQUATION_ADD:
            op = VK_BLEND_OP_ADD;
            break;
        case BLEND_EQUATION_SUB:
            op = VK_BLEND_OP_SUBTRACT;
            break;
        case BLEND_EQUATION_REVSUB:
            op = VK_BLEND_OP_REVERSE_SUBTRACT;
            break;
        case BLEND_EQUATION_MIN:
            op = VK_BLEND_OP_MIN;
            break;
        case BLEND_EQUATION_MAX:
            op = VK_BLEND_OP_MAX;
            break;
        default:
            op = VK_BLEND_OP_ADD;
            break;
        }
        return op;
    }

    VkCompareOp GetDepthCompareOp(enumDepthType cp)
    {
        VkCompareOp op;
        switch (cp)
        {
        case DEPTH_TEST_LESS:
            op = VK_COMPARE_OP_LESS;
            break;
        case DEPTH_TEST_LEQUAL:
            op = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case DEPTH_TEST_EQUAL:
            op = VK_COMPARE_OP_EQUAL;
            break;
        case DEPTH_TEST_GEQUAL:
            op = VK_COMPARE_OP_GREATER_OR_EQUAL;
            break;
        case DEPTH_TEST_GREATER:
            op = VK_COMPARE_OP_GREATER;
            break;
        case DEPTH_TEST_NOTEQUAL:
            op = VK_COMPARE_OP_NOT_EQUAL;
            break;
        case DEPTH_TEST_NEVER:
            op = VK_COMPARE_OP_NEVER;
            break;
        case DEPTH_TEST_ALWAYS:
            op = VK_COMPARE_OP_ALWAYS;
            break;
        default:
            op = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        }
        return op;
    }

    VkCompareOp GetStencilCompareOp(enumStencilType st)
    {
        VkCompareOp op;
        switch (st)
        {
        case STENCIL_TEST_LESS:
            op = VK_COMPARE_OP_LESS;
            break;
        case STENCIL_TEST_LEQUAL:
            op = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case STENCIL_TEST_EQUAL:
            op = VK_COMPARE_OP_EQUAL;
            break;
        case STENCIL_TEST_GEQUAL:
            op = VK_COMPARE_OP_GREATER_OR_EQUAL;
            break;
        case STENCIL_TEST_GREATER:
            op = VK_COMPARE_OP_GREATER;
            break;
        case STENCIL_TEST_NOTEQUAL:
            op = VK_COMPARE_OP_NOT_EQUAL;
            break;
        case STENCIL_TEST_NEVER:
            op = VK_COMPARE_OP_NEVER;
            break;
        case STENCIL_TEST_ALWAYS:
            op = VK_COMPARE_OP_ALWAYS;
            break;
        default:
            op = VK_COMPARE_OP_ALWAYS;
            break;
        }
        return op;
    }

    VkStencilOp GetStencilOp(enumStencilOpType p)
    {
        VkStencilOp op = VK_STENCIL_OP_KEEP;
        switch (p)
        {
        case gfx::STENCIL_OP_KEEP:
            op = VK_STENCIL_OP_KEEP;
            break;
        case gfx::STENCIL_OP_ZERO:
            op = VK_STENCIL_OP_ZERO;
            break;
        case gfx::STENCIL_OP_REPLACE:
            op = VK_STENCIL_OP_REPLACE;
            break;
        case gfx::STENCIL_OP_INCREMENT_AND_CLAMP:
            op = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            break;
        case gfx::STENCIL_OP_DECREMENT_AND_CLAMP:
            op = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            break;
        case gfx::STENCIL_OP_INVERT:
            op = VK_STENCIL_OP_INVERT;
            break;
        case gfx::STENCIL_OP_INCREMENT_AND_WRAP:
            op = VK_STENCIL_OP_INCREMENT_AND_WRAP;
            break;
        case gfx::STENCIL_OP_DECREMENT_AND_WRAP:
            op = VK_STENCIL_OP_DECREMENT_AND_WRAP;
            break;
        default:
            break;
        }
        return op;
    }

    // VkStencilOp GetFailSOp(enumStencilOpFailSType fs)
    //{
    //   VkStencilOp op;
    //   switch (fs)
    //   {
    //   case STENCIL_OP_FAIL_S_ZERO:
    //       op = VK_STENCIL_OP_ZERO;
    //       break;
    //   case STENCIL_OP_FAIL_S_KEEP:
    //       op = VK_STENCIL_OP_KEEP;
    //       break;
    //   case STENCIL_OP_FAIL_S_REPLACE:
    //       op = VK_STENCIL_OP_REPLACE;
    //       break;
    //   case STENCIL_OP_FAIL_S_INCR:
    //       op = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    //       break;
    //   case STENCIL_OP_FAIL_S_INCRSAT:
    //       op = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    //       break;
    //   case STENCIL_OP_FAIL_S_DECR:
    //       op = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    //       break;
    //   case STENCIL_OP_FAIL_S_DECRSAT:
    //       op = VK_STENCIL_OP_DECREMENT_AND_WRAP;
    //       break;
    //   case STENCIL_OP_FAIL_S_INVERT:
    //       op = VK_STENCIL_OP_INVERT;
    //       break;
    //   default:
    //       op = VK_STENCIL_OP_KEEP;
    //       break;
    //   }
    //   return op;
    // }

    // VkStencilOp GetFailZOp(enumSTencilOpFailZType fz)
    //{
    //   VkStencilOp op;
    //   switch (fz)
    //   {
    //   case STENCIL_OP_FAIL_Z_ZERO:
    //       op = VK_STENCIL_OP_ZERO;
    //       break;
    //   case STENCIL_OP_FAIL_Z_KEEP:
    //       op = VK_STENCIL_OP_KEEP;
    //       break;
    //   case STENCIL_OP_FAIL_Z_REPLACE:
    //       op = VK_STENCIL_OP_REPLACE;
    //       break;
    //   case STENCIL_OP_FAIL_Z_INCR:
    //       op = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    //       break;
    //   case STENCIL_OP_FAIL_Z_INCRSAT:
    //       op = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    //       break;
    //   case STENCIL_OP_FAIL_Z_DECR:
    //       op = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    //       break;
    //   case STENCIL_OP_FAIL_Z_DECRSAT:
    //       op = VK_STENCIL_OP_DECREMENT_AND_WRAP;
    //       break;
    //   case STENCIL_OP_FAIL_Z_INVERT:
    //       op = VK_STENCIL_OP_INVERT;
    //       break;
    //   default:
    //       op = VK_STENCIL_OP_KEEP;
    //       break;
    //   }
    //   return op;
    // }

    // VkStencilOp GetPassZOp(enumSTencilOpPassZType pz)
    //{
    //   VkStencilOp op;
    //   switch (pz)
    //   {
    //   case STENCIL_OP_PASS_Z_ZERO:
    //       op = VK_STENCIL_OP_ZERO;
    //       break;
    //   case STENCIL_OP_PASS_Z_KEEP:
    //       op = VK_STENCIL_OP_KEEP;
    //       break;
    //   case STENCIL_OP_PASS_Z_REPLACE:
    //       op = VK_STENCIL_OP_REPLACE;
    //       break;
    //   case STENCIL_OP_PASS_Z_INCR:
    //       op = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    //       break;
    //   case STENCIL_OP_PASS_Z_INCRSAT:
    //       op = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    //       break;
    //   case STENCIL_OP_PASS_Z_DECR:
    //       op = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    //       break;
    //   case STENCIL_OP_PASS_Z_DECRSAT:
    //       op = VK_STENCIL_OP_DECREMENT_AND_WRAP;
    //       break;
    //   case STENCIL_OP_PASS_Z_INVERT:
    //       op = VK_STENCIL_OP_INVERT;
    //       break;
    //   default:
    //       op = VK_STENCIL_OP_KEEP;
    //       break;
    //   }
    //   return op;
    // }


    VkShaderStageFlagBits GetShaderStageFlag(gfx::ShaderStageType flag)
    {
        VkShaderStageFlagBits ret = VK_SHADER_STAGE_VERTEX_BIT;
        switch (flag)
        {
        case gfx::ShaderStageType::Vertex:
            ret = VK_SHADER_STAGE_VERTEX_BIT;
            break;
        case gfx::ShaderStageType::Hull:
            ret = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            break;
        case gfx::ShaderStageType::Domain:
            ret = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            break;
        case gfx::ShaderStageType::Geometry:
            ret = VK_SHADER_STAGE_GEOMETRY_BIT;
            break;
        case gfx::ShaderStageType::Fragment:
            ret = VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        case gfx::ShaderStageType::Compute:
            ret = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
            // for types of ray tracing shaders
        case gfx::ShaderStageType::RayGeneration:
            ret = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            break;
        case gfx::ShaderStageType::AnyHit:
            ret = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            break;
        case gfx::ShaderStageType::ClosestHit:
            ret = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            break;
        case gfx::ShaderStageType::Miss:
            ret = VK_SHADER_STAGE_MISS_BIT_KHR;
            break;
        case gfx::ShaderStageType::Intersection:
            ret = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            break;
        case gfx::ShaderStageType::Callable:
            ret = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            break;
        case gfx::ShaderStageType::AllGraphics:
            ret = VK_SHADER_STAGE_ALL_GRAPHICS;
            break;
        case gfx::ShaderStageType::ALLStages:
            ret = VK_SHADER_STAGE_ALL;
            break;
        default:
            break;
        }
        return ret;
    }

    VkSampleCountFlagBits GetSamplerCount(enumSampleCountFlag sampleCount)
    {
        VkSampleCountFlagBits result = VK_SAMPLE_COUNT_1_BIT;
        switch (sampleCount)
        {
        case SAMPLE_COUNT_1_BIT:
            result = VK_SAMPLE_COUNT_1_BIT;
            break;
        case SAMPLE_COUNT_2_BIT:
            result = VK_SAMPLE_COUNT_2_BIT;
            break;
        case SAMPLE_COUNT_4_BIT:
            result = VK_SAMPLE_COUNT_4_BIT;
            break;
        case SAMPLE_COUNT_8_BIT:
            result = VK_SAMPLE_COUNT_8_BIT;
            break;
        case SAMPLE_COUNT_16_BIT:
            result = VK_SAMPLE_COUNT_16_BIT;
            break;
        case SAMPLE_COUNT_32_BIT:
            result = VK_SAMPLE_COUNT_32_BIT;
            break;
        case SAMPLE_COUNT_64_BIT:
            result = VK_SAMPLE_COUNT_64_BIT;
            break;
        }
        return result;
    }


    VkFormat GetVertBindItemFormat(KBaseAttribType::Enum baseType, uint8_t m_baseAttribCount, BOOL bAsInt)
    {
        if (baseType == KBaseAttribType::Float)
        {
            switch (m_baseAttribCount)
            {
            case 1:
                return VK_FORMAT_R32_SFLOAT;
            case 2:
                return VK_FORMAT_R32G32_SFLOAT;
            case 3:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case 4:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
        }
        else if (baseType == KBaseAttribType::Uint8)
        {
            if (bAsInt)
            {
                switch (m_baseAttribCount)
                {
                case 1:
                    return VK_FORMAT_R8_SINT;
                case 2:
                    return VK_FORMAT_R8G8_SINT;
                case 3:
                    return VK_FORMAT_R8G8B8_SINT;
                case 4:
                    return VK_FORMAT_R8G8B8A8_SINT;
                }
            }
            else
            {
                switch (m_baseAttribCount)
                {
                case 1:
                    return VK_FORMAT_R8_UNORM;
                case 2:
                    return VK_FORMAT_R8G8_UNORM;
                case 3:
                    return VK_FORMAT_R8G8B8_SNORM;
                case 4:
                    return VK_FORMAT_R8G8B8A8_SNORM;
                }
            }
        }
        else if (baseType == KBaseAttribType::Uint32)
        {
            if (m_baseAttribCount == 1)
            {
                if (bAsInt)
                {
                    return VK_FORMAT_R32_SINT;
                }
                else
                {
                    return VK_FORMAT_R32_SFLOAT;
                }
            }
        }
        ASSERT(0 && "unknown format");
        return VK_FORMAT_UNDEFINED;
    }


    VkFormat GetVertFormat(enumVertexFormat fmt)
    {
        VkFormat ret = VK_FORMAT_UNDEFINED;
        switch (fmt)
        {
        case gfx::VERT_FORMAT_R32G32B32A32_SFLOAT:
            ret = VK_FORMAT_R32G32B32A32_SFLOAT;
            break;
        case gfx::VERT_FORMAT_R32G32B32_SFLOAT:
            ret = VK_FORMAT_R32G32B32_SFLOAT;
            break;
        case gfx::VERT_FORMAT_R32G32_SFLOAT:
            ret = VK_FORMAT_R32G32_SFLOAT;
            break;
        case gfx::VERT_FORMAT_R32_SFLOAT:
            ret = VK_FORMAT_R32_SFLOAT;
            break;
        case gfx::VERT_FORMAT_R8G8B8A8_UINT:
            ret = VK_FORMAT_R8G8B8A8_UINT;
            break;
        case gfx::VERT_FORMAT_R8G8B8A8_SINT:
            ret = VK_FORMAT_R8G8B8A8_SINT;
            break;
        case gfx::VERT_FORMAT_R8G8B8A8_UNORM:
            ret = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case gfx::VERT_FORMAT_R8G8B8A8_SNORM:
            ret = VK_FORMAT_R8G8B8A8_SNORM;
            break;
        case gfx::VERT_FORMAT_R16G16_UINT:
            ret = VK_FORMAT_R16G16_UINT;
            break;
        case gfx::VERT_FORMAT_R16G16_SINT:
            ret = VK_FORMAT_R16G16_SINT;
            break;
        default:
            break;
        }
        return ret;
    }

    VkVertexInputRate GetVertInputRate(enumVertexInputRate inputRate)
    {
        VkVertexInputRate ret = VK_VERTEX_INPUT_RATE_VERTEX;
        if (inputRate == VERTEX_INPUT_RATE_INSTANCE)
        {
            ret = VK_VERTEX_INPUT_RATE_INSTANCE;
        }
        return ret;
    }

   
    VkDescriptorType GetDescriptorType(enumDescriptorType desc)
    {
        VkDescriptorType ret = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        switch (desc)
        {
        case gfx::DESCRIPTOR_TYPE_SAMPLER:
            ret = VK_DESCRIPTOR_TYPE_SAMPLER;
            break;
        case gfx::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            ret = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            break;
        case gfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            ret = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            break;
        case gfx::DESCRIPTOR_TYPE_STORAGE_IMAGE:
            ret = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            break;
        case gfx::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            ret = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            break;
        case gfx::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            ret = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            break;
        case gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            ret = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            break;
        case gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER:
            ret = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            break;
        case gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            ret = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            break;
        case gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            ret = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            break;
        case gfx::DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            ret = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            break;
        case gfx::DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
            ret = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            break;
        default:
            break;
        }
        return ret;
    }

    VkPipelineBindPoint GetVkPipelineBindPoint(enumPipelineBindPoint point);

    VkPipelineStageFlagBits GetPipelineStageFlag(enumPipelineStageFlag flag)
    {
        VkPipelineStageFlagBits ret = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        switch (flag)
        {
        case PIPELINE_STAGE_TOP_OF_PIPE_BIT:
            ret = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case PIPELINE_STAGE_DRAW_INDIRECT_BIT:
            ret = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
            break;
        case PIPELINE_STAGE_VERTEX_INPUT_BIT:
            ret = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            break;
        case PIPELINE_STAGE_VERTEX_SHADER_BIT:
            ret = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            break;
        case PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT:
            ret = VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
            break;
        case PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT:
            ret = VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
            break;
        case PIPELINE_STAGE_GEOMETRY_SHADER_BIT:
            ret = VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
            break;
        case PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
            ret = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT:
            ret = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            break;
        case PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT:
            ret = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        case PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
            ret = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case PIPELINE_STAGE_COMPUTE_SHADER_BIT:
            ret = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;
        case PIPELINE_STAGE_TRANSFER_BIT:
            ret = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
            ret = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        case PIPELINE_STAGE_HOST_BIT:
            ret = VK_PIPELINE_STAGE_HOST_BIT;
            break;
        case PIPELINE_STAGE_ALL_GRAPHICS_BIT:
            ret = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
            break;
        case PIPELINE_STAGE_ALL_COMMANDS_BIT:
            ret = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
        case PIPELINE_STAGE_FLAG_BITS_MAX_ENUM:
            ret = VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM;
            break;
        default:
            break;
        }
        return ret;
    }

    VkPrimitiveTopology GetPrimitiveTopology(enumDrawMode eDrawMode)
    {
        VkPrimitiveTopology vkPrimitiveTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        switch (eDrawMode)
        {
        case PT_POINT_LIST:
            vkPrimitiveTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case PT_LINE_LIST:
            vkPrimitiveTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case PT_LINE_STRIP:
            vkPrimitiveTopo = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            break;
        case PT_TRIANGLE_LIST:
            vkPrimitiveTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        case PT_TRIANGLE_STRIP:
            vkPrimitiveTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            break;
        case PT_TRIANGLE_FAN:
            vkPrimitiveTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
            break;
        }
        return vkPrimitiveTopo;
    }

    VkPolygonMode GetPolygonMode(enumPolygonMode ePolygonMode)
    {
        VkPolygonMode vkPolygonMode = VK_POLYGON_MODE_FILL;
        switch (ePolygonMode)
        {
        case POLYGON_MODE_FILL:
            vkPolygonMode = VK_POLYGON_MODE_FILL;
            break;
        case POLYGON_MODE_LINE:
            vkPolygonMode = VK_POLYGON_MODE_LINE;
            break;
        case POLYGON_MODE_POINT:
            vkPolygonMode = VK_POLYGON_MODE_POINT;
            break;
        default:
            vkPolygonMode = VK_POLYGON_MODE_FILL;
            break;
        }
        return vkPolygonMode;
    }

    VkCullModeFlagBits GetCullMode(enumCullMode eCullMode)
    {
        VkCullModeFlagBits vkCullMode = VK_CULL_MODE_NONE;
        switch (eCullMode)
        {
        case CULL_MODE_NONE:
            vkCullMode = VK_CULL_MODE_NONE;
            break;
        case CULL_MODE_FRONT:
            vkCullMode = VK_CULL_MODE_FRONT_BIT;
            break;
        case CULL_MODE_BACK:
            vkCullMode = VK_CULL_MODE_BACK_BIT;
            break;
        case CULL_MODE_FRONT_AND_BACK:
            vkCullMode = VK_CULL_MODE_FRONT_AND_BACK;
            break;
        default:
            vkCullMode = VK_CULL_MODE_BACK_BIT;
            break;
        }
        return vkCullMode;
    }

    VkFrontFace GetFrontFaceMode(enumFrontFaceMode eFrontFaceMode)
    {
        VkFrontFace vkFrontFaceMode = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        switch (eFrontFaceMode)
        {
        case FRONT_FACE_COUNTER_CLOCKWISE:
            vkFrontFaceMode = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            break;
        case FRONT_FACE_CLOCKWISE:
            vkFrontFaceMode = VK_FRONT_FACE_CLOCKWISE;
            break;
        default:
            vkFrontFaceMode = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            break;
        }
        return vkFrontFaceMode;
    }

    static uint32_t GetNumBitsPerPixel(enumTextureFormat eFormat)
    {
        switch (eFormat)
        {
        case TEX_FORMAT_NONE:
            return 0;

        // 8-bit
        case TEX_FORMAT_R8_UNORM:
        case TEX_FORMAT_R8_UINT:
        case TEX_FORMAT_ETC2_R_UNORM_BLOCK:
        case TEX_FORMAT_ETC2_R_SNORM_BLOCK:
            return 8;

        // 16-bit
        case TEX_FORMAT_R8G8_UNORM:
        case TEX_FORMAT_R16_UNORM:
        case TEX_FORMAT_R16_UINT:
        case TEX_FORMAT_R16_SFLOAT:
        case TEX_FORMAT_D16_UNORM:
        case TEX_FORMAT_B5G6R5_UNORM_PACK16:
        case TEX_FORMAT_ETC2_RG_UNORM_BLOCK:
        case TEX_FORMAT_ETC2_RG_SNORM_BLOCK:
            return 16;

        // 24-bit
        case TEX_FORMAT_B8G8R8_UNORM:
        case TEX_FORMAT_R8G8B8_UNORM:
            return 24;

        // 32-bit
        case TEX_FORMAT_R8G8B8A8_UNORM:
        case TEX_FORMAT_R8G8B8A8_SNORM:
        case TEX_FORMAT_R8G8B8A8_SRGB:
        case TEX_FORMAT_R8G8B8A8_UINT:
        case TEX_FORMAT_B8G8R8A8_UNORM:
        case TEX_FORMAT_B8G8R8A8_SRGB:
        case TEX_FORMAT_R16G16_UINT:
        case TEX_FORMAT_R16G16_UNORM:
        case TEX_FORMAT_R16G16_SFLOAT:
        case TEX_FORMAT_R32_SINT:
        case TEX_FORMAT_R32_UINT:
        case TEX_FORMAT_R32_FLOAT:
        case TEX_FORMAT_D32_SFLOAT:
        case TEX_FORMAT_A2R10G10B10_UNORM_PACK32:
        case TEX_FORMAT_B10G11R11_UFLOAT_PACK32:
        case TEX_FORMAT_D24_UNORM_S8_UINT: // 实际为24+8，但通常打包在32位
        case TEX_FORMAT_R32G32_UINT:       // 2x32位组件
            return 32;

        // 64-bit
        case TEX_FORMAT_R16G16B16A16_UNORM:
        case TEX_FORMAT_R16G16B16A16_SFLOAT:
        case TEX_FORMAT_R64_UINT:
        case TEX_FORMAT_D32_SFLOAT_S8_UINT: // 32深度+8模板，通常打包在64位
            return 64;

        // 128-bit
        case TEX_FORMAT_R32G32B32A32_SFLOAT:
        case TEX_FORMAT_R32G32B32A32_UINT:
            return 128;

        // 压缩格式 - BC系列
        case TEX_FORMAT_BC1_RGB_UNORM:  // 4 bpp (64 bits per 4x4 block)
        case TEX_FORMAT_BC1_RGBA_UNORM: // 4 bpp
        case TEX_FORMAT_BC4_UNORM:      // 4 bpp
        case TEX_FORMAT_BC4_SNORM:      // 4 bpp
            return 4;

        case TEX_FORMAT_BC2_UNORM:      // 8 bpp (128 bits per 4x4 block)
        case TEX_FORMAT_BC3_UNORM:      // 8 bpp
        case TEX_FORMAT_BC5_UNORM:      // 8 bpp
        case TEX_FORMAT_BC5_SNORM:      // 8 bpp
        case TEX_FORMAT_BC6H_UFLOAT:    // 8 bpp
        case TEX_FORMAT_BC6H_SFLOAT:    // 8 bpp
        case TEX_FORMAT_BC7_UNORM:      // 8 bpp
        case TEX_FORMAT_BC7_SRGB_UNORM: // 8 bpp
            return 8;

        // 压缩格式 - ETC2系列
        case TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:   // 4 bpp (64 bits per 4x4 block)
            return 4;
        case TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: // 8 bpp (128 bits per 4x4 block)
            return 8;

        // 压缩格式 - ASTC系列
        case TEX_FORMAT_ASTC_4X4_UNORM_BLOCK: // 8 bpp (128 bits per 4x4 block)
            return 8;
        case TEX_FORMAT_ASTC_6X6_UNORM_BLOCK: // 3 bpp (128 bits per 6x6 block = 3.55 bpp, rounded)
            return 3;
        case TEX_FORMAT_ASTC_8X8_UNORM_BLOCK: // 2 bpp (128 bits per 8x8 block)
            return 2;

        default:
            return 0;
        }
    }

#ifndef VK_ATTACHMENT_LOAD_OP_NONE_KHR
#define VK_ATTACHMENT_LOAD_OP_NONE_KHR (VkAttachmentLoadOp)1000400000
#endif

    //**********************************************************************************
    //   VULKAN CONST VALUES
    //**********************************************************************************
    VkAttachmentLoadOp gVkAttachmentLoadOpTranslator[enumLoadActionType::MAX_LOAD_ACTION] =
        {
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_LOAD,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_LOAD_OP_NONE_KHR,
    };

    VkAttachmentStoreOp gVkAttachmentStoreOpTranslator[enumStoreActionType::MAX_STORE_ACTION] =
        {
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_NONE_QCOM,
    };

    ////////////////////////////////////////////////////////////////////
    KVulkanFence::KVulkanFence()
    {
    }

    KVulkanFence::~KVulkanFence()
    {
        if (m_pFence)
        {
            VkDevice pDevice = GetVkDevice();

            if (m_bSubmitted)
            {
                VkResult vkResult = vks::vkWaitForFences(pDevice, 1, &m_pFence, true, UINT64_MAX); // set 2s timeout.
                ASSERT(VK_SUCCESS == vkResult);
            }

            vks::vkDestroyFence(pDevice, m_pFence, nullptr);
        }
    }

    BOOL KVulkanFence::Create(BOOL bInitWithSignaled)
    {
        BOOL               bRet      = false;
        VkDevice           pDevice   = GetVkDevice();
        VkFenceCreateFlags flags     = !bInitWithSignaled ? 0 : VK_FENCE_CREATE_SIGNALED_BIT;
        VkFenceCreateInfo  fenceInfo = vks::initializers::FenceCreateInfo(flags);
        VkResult           hRetCode  = VK_INCOMPLETE;
        hRetCode                     = vks::vkCreateFence(pDevice, &fenceInfo, nullptr, &m_pFence);
        KGLOG_COM_PROCESS_ERROR(hRetCode);

        m_bSubmitted = FALSE;

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanFence::Destroy()
    {
        BOOL bRet = FALSE;

        VkDevice pDevice = GetVkDevice();
        KGLOG_PROCESS_ERROR(m_pFence);

        vks::vkDestroyFence(pDevice, m_pFence, nullptr);
        m_pFence = VK_NULL_HANDLE;

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    VkFence KVulkanFence::GetFence()
    {
        return m_pFence;
    }

    void KVulkanFence::SetObjectName(const char* szName)
    {
#ifdef _WIN32
        m_strName = szName;
        GetVulkanDevice()->SetObjectLabel(m_pFence, szName);
#endif
    }

    BOOL KVulkanFence::Query()
    {
        if (m_bSignaled)
            return TRUE;

        return CheckState();
    }

    BOOL KVulkanFence::CheckState()
    {
        if (!m_bSubmitted)
        {
            return FALSE;
        }

        VkResult vkResult = vks::vkGetFenceStatus(GetVkDevice(), m_pFence);
        switch (vkResult)
        {
        case VK_SUCCESS:
            m_bSignaled = TRUE;
            ++m_uSignalFenceCounter;
            return TRUE;

        case VK_NOT_READY:
            break;

        default:
            break;
        }

        return FALSE;
    }

    void KVulkanFence::Reset()
    {
        VkDevice pDevice = GetVkDevice();
        vks::vkResetFences(pDevice, 1, &m_pFence);
        m_bSignaled  = false;
        m_bSubmitted = false;
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanSemaphore::KVulkanSemaphore()
    {
        m_pSemaphore = nullptr;
#if semaphore_mem_lect
        static uint32_t uMemeLectCount = 0;
        uMemeLectCount++;
        m_memLeckBuffer = new uint8_t[uMemeLectCount];
#endif
    }

    KVulkanSemaphore::~KVulkanSemaphore()
    {
#if semaphore_mem_lect
        SAFE_DELETE_ARRAY(m_memLeckBuffer);
#endif
        Destroy();
    }

    BOOL KVulkanSemaphore::Create()
    {
        BOOL                  bRet                = false;
        VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::SemaphoreCreateInfo();
        VkResult              hRetCode            = vks::vkCreateSemaphore(
            GetVkDevice(),
            &semaphoreCreateInfo,
            nullptr,
            &m_pSemaphore
        );
        KGLOG_COM_PROCESS_ERROR(hRetCode);
        bRet = true;
    Exit0:
        return bRet;
    }

    void KVulkanSemaphore::Destroy()
    {
        if (m_pSemaphore)
        {
            vks::vkDestroySemaphore(GetVkDevice(), m_pSemaphore, nullptr);
            m_pSemaphore = VK_NULL_HANDLE;
        }
    }

    VkSemaphore& KVulkanSemaphore::GetSemaphore()
    {
        return m_pSemaphore;
    }

    ////////////////////////////////////////////////////////////////////
    gli::format Util_GetGLIImageFormatFromVk(VkFormat eVkFomate)
    {
        gli::format format = gli::FORMAT_UNDEFINED;

        switch (eVkFomate)
        {
        case VK_FORMAT_R32_SFLOAT:
            format = gli::FORMAT_R32_SFLOAT_PACK32;
            break;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            format = gli::FORMAT_RGBA32_SFLOAT_PACK32;
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
            format = gli::FORMAT_RGBA8_UNORM_PACK8;
            break;
        case VK_FORMAT_R8G8B8_UNORM:
            format = gli::FORMAT_RGB8_UNORM_PACK8;
            break;
        case VK_FORMAT_R8_UNORM:
            format = gli::FORMAT_R8_UNORM_PACK8;
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
            format = gli::FORMAT_BGRA8_UNORM_PACK8;
            break;
        case VK_FORMAT_B8G8R8_UNORM:
            format = gli::FORMAT_BGR8_UNORM_PACK8;
            break;
        case VK_FORMAT_R16G16B16A16_UNORM:
            format = gli::FORMAT_RGBA16_UNORM_PACK16;
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            format = gli::FORMAT_RGBA16_SFLOAT_PACK16;
            break;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_DXT1_UNORM_BLOCK8;
            break;
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            format = gli::FORMAT_RGB_DXT1_SRGB_BLOCK8;
            break;
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_DXT1_SRGB_BLOCK8;
            break;
        case VK_FORMAT_BC2_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_DXT3_UNORM_BLOCK16;
            break;
        case VK_FORMAT_BC2_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_DXT3_SRGB_BLOCK16;
            break;
        case VK_FORMAT_BC3_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16;
            break;
        case VK_FORMAT_BC3_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16;
            break;
        case VK_FORMAT_BC7_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_BP_UNORM_BLOCK16;
            break;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
            format = gli::FORMAT_RGB_BP_UFLOAT_BLOCK16;
            break;
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            format = gli::FORMAT_RGB_BP_SFLOAT_BLOCK16;
            break;
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
            format = gli::FORMAT_RGB_ETC2_UNORM_BLOCK8;
            break;
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
            format = gli::FORMAT_RGB_ETC2_SRGB_BLOCK8;
            break;
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ETC2_UNORM_BLOCK8;
            break;
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ETC2_SRGB_BLOCK8;
            break;
            // case gli::FORMAT_RGBA_ETC2_UNORM_BLOCK16:
            //   break;
            // case gli::FORMAT_RGBA_ETC2_SRGB_BLOCK16:
            //   break;
        case VK_FORMAT_EAC_R11_UNORM_BLOCK:
            format = gli::FORMAT_R_EAC_UNORM_BLOCK8;
            break;
        case VK_FORMAT_EAC_R11_SNORM_BLOCK:
            format = gli::FORMAT_R_EAC_SNORM_BLOCK8;
            break;
        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
            format = gli::FORMAT_RG_EAC_UNORM_BLOCK16;
            break;
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
            format = gli::FORMAT_RG_EAC_SNORM_BLOCK16;
            break;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            format = gli::FORMAT_RGB10A2_UNORM_PACK32;
            break;
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_4X4_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_4X4_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_5X4_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_5X4_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_5X5_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_5X5_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_6X5_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_6X5_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_6X6_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_6X6_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_8X5_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_8X5_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_8X6_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_8X6_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_8X8_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_8X8_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X5_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X5_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X6_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X6_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X8_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X8_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X10_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_10X10_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_12X10_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_12X10_SRGB_BLOCK16;
            break;
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_12X12_UNORM_BLOCK16;
            break;
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
            format = gli::FORMAT_RGBA_ASTC_12X12_SRGB_BLOCK16;
            break;
        case VK_FORMAT_BC5_UNORM_BLOCK:
            format = gli::FORMAT_RG_ATI2N_UNORM_BLOCK16;
            break;
        case VK_FORMAT_BC5_SNORM_BLOCK:
            format = gli::FORMAT_RG_ATI2N_SNORM_BLOCK16;
            break;
        default:
            // KASSERT(0);
            KASSERT(eVkFomate <= gli::FORMAT_LAST);
            KGLogPrintf(KGLOG_ERR, "Maybe unSuport texture format");
            format = (gli::format)(eVkFomate);
            break;
        }

        return format;
    }


    BOOL SaveTo4ChannelTgaMemFile(std::vector<uint8_t>& memFiledata, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height)
    {
        BOOL bRet = false;

        memFiledata.resize(uBuffersize + 18);
        uint32_t channels    = 4;
        uint8_t  header0[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        uint8_t  header1[6]  = {0};
        header1[0]           = width % 256;  // 图片width 的 lowbyte
        header1[1]           = width / 256;  // 图片width 的 highbyte
        header1[2]           = height % 256; // 图片height 的 lowbyte
        header1[3]           = height / 256; // 图片height的 highbyte
        header1[4]           = channels * 8;
        header1[5]           = 8;
        memcpy(&memFiledata[0], header0, sizeof(header0));
        memcpy(&memFiledata[sizeof(header0)], header1, sizeof(header1));
        uint32_t pixelPos = 18;

        // invers y and rgba to bgra
        for (uint32_t i = 0; i < height; ++i)
        {
            for (uint32_t j = 0; j < width; ++j)
            {
                uint32_t id = (height - i - 1) * width * 4 + j * 4;
                ASSERT(id < uBuffersize);
                memFiledata[pixelPos]      = pBuffer[id + 2];
                memFiledata[pixelPos + 1]  = pBuffer[id + 1];
                memFiledata[pixelPos + 2]  = pBuffer[id];
                memFiledata[pixelPos + 3]  = pBuffer[id + 3];
                pixelPos                  += channels;
            }
        }

        bRet = true;
        // Exit0:
        return bRet;
    }

    BOOL SaveTo4ChannelTgaFile(const char* pFileName, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height)
    {
        BOOL  bRet     = false;
        BOOL  bRetCode = false;
        FILE* fp       = nullptr;

        std::vector<uint8_t> memFiledata;
        bRetCode = SaveTo4ChannelTgaMemFile(memFiledata, pBuffer, uBuffersize, width, height);
        KGLOG_PROCESS_ERROR(bRetCode);
        fp = fopen(pFileName, "wb");
        KGLOG_PROCESS_ERROR(fp);
        fwrite(memFiledata.data(), memFiledata.size() * sizeof(uint8_t), 1, fp);
        bRet = true;
    Exit0:
        if (fp)
        {
            fclose(fp);
            fp = nullptr;
        }
        return bRet;
    }

    struct ClBitMapFileHeader
    {
        // unsigned short    bfType;
        unsigned long  bfSize;
        unsigned short bfReserved1;
        unsigned short bfReserved2;
        unsigned long  bfOffBits;
    };

    struct ClBitMapInfoHeader
    {
        unsigned long  biSize;
        long           biWidth;
        long           biHeight;
        unsigned short biPlanes;
        unsigned short biBitCount;
        unsigned long  biCompression;
        unsigned long  biSizeImage;
        long           biXPelsPerMeter;
        long           biYPelsPerMeter;
        unsigned long  biClrUsed;
        unsigned long  biClrImportant;
    };

    BOOL SaveTo4ChannelBmpMemFile(std::vector<uint8_t>& memFiledata, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height)
    {
        unsigned short bmType   = 0x4D42;
        uint32_t       channels = 4;
        uint32_t       step     = channels * width;
        memFiledata.resize(sizeof(bmType) + sizeof(ClBitMapFileHeader) + sizeof(ClBitMapInfoHeader) + uBuffersize);

        uint32_t pos = 0;
        memcpy(&memFiledata[pos], &bmType, sizeof(bmType));
        pos += sizeof(bmType);

        ClBitMapFileHeader bmpFileHeader;
        bmpFileHeader.bfSize      = height * step + 54;
        bmpFileHeader.bfReserved1 = 0;
        bmpFileHeader.bfReserved2 = 0;
        bmpFileHeader.bfOffBits   = 54;
        memcpy(&memFiledata[pos], &bmpFileHeader, sizeof(ClBitMapFileHeader));
        pos += sizeof(ClBitMapFileHeader);

        ClBitMapInfoHeader bmpInfoHeader;
        bmpInfoHeader.biSize          = 40;
        bmpInfoHeader.biWidth         = width;
        bmpInfoHeader.biHeight        = height;
        bmpInfoHeader.biPlanes        = 1;
        bmpInfoHeader.biBitCount      = 32;
        bmpInfoHeader.biCompression   = 0;
        bmpInfoHeader.biSizeImage     = height * step;
        bmpInfoHeader.biXPelsPerMeter = 0;
        bmpInfoHeader.biYPelsPerMeter = 0;
        bmpInfoHeader.biClrUsed       = 0;
        bmpInfoHeader.biClrImportant  = 0;
        memcpy(&memFiledata[pos], &bmpInfoHeader, sizeof(ClBitMapInfoHeader));
        pos += sizeof(ClBitMapInfoHeader);


        for (uint32_t i = 0; i < height; ++i)
        {
            for (uint32_t j = 0; j < width; ++j)
            {
                uint32_t id = (height - i - 1) * width * 4 + j * 4;
                ASSERT(height >= i + 1);
                ASSERT(id < uBuffersize);
                memFiledata[pos]      = pBuffer[id + 2];
                memFiledata[pos + 1]  = pBuffer[id + 1];
                memFiledata[pos + 2]  = pBuffer[id];
                memFiledata[pos + 3]  = pBuffer[id + 3];
                pos                  += channels;
            }
        }


        // memcpy(&memFiledata[pos], pBuffer, uBuffersize);
        ASSERT(pos == memFiledata.size());
        return true;
    }

    BOOL SaveTo4ChannelBmpFile(const char* pFileName, const uint8_t* pBuffer, uint32_t uBuffersize, uint32_t width, uint32_t height)
    {
        BOOL  bRet     = false;
        BOOL  bRetCode = false;
        FILE* fp       = nullptr;

        std::vector<uint8_t> memFiledata;
        bRetCode = SaveTo4ChannelBmpMemFile(memFiledata, pBuffer, uBuffersize, width, height);
        KGLOG_PROCESS_ERROR(bRetCode);
        fp = fopen(pFileName, "wb");
        KGLOG_PROCESS_ERROR(fp);
        fwrite(memFiledata.data(), memFiledata.size() * sizeof(uint8_t), 1, fp);
        bRet = true;
    Exit0:
        if (fp)
        {
            fclose(fp);
            fp = nullptr;
        }
        return bRet;
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanRenderTarget2D::KVulkanRenderTarget2D()
    {
        m_bForDepth      = false;
        m_bHasStencil    = false;
        m_bOwnsImage     = TRUE;
        m_nRef           = 1;
        m_pixelByteSride = 0;
        m_rowPitch       = 0;
        m_ImageUsage     = 0;
    }

    KVulkanRenderTarget2D::~KVulkanRenderTarget2D()
    {
        Destroy();
    }

    BOOL KVulkanRenderTarget2D::Destroy()
    {
        PROF_CPU();
        VkDevice            pDevice     = GetVkDevice();
        KEnginePerformance* pEnginePerf = NSEngine::GetEnginePerformance();
        uint32_t            dwRTSize    = 0;
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();

        SAFE_RELEASE(m_pFullSRV);
        SAFE_RELEASE(m_pFullUAV);
        SAFE_RELEASE(m_pFullRTVorDSV);

        for (auto iter : m_MipmapSRVs)
        {
            SAFE_RELEASE(iter);
        }
        m_MipmapSRVs.clear();

        for (auto iter : m_MipmapUAVs)
        {
            SAFE_RELEASE(iter);
        }
        m_MipmapUAVs.clear();

        for (auto iter : m_MipmapRTVorDSVs)
        {
            SAFE_RELEASE(iter);
        }
        m_MipmapRTVorDSVs.clear();

        if (!m_bOwnsImage && m_pTextureResource)
        {
            // 一般这种直接引用外部创建的VkImage的情况，都由外部掌握其生命周期（例如Swapchain）
            m_pTextureResource->m_pvkTextureImage = nullptr;
        }
        SAFE_RELEASE(m_pTextureResource);

        return true;
    }

    uint32_t KVulkanRenderTarget2D::GetMipMapCount()
    {
        ASSERT(m_MipmapSRVs.size() == mDesc.uMipLevels);
        return (uint32_t)mDesc.uMipLevels;
    }

    uint64_t KVulkanRenderTarget2D::GetNameHash()
    {
        return (uint64_t)this;
    }

    int32_t KVulkanRenderTarget2D::AddRef()
    {
        m_nRef++;
        return m_nRef;
    }
    int32_t KVulkanRenderTarget2D::GetRef()
    {
        return m_nRef;
    }

    int32_t KVulkanRenderTarget2D::Release()
    {
        m_nRef--;
        int32_t nRef = m_nRef;
        if (nRef == 0)
        {
            delete this;
        }
        return nRef;
    }

    bool KVulkanRenderTarget2D::IsForDepth()
    {
        return m_bForDepth;
    }

    bool KVulkanRenderTarget2D::IsHasStencil()
    {
        return m_bHasStencil;
    }

    uint64_t KVulkanRenderTarget2D::GetResourceSize()
    {
        // UNDONE KVulkanRenderTarget2D 【wait check】Byte
        if (m_pTextureResource)
        {
            return m_pTextureResource->m_uDevivceMemSize;
        }
        else
        {
            return 0;
        }
    }

    uint64_t KVulkanRenderTarget2D::GetId()
    {
        return (uint64_t)m_pFullRTVorDSV + m_uTextureId;
    }

    void KVulkanRenderTarget2D::SetObjectName(const char* szName)
    {
        m_szName = szName;
        if (m_pTextureResource)
        {
            m_pTextureResource->SetDebugName(szName);
        }
    }

    const char* KVulkanRenderTarget2D::GetName()
    {
        return m_szName.c_str();
    }

    const KGFX_TextureDesc& KVulkanRenderTarget2D::GetTexDesc() const
    {
        if (m_pTextureResource)
        {
            return *m_pTextureResource->GetDesc();
        }
        else
        {
            return KGFX_TextureDesc::g_EmptryValue;
        }
    }

    KGfxSubresourceRange KVulkanRenderTarget2D::ResolveSubresourceRange(const KGfxSubresourceRange& range)
    {
        KGfxSubresourceRange resolved = range;
        auto                 texDesc  = m_pTextureResource->GetDesc();

        resolved.uBaseMipLevel = std::min<uint32_t>(resolved.uBaseMipLevel, texDesc->uMipLevels - 1);
        resolved.uMipCount = std::min<uint32_t>(resolved.uMipCount, texDesc->uMipLevels - resolved.uBaseMipLevel);

        uint32_t arrayLayerCount = texDesc->uArraySize;
        resolved.uBaseArraySlice     = std::min<uint32_t>(resolved.uBaseArraySlice, arrayLayerCount - 1);
        resolved.uArrayCount     = std::min<uint32_t>(resolved.uArrayCount, arrayLayerCount - resolved.uBaseArraySlice);

        return resolved;
    }

    bool KVulkanRenderTarget2D::SaveToFile(const char* pcszSaveFilePath)
    {
        bool     bResult   = false;
        BOOL     bRetCode  = FALSE;
        VkResult vkRetCode = VkResult::VK_ERROR_UNKNOWN;

        VkDevice            pVkDevice     = GetVkDevice();
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();

        VkFormat    eSaveImageFormat  = VK_FORMAT_UNDEFINED;
        uint32_t    uBytesStride      = 0;
        gli::format eSaveGLITexFormat = gli::FORMAT_UNDEFINED;

        VkImage              pSaveImage             = nullptr;
        VkMemoryRequirements sSaveImageMemReq       = {};
        KVkDeviceMemory      pSaveImageMemory       = nullptr;
        uint32_t             uSaveImageMemoryOffset = 0;
        // #if X3D_VK_USE_VMA
        VmaAllocation        pSaveImgVMAllocation   = nullptr;
        // #endif

        KGLOG_PROCESS_ERROR(pVkDevice);
        KGLOG_PROCESS_ERROR(pVulkanDevice);
        KGLOG_PROCESS_ERROR(m_pTextureResource);
        KGLOG_PROCESS_ERROR(pcszSaveFilePath && pcszSaveFilePath[0]);

        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            KGLOG_PROCESS_ERROR(m_pTextureResource->m_pVMAllocation);
        }
        // #else
        else
        {
            KGLOG_PROCESS_ERROR(m_pTextureResource->m_pvkDevivceMem);
        }
        // #endif

        // parse save format
        {
            BOOL     bColorAttach = FALSE, bDepth = FALSE, bStencil = FALSE;
            uint32_t byteStride = 0;
            eSaveImageFormat    = GetTextureFormatFromTargetFormat(mDesc.eFormat, bColorAttach, bDepth, bStencil, uBytesStride);
            KGLOG_PROCESS_ERROR(eSaveImageFormat != VK_FORMAT_UNDEFINED);

            eSaveGLITexFormat = Util_GetGLIImageFormatFromVk(eSaveImageFormat);
            KGLOG_PROCESS_ERROR(eSaveGLITexFormat != gli::FORMAT_UNDEFINED);
        }

        // create backup image
        {
            VkImageCreateInfo imageCreateCI(vks::initializers::ImageCreateInfo());
            imageCreateCI.imageType     = VK_IMAGE_TYPE_2D;
            // Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
            imageCreateCI.format        = eSaveImageFormat;
            imageCreateCI.extent.width  = mDesc.uWidth;
            imageCreateCI.extent.height = mDesc.uHeight;
            imageCreateCI.extent.depth  = 1;
            imageCreateCI.arrayLayers   = 1;
            imageCreateCI.mipLevels     = 1;
            imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateCI.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageCreateCI.tiling        = VK_IMAGE_TILING_LINEAR;
            imageCreateCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            // Create the save image
            // #if X3D_VK_USE_VMA
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                bRetCode = pVulkanDevice->VMACreateImage(imageCreateCI, VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_TO_CPU, pSaveImage, pSaveImgVMAllocation);
                KGLOG_PROCESS_ERROR(bRetCode);
            }
            // #else
            else
            {
                vkRetCode = vks::vkCreateImage(pVkDevice, &imageCreateCI, nullptr, &pSaveImage);
                KGLOG_PROCESS_ERROR(vkRetCode == VK_SUCCESS && pSaveImage);
            }
            // #endif
        }

        // copy to the back up image
        {
            gfx::KBlitRegion src = {0, 0, mDesc.uWidth, mDesc.uHeight};
            gfx::KBlitRegion dst = {0, 0, mDesc.uWidth, mDesc.uHeight};

            // #if !X3D_VK_USE_VMA
            if (!DrvOption::bX3D_VK_USE_VMA)
            {
                VkMemoryAllocateInfo memAllocInfo(vks::initializers::MemoryAllocateInfo());
                vks::vkGetImageMemoryRequirements(pVkDevice, pSaveImage, &sSaveImageMemReq);
                memAllocInfo.allocationSize  = sSaveImageMemReq.size;
                // Memory must be host visible to copy from
                memAllocInfo.memoryTypeIndex = pVulkanDevice->GetMemoryType(sSaveImageMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                vkRetCode = pVulkanDevice->AllocateMemory(pVkDevice, &memAllocInfo, nullptr, &pSaveImageMemory, &uSaveImageMemoryOffset, (uint32_t)sSaveImageMemReq.alignment);
                KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);

                vkRetCode = vks::vkBindImageMemory(pVkDevice, pSaveImage, pSaveImageMemory, uSaveImageMemoryOffset);
                KGLOG_ASSERT_EXIT(vkRetCode == VK_SUCCESS);
            }
            // #endif

            bRetCode = gfx::_Blit(eSaveImageFormat, m_pTextureResource->GetVkHandle(), src, eSaveImageFormat, pSaveImage, dst);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        // save backup image 2 save file
        {
            // Get layout of the image (including row pitch)
            VkImageSubresource  sSaveImageSubResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
            VkSubresourceLayout sSaveImageSubResourceLayout = {};
            vks::vkGetImageSubresourceLayout(pVkDevice, pSaveImage, &sSaveImageSubResource, &sSaveImageSubResourceLayout);

            gli::extent2d  sExtent(mDesc.uWidth, mDesc.uHeight);
            gli::texture2d sGli2DTex(eSaveGLITexFormat, sExtent, 1);
            char*          pTexData     = sGli2DTex.data<char>();
            size_t         stTexMemSize = sGli2DTex.size();

            KGLOG_PROCESS_ERROR(sSaveImageMemReq.size >= stTexMemSize);

            // copy memory data 2 gli texture
            {
                const char* pMappedMemory = nullptr;
                // Map image memory so we can start copying from it
                // #if X3D_VK_USE_VMA
                if (DrvOption::bX3D_VK_USE_VMA)
                {
                    pVulkanDevice->VMAMapMemory(pSaveImgVMAllocation, (void**)&pMappedMemory);
                }
                // #else
                else
                {
                    vkRetCode = vks::vkMapMemory(pVkDevice, pSaveImageMemory, uSaveImageMemoryOffset, sSaveImageMemReq.size, 0, (void**)&pMappedMemory);
                    KGLOG_COM_ASSERT_EXIT(vkRetCode);
                }
                // #endif
                //  data += subResourceLayout.offset;
                KGLOG_PROCESS_ERROR(pMappedMemory);
                pMappedMemory += sSaveImageSubResourceLayout.offset;

                for (uint32_t y = 0; y < mDesc.uHeight; y++)
                {
                    uint32_t uRowDataSize = uBytesStride * mDesc.uWidth;
                    memcpy(pTexData, pMappedMemory, uRowDataSize);
                    pTexData      += uRowDataSize;
                    pMappedMemory += sSaveImageSubResourceLayout.rowPitch;
                }

                vks::vkUnmapMemory(pVkDevice, pSaveImageMemory);
            }

            // save 2 file
            {
                bRetCode = FALSE;
                if (KSTR_HELPER::StrEndWith(pcszSaveFilePath, ".dds"))
                {
                    bRetCode = gli::save_dds(sGli2DTex, pcszSaveFilePath) ? TRUE : FALSE;
                }
                else if (KSTR_HELPER::StrEndWith(pcszSaveFilePath, ".ktx"))
                {
                    bRetCode = gli::save_ktx(sGli2DTex, pcszSaveFilePath) ? TRUE : FALSE;
                }
                KGLOG_PROCESS_ERROR(bRetCode);
            }
        }

        bResult = true;
    Exit0:
        // #if X3D_VK_USE_VMA
        if (DrvOption::bX3D_VK_USE_VMA)
        {
            if (pSaveImgVMAllocation)
            {
                pVulkanDevice->VMAUnmapMemory(pSaveImgVMAllocation);
            }
            if (pSaveImage && pSaveImgVMAllocation)
            {
                pVulkanDevice->VMADestroyImage(pSaveImage, pSaveImgVMAllocation);
            }
        }
        // #else
        else
        {
            if (pSaveImageMemory)
            {
                pVulkanDevice->FreeMemory(pVkDevice, pSaveImageMemory, nullptr, uSaveImageMemoryOffset, (uint32_t)sSaveImageMemReq.size);
                pSaveImageMemory = nullptr;
            }
            if (pSaveImage)
            {
                vks::vkDestroyImage(pVkDevice, pSaveImage, nullptr);
                pSaveImage = nullptr;
            }
        }
        // #endif
        return bResult;
    }

    // 创建mipmap的时候可能有问题？ 需要一个更自定义话的创建方式
    // BOOL KVulkanRenderTarget2D::Create(uint32_t uWidth, uint32_t uHeight, enumTextureFormat targetFormat, const KSamplerState& samplerState, BOOL bTileOptimize)
    //{
    //  BOOL bRet = false;
    //  VkResult hRetCode = VK_INCOMPLETE;
    //  VkImageViewCreateInfo imageView;
    //  BOOL bColorAttach = false;
    //  uint32_t byteStride = 0;
    //  VkDevice pDevice = GetVkDevice();
    //  vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
    //  VkImageCreateInfo image = vks::initializers::ImageCreateInfo();
    //  image.imageType = VK_IMAGE_TYPE_2D;
    //  image.format = GetTextureFormatFromTargetFormat(targetFormat, bColorAttach, byteStride);
    //  image.extent.width = uWidth;
    //  image.extent.height = uHeight;
    //  image.extent.depth = 1;
    //  image.mipLevels = 1;
    //  image.arrayLayers = 1;
    //  image.samples = VK_SAMPLE_COUNT_1_BIT;
    //  image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    //  VkImageSubresourceRange subresourceRange;

    //  VkCommandBuffer cmd = nullptr;
    //  if (bTileOptimize)
    //  {
    //      image.tiling = VK_IMAGE_TILING_OPTIMAL;
    //  }
    //  else
    //  {
    //      image.tiling = VK_IMAGE_TILING_LINEAR;
    //  }
    //  VkSamplerCreateInfo sampler = vks::initializers::SamplerCreateInfo();
    //  m_uWidth = uWidth;
    //  m_uHeight = uHeight;
    //  m_targetFormat = targetFormat;
    //  m_pSamperState = samplerState;

    //  VkMemoryAllocateInfo memAlloc = vks::initializers::MemoryAllocateInfo();
    //  VkMemoryRequirements memReqs;

    //  // We will sample directly from the color attachment
    //  if (bColorAttach)
    //  {
    //      image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    //      m_bForDepth = FALSE;
    //  }
    //  else
    //  {
    //      image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    //      m_bForDepth = TRUE;

    //      if (targetFormat == TEX_FORMAT_D32_SFLOAT_S8_UINT || targetFormat == TEX_FORMAT_D24_UNORM_S8_UINT)
    //      {
    //          m_bHasStencil = true;
    //      }
    //      else
    //      {
    //          m_bHasStencil = false;
    //      }
    //  }


    //  hRetCode = vks::vkCreateImage(pDevice, &image, nullptr, &m_pImage);
    //  if (hRetCode != 0)
    //  {
    //      KGLogPrintf(KGLOG_ERR, "不支持这个格式");
    //      goto Exit0;
    //  }


    //  vks::vkGetImageMemoryRequirements(pDevice, m_pImage, &memReqs);
    //  memAlloc.allocationSize = memReqs.size;
    //  memAlloc.memoryTypeIndex = pVulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    //  hRetCode = vks::vkAllocateMemory(pDevice, &memAlloc, nullptr, &m_pMemory);
    //  KGLOG_COM_PROCESS_ERROR(hRetCode);

    //  hRetCode = vks::vkBindImageMemory(pDevice, m_pImage, m_pMemory, 0);
    //  KGLOG_COM_PROCESS_ERROR(hRetCode);

    //  imageView = vks::initializers::ImageViewCreateInfo();
    //  imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    //  imageView.format = image.format;
    //  imageView.flags = 0;
    //  imageView.subresourceRange = {};
    //  if (bColorAttach)
    //  {
    //      imageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    //  }
    //  else
    //  {
    //      imageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    //  }
    //  imageView.subresourceRange.baseMipLevel = 0;
    //  imageView.subresourceRange.levelCount = 1;
    //  imageView.subresourceRange.baseArrayLayer = 0;
    //  imageView.subresourceRange.layerCount = 1;
    //  imageView.image = m_pImage;

    //  hRetCode = vks::vkCreateImageView(pDevice, &imageView, nullptr, &m_pView);
    //  KGLOG_COM_PROCESS_ERROR(hRetCode);


    //  sampler.magFilter = GetSamplerFilter(samplerState.enuMagFilter);
    //  sampler.minFilter = GetSamplerFilter(samplerState.enuMinFilter);
    //  sampler.mipmapMode = GetSamplerMipmapMode(samplerState.enuMipmapMode);
    //  sampler.addressModeU = GetSamplerAddressMode(samplerState.enuAddressModeU);
    //  sampler.addressModeV = GetSamplerAddressMode(samplerState.enuAddressModeV);
    //  sampler.addressModeW = GetSamplerAddressMode(samplerState.enuAddressModeW);
    //  sampler.mipLodBias = samplerState.fMipLodBias;
    //  if (samplerState.fMaxAnisotropy == 0)
    //  {
    //      sampler.maxAnisotropy = 1.0f;
    //      sampler.anisotropyEnable = VK_FALSE;
    //  }
    //  else if (pVulkanDevice->m_Features.samplerAnisotropy)
    //  {
    //      // Use max. level of anisotropy for this example
    //      sampler.maxAnisotropy = _MIN(_MIN(samplerState.fMaxAnisotropy, 1.0f), pVulkanDevice->m_Properties.limits.maxSamplerAnisotropy);
    //      sampler.anisotropyEnable = VK_TRUE;
    //  }
    //  sampler.minLod = samplerState.fToMinLod;
    //  sampler.maxLod = samplerState.fToMaxLod;
    //  sampler.borderColor = GetSamplerBorderColor(samplerState.enuBorderColor);

    //  hRetCode = vks::vkCreateSampler(pDevice, &sampler, nullptr, &m_pSampler);
    //  KGLOG_COM_PROCESS_ERROR(hRetCode);

    //  cmd = pVulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true, gfx::FOR_GRPAHIC);
    //  KGLOG_PROCESS_ERROR(cmd);

    //  subresourceRange = imageView.subresourceRange;
    //  subresourceRange.aspectMask = GetImageAspectMask(vkImageFormat);

    //  if (bColorAttach)
    //  {
    //      m_Descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    //      vks::tools::SetImageLayout(
    //          cmd,
    //          m_pImage,
    //          VK_IMAGE_LAYOUT_UNDEFINED,
    //          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //          subresourceRange
    //      );
    //  }
    //  else
    //  {
    //      m_Descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    //      vks::tools::SetImageLayout(
    //          cmd,
    //          m_pImage,
    //          VK_IMAGE_LAYOUT_UNDEFINED,
    //          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    //          subresourceRange
    //      );
    //  }

    //  pVulkanDevice->FlushCommandBuffer(cmd, gfx::FOR_GRPAHIC, true, true);

    //  m_Descriptor.imageView = m_pView;
    //  m_Descriptor.sampler = m_pSampler;
    //  m_bCreated = true;
    //  bRet = true;
    // Exit0:
    //  return bRet;
    //}

    static KGfxAccess GfxImageLayoutToAccess(enumImageLayout layout)
    {
        switch (layout)
        {
        case IMAGE_LAYOUT_UNDEFINED:
            return KGfxAccess::Unknown;
            break;
        case IMAGE_LAYOUT_GENERAL:
            return KGfxAccess::UAVMask;
            break;
        case IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return KGfxAccess::RTV;
            break;
        case IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return KGfxAccess::DSVWrite;
            break;
        case IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return KGfxAccess::DSVRead;
            break;
        case IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return KGfxAccess::SRVMask;
            break;
        case IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return KGfxAccess::CopySrc;
            break;
        case IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return KGfxAccess::CopyDst;
            break;
        case IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return KGfxAccess::Present;
            break;
        case IMAGE_LAYOUT_SHARED_PRESENT_KHR:
            assert(false);
            return KGfxAccess::Present;
            break;
        default:
            return KGfxAccess::Unknown;
        }
    }

    BOOL KVulkanRenderTarget2D::Create(const KRenderTargetDesc* pDesc, BOOL bTileOptimize)
    {
        BOOL                bRet          = false;
        BOOL                bRetCode      = FALSE;
        VkResult            hRetCode      = VK_INCOMPLETE;
        BOOL                bColorAttach  = false;
        BOOL                bDepth        = false;
        BOOL                bStencil      = false;
        uint32_t            byteStride    = 0;
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        gfx::IKGFX_TextureResource* pCreatedTexture = nullptr;

        auto* pGraphicDevice = gfx::KGFX_GetGraphicDevice();
        CHECK_ASSERT(pGraphicDevice);

        VkFormat vkImageFormat = GetTextureFormatFromTargetFormat(pDesc->eFormat, bColorAttach, bDepth, bStencil, byteStride);
        m_bForDepth            = bColorAttach ? FALSE : TRUE;

        mDesc = *pDesc;
        CHECK_ASSERT(mDesc.m_szRTName[0] != '\0');

        if (pDesc->eFormat == TEX_FORMAT_D32_SFLOAT_S8_UINT || pDesc->eFormat == TEX_FORMAT_D24_UNORM_S8_UINT)
        {
            m_bHasStencil = true;
        }
        else
        {
            m_bHasStencil = false;
        }

        BOOL          cubemapRequired  = false;
        uint32_t      depthOrArraySize = 0;
        uint32_t      numRTVs          = 0;
        BOOL          uavRequired      = false;
        VkImageLayout imageLayout      = VK_IMAGE_LAYOUT_UNDEFINED;

        // RenderTarget must have a name to generate hash value!
        CHECK_ASSERT(m_pTextureResource == nullptr);

        if (mDesc.uDepth > 1)
            mDesc.eDimension = TextureDimensionType::Texture3D;
        else if (mDesc.uHeight > 1)
            mDesc.eDimension = TextureDimensionType::Texture2D;
        else
            mDesc.eDimension = TextureDimensionType::Texture1D;

        mDesc.memoryType = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;

        if ((mDesc.uUsageFlags & TextureUsageFlagBits::TEXTURE_USAGE_STORAGE_BIT) > 0)
        {
            uavRequired = true;
        }

        mDesc.uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_SAMPLED_BIT | TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_SRC_BIT | TextureUsageFlagBits::TEXTURE_USAGE_TRANSFER_DST_BIT | TextureUsageFlagBits::TEXTURE_USAGE_INPUT_ATTACHMENT_BIT;

        if (bColorAttach)
            mDesc.uUsageFlags |= 0TextureUsageFlagBits::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
        else
            mDesc.uUsageFlags |= TextureUsageFlagBits::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        if (mDesc.cpNativeHandle)
        {
            m_bOwnsImage = FALSE;

            m_pTextureResource = new KVulkanTexture();
            CHECK_ASSERT(m_pTextureResource != nullptr);

            // 直接持有外部创建的VkImage（例如Swapchain）
            m_pTextureResource->m_pvkTextureImage = (VkImage)mDesc.cpNativeHandle;

            m_pTextureResource->m_texDesc      = mDesc;
            m_pTextureResource->m_uAspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;

            m_pTextureResource->SetDebugName(mDesc.m_szRTName);

            depthOrArraySize = 1;
        }
        else
        {
            m_bOwnsImage = TRUE;

            KGFX_TextureDesc texDesc = mDesc;

            bRetCode = pGraphicDevice->CreateTexture(texDesc, mDesc.m_szRTName, &pCreatedTexture);
            KGLOG_ASSERT_EXIT(bRetCode);

            m_pTextureResource = (KVulkanTexture*)pCreatedTexture;
            pCreatedTexture    = nullptr;

            depthOrArraySize = mDesc.uArraySize;
        }

        /************************************************************************/
        /*create image view                                                     */
        /************************************************************************/
        {
            KGFX_TextureViewDesc srvDesc = RHIHelper::InitTexture2DViewDesc_SRV(mDesc.eFormat);
            KGFX_TextureViewDesc uavDesc = RHIHelper::InitTexture2DViewDesc_UAV(mDesc.eFormat);
            KGFX_TextureViewDesc rtvDesc;

            rtvDesc.eFormat   = mDesc.eFormat;
            rtvDesc.eViewType = bColorAttach ? gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_RTV : gfx::KGfxResourceViewType::RESOURCE_VIEW_TYPE_DSV;

            if (mDesc.eDimension == TextureDimensionType::TextureCube)
            {
                srvDesc.eViewDimension = mDesc.uArraySize > 6 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
                uavDesc.eViewDimension = mDesc.uArraySize > 6 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
                rtvDesc.eViewDimension = mDesc.uArraySize > 6 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURECUBE;
            }
            else if (mDesc.eDimension == TextureDimensionType::Texture3D)
            {
                srvDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
                uavDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D;
                rtvDesc.eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE3D; // TODO: maybe not supported.
            }
            else if (mDesc.eDimension == TextureDimensionType::Texture2D)
            {
                srvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
                uavDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
                rtvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
            }
            else
            {
                srvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
                uavDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
                rtvDesc.eViewDimension = mDesc.uArraySize > 1 ? ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D_ARRAY : ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE1D;
            }

            bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, srvDesc, (IKGFX_TextureView**)&m_pFullSRV);
            KGLOG_ASSERT_EXIT(bRetCode);

            bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, rtvDesc, (IKGFX_TextureView**)&m_pFullRTVorDSV);
            KGLOG_ASSERT_EXIT(bRetCode);

            if (uavRequired)
            {
                bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, uavDesc, (IKGFX_TextureView**)&m_pFullUAV);
                KGLOG_ASSERT_EXIT(bRetCode);
            }

            numRTVs = mDesc.uMipLevels * depthOrArraySize;
            m_MipmapSRVs.resize(numRTVs, nullptr);
            m_MipmapRTVorDSVs.resize(numRTVs, nullptr);

            if (uavRequired)
                m_MipmapUAVs.resize(numRTVs, nullptr);

            if (depthOrArraySize > 1) // image type is texture array
            {
                ASSERT(mDesc.uMipLevels == 1);

                for (uint32_t i = 0; i < depthOrArraySize; ++i)
                {
                    srvDesc.sSubresourceRange.uBaseArraySlice = i;
                    srvDesc.sSubresourceRange.uArrayCount = 1;
                    srvDesc.sSubresourceRange.uBaseMipLevel   = 0;
                    srvDesc.sSubresourceRange.uMipCount   = 1;

                    uavDesc.sSubresourceRange = srvDesc.sSubresourceRange;
                    rtvDesc.sSubresourceRange = srvDesc.sSubresourceRange;

                    bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, srvDesc, (IKGFX_TextureView**)&(m_MipmapSRVs[i]));
                    KGLOG_ASSERT_EXIT(bRetCode);

                    bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, rtvDesc, (IKGFX_TextureView**)&(m_MipmapRTVorDSVs[i]));
                    KGLOG_ASSERT_EXIT(bRetCode);

                    if (uavRequired)
                    {
                        bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, uavDesc, (IKGFX_TextureView**)&(m_MipmapUAVs[i]));
                        KGLOG_ASSERT_EXIT(bRetCode);
                    }
                }
            }
            else // if (mDesc.uMipLevels > 1)
            {
                for (uint32_t i = 0; i < mDesc.uMipLevels; ++i)
                {
                    srvDesc.sSubresourceRange.uBaseArraySlice = 0;
                    srvDesc.sSubresourceRange.uArrayCount = 1;
                    srvDesc.sSubresourceRange.uBaseMipLevel   = i;
                    srvDesc.sSubresourceRange.uMipCount   = 1;

                    uavDesc.sSubresourceRange = srvDesc.sSubresourceRange;
                    rtvDesc.sSubresourceRange = srvDesc.sSubresourceRange;

                    bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, srvDesc, (IKGFX_TextureView**)&(m_MipmapSRVs[i]));
                    KGLOG_ASSERT_EXIT(bRetCode);

                    bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, rtvDesc, (IKGFX_TextureView**)&(m_MipmapRTVorDSVs[i]));
                    KGLOG_ASSERT_EXIT(bRetCode);

                    if (uavRequired)
                    {
                        bRetCode = pGraphicDevice->CreateTextureView(m_pTextureResource, uavDesc, (IKGFX_TextureView**)&(m_MipmapUAVs[i]));
                        KGLOG_ASSERT_EXIT(bRetCode);
                    }
                }
            }
        }

        m_uTextureUpdateCode = (uint32_t)(this->GetId() + 1);
        bRet                 = true;
    Exit0:
        SAFE_RELEASE(pCreatedTexture);
        return bRet;
    }

    BOOL _Blit(VkFormat vkSrcFormat, VkImage srcImage, KBlitRegion blitSrc, VkFormat vkDstFormat, VkImage dstImage, KBlitRegion blitDst, bool fromSwapChain)
    {
        BOOL                bRet           = false;
        VkDevice            pDevice        = GetVkDevice();
        vks::KVulkanDevice* pVulkanDevice  = GetVulkanDevice();
        KGraphicDevice*     pGraphicDevice = GetGraphicDevice();

        KGLOG_PROCESS_ERROR(srcImage && dstImage);
        {
            bool               supportsBlit = true;
            VkFormatProperties formatProps;

            // Check if the device supports blitting from optimal images (the swapchain images are in optimal format)
            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, vkSrcFormat, &formatProps);
            if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            {
                supportsBlit = false;
            }

            // Check if the device supports blitting to linear images
            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, vkDstFormat, &formatProps);
            if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            {
                supportsBlit = false;
            }

            VkCommandBuffer copyCmd = pVulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true, gfx::FOR_GRPAHIC);
            // KVulkanCommandBuffer* pCommond = pGraphicDevice->GetMainCommandBuffer(gfx::MAIN_CONTEXT);
            // VkCommandBuffer copyCmd = (VkCommandBuffer)pCommond->GetCommandPtr();

            // Transition destination image to transfer destination layout
            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                dstImage,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );

            // Transition swapchain image from present to transfer source layout
            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                srcImage,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                fromSwapChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // if swapchan VK_IMAGE_LAYOUT_PRESENT_SRC_KHR else VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );

            /*vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                srcImage,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                fromSwapChain ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // if swapchan VK_IMAGE_LAYOUT_PRESENT_SRC_KHR else VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                fromSwapChain ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );*/

            // If source and destination support blit we'll blit as this also does automatic format conversion (e.g. from BGR to RGB)
            if (supportsBlit)
            {
                VkOffset3D srcOffset;
                srcOffset.x = blitSrc.uX;
                srcOffset.y = blitSrc.uY;
                srcOffset.z = 0;

                VkOffset3D srcBlitSize;
                srcBlitSize.x = blitSrc.uWidth;
                srcBlitSize.y = blitSrc.uHeight;
                srcBlitSize.z = 1;

                VkOffset3D destOffset;
                destOffset.x = blitDst.uX;
                destOffset.y = blitDst.uY;
                destOffset.z = 0;

                VkOffset3D destBlitSize;
                destBlitSize.x = blitDst.uWidth;
                destBlitSize.y = blitDst.uHeight;
                destBlitSize.z = 1;

                VkImageBlit imageBlitRegion{};
                imageBlitRegion.srcSubresource.aspectMask = GetImageAspectMask(vkSrcFormat);
                imageBlitRegion.srcSubresource.layerCount = 1;
                // imageBlitRegion.srcOffsets[0] = srcOffset;
                imageBlitRegion.srcOffsets[1]             = srcBlitSize;
                imageBlitRegion.dstSubresource.aspectMask = GetImageAspectMask(vkDstFormat);
                imageBlitRegion.dstSubresource.layerCount = 1;
                // imageBlitRegion.dstOffsets[0] = destOffset;
                imageBlitRegion.dstOffsets[1]             = destBlitSize;

                // Issue the blit command
                vks::vkCmdBlitImage(
                    copyCmd,
                    srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &imageBlitRegion,
                    VK_FILTER_NEAREST
                );
            }
            else
            {
                // Otherwise use image copy (requires us to manually flip components)
                VkImageCopy imageCopyRegion{};
                imageCopyRegion.srcSubresource.aspectMask = GetImageAspectMask(vkSrcFormat);
                imageCopyRegion.srcSubresource.layerCount = 1;
                imageCopyRegion.dstSubresource.aspectMask = GetImageAspectMask(vkDstFormat);
                imageCopyRegion.dstSubresource.layerCount = 1;
                imageCopyRegion.extent.width              = blitSrc.uWidth;
                imageCopyRegion.extent.height             = blitSrc.uHeight;
                imageCopyRegion.extent.depth              = 1;

                // Issue the copy command
                vks::vkCmdCopyImage(
                    copyCmd,
                    srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &imageCopyRegion
                );
            }

            // Transition destination image to general layout, which is the required layout for mapping the image memory later on
            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                dstImage,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );


            // Transition back the swap chain image after the blit is done
            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                srcImage,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                fromSwapChain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // if swapchan VK_IMAGE_LAYOUT_PRESENT_SRC_KHR else VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );

            /*vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                srcImage,
                VK_ACCESS_TRANSFER_READ_BIT,
                fromSwapChain ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                fromSwapChain ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // if swapchan VK_IMAGE_LAYOUT_PRESENT_SRC_KHR else VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );*/

            pVulkanDevice->FlushCommandBuffer(copyCmd, gfx::FOR_GRPAHIC, TRUE, TRUE);
        }
        bRet = true;
    Exit0:
        return bRet;
    }


    BOOL KVulkanRenderTarget2D::ReadPixels(void* pBytes, uint32_t ubytes)
    {
        return false;
    }

    BOOL KVulkanRenderTarget2D::Blit(
        KRenderTarget* pSrcRT, KBlitRegion blitSrc,
        KBlitRegion           blitDest,
        gfx::IKGFX_RenderContext* pCommandBuffer
    )
    {
        BOOL bRet = FALSE;

        BOOL                bColorAttach   = false;
        BOOL                bDepth         = false;
        BOOL                bStencil       = false;
        uint32_t            byteStride     = 0;
        VkDevice            pDevice        = GetVkDevice();
        vks::KVulkanDevice* pVulkanDevice  = GetVulkanDevice();
        KGraphicDevice*     pGraphicDevice = GetGraphicDevice();
        VkFormat            vkImageFormat  = GetTextureFormatFromTargetFormat(mDesc.eFormat, bColorAttach, bDepth, bStencil, byteStride);
        VkImage             srcImage       = nullptr;
        VkImage             dstImage       = nullptr;
        // blit image from source render target
        if (pSrcRT)
        {
            BOOL     bColorAttach = false;
            uint32_t byteStride   = 0;

            bool supportsBlit = true;

            KVulkanRenderTarget2D* pSrcVkTarget = (KVulkanRenderTarget2D*)pSrcRT;

            VkFormat vkSrcFormat = GetTextureFormatFromTargetFormat(pSrcVkTarget->GetDesc().eFormat, bColorAttach, bDepth, bStencil, byteStride);

            // Check blit support for source and destination
            VkFormatProperties formatProps;
            // Check if the device supports blitting from optimal images (the swapchain images are in optimal format)
            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, vkSrcFormat, &formatProps);
            if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            {
                supportsBlit = false;
            }

            // Check if the device supports blitting to linear images
            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, vkImageFormat, &formatProps);
            if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            {
                supportsBlit = false;
            }
            if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            {
                supportsBlit = false;
            }


            srcImage = (VkImage)pSrcVkTarget->GetNativeImageHandle();

            dstImage = m_pTextureResource->m_pvkTextureImage;

            VkCommandBuffer copyCmd = nullptr;

            if (pCommandBuffer)
            {
                copyCmd = (VkCommandBuffer)pCommandBuffer->GetCommandBufferNativeHandle();
            }
            else
            {
                copyCmd = pVulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true, gfx::FOR_GRPAHIC);
            }

            VkOffset3D destOffset;
            destOffset.x = blitDest.uX;
            destOffset.y = blitDest.uY;
            destOffset.z = 0;

            VkOffset3D srcOffset;
            srcOffset.x = blitSrc.uX;
            srcOffset.y = blitSrc.uY;
            srcOffset.z = 0;

            VkImageSubresourceRange imageSubresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (pSrcRT->IsForDepth())
            {
                imageSubresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                if (pSrcRT->IsHasStencil())
                {
                    imageSubresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
            }


            // src image change layout to src_optimal

            // vks::tools::InsertImageMemoryBarrier(copyCmd,
            //   srcImage,
            //   0,
            //   VK_ACCESS_TRANSFER_READ_BIT,
            //   VK_IMAGE_LAYOUT_UNDEFINED,
            //   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            //   VK_PIPELINE_STAGE_TRANSFER_BIT,
            //   VK_PIPELINE_STAGE_TRANSFER_BIT,
            //   imageSubresourceRange
            //);

            //// dest image change layout to dest_optimal
            // vks::tools::InsertImageMemoryBarrier(copyCmd,
            //   dstImage,
            //   0,
            //   VK_ACCESS_TRANSFER_WRITE_BIT,
            //   VK_IMAGE_LAYOUT_UNDEFINED,
            //   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            //   VK_PIPELINE_STAGE_TRANSFER_BIT,
            //   VK_PIPELINE_STAGE_TRANSFER_BIT,
            //   imageSubresourceRange
            //);


            if (pSrcRT->IsForDepth())
            {
                // src
                vks::tools::SetImageLayout(
                    copyCmd,
                    srcImage,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    imageSubresourceRange
                );

                // dst
                vks::tools::SetImageLayout(
                    copyCmd,
                    dstImage,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    imageSubresourceRange
                );
            }
            else
            {
                // src
                vks::tools::SetImageLayout(
                    copyCmd,
                    srcImage,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    imageSubresourceRange
                );

                // dst
                vks::tools::SetImageLayout(
                    copyCmd,
                    dstImage,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    imageSubresourceRange
                );
            }

            // supportsBlit = true;
            if (supportsBlit)
            {
                // Define the region to blit (we will blit the whole swapchain image)
                VkOffset3D destBlitSize;
                destBlitSize.x = blitDest.uWidth;
                destBlitSize.y = blitDest.uHeight;
                destBlitSize.z = 1;

                VkOffset3D srcBlitSize;
                srcBlitSize.x = blitSrc.uWidth;
                srcBlitSize.y = blitSrc.uHeight;
                srcBlitSize.z = 1;


                VkImageBlit imageBlitRegion{};
                imageBlitRegion.srcSubresource.aspectMask = GetImageAspectMask(vkSrcFormat);
                imageBlitRegion.srcSubresource.layerCount = 1;
                // imageBlitRegion.srcOffsets[0] = srcOffset;
                imageBlitRegion.srcOffsets[1]             = srcBlitSize;
                imageBlitRegion.dstSubresource.aspectMask = GetImageAspectMask(vkImageFormat);
                imageBlitRegion.dstSubresource.layerCount = 1;
                // imageBlitRegion.dstOffsets[0] = destOffset;
                imageBlitRegion.dstOffsets[1]             = destBlitSize;

                if (IsForDepth())
                {
                    vks::vkCmdBlitImage(
                        copyCmd,
                        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &imageBlitRegion,
                        VK_FILTER_NEAREST
                    );
                }
                else
                {
                    // Issue the blit command
                    vks::vkCmdBlitImage(
                        copyCmd,
                        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &imageBlitRegion,
                        VK_FILTER_LINEAR
                    );
                }
            }
            else
            {
                // Otherwise use image copy (requires us to manually flip components)
                VkImageCopy imageCopyRegion{};
                imageCopyRegion.srcSubresource.aspectMask = GetImageAspectMask(vkSrcFormat);
                imageCopyRegion.srcSubresource.layerCount = 1;
                imageCopyRegion.dstSubresource.aspectMask = GetImageAspectMask(vkImageFormat);
                imageCopyRegion.dstSubresource.layerCount = 1;
                imageCopyRegion.extent.width              = blitSrc.uWidth;
                imageCopyRegion.extent.height             = blitSrc.uHeight;
                imageCopyRegion.extent.depth              = 1;
                // imageCopyRegion.srcOffset = srcOffset;
                // imageCopyRegion.dstOffset = destOffset;

                // Issue the copy command
                vks::vkCmdCopyImage(
                    copyCmd,
                    srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &imageCopyRegion
                );
            }

            if (pSrcRT->IsForDepth())
            {
                // Transform framebuffer color attachment back
                vks::tools::SetImageLayout(
                    copyCmd,
                    srcImage,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    imageSubresourceRange
                );


                // Change image layout of copied face to shader read
                vks::tools::SetImageLayout(
                    copyCmd,
                    dstImage,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    imageSubresourceRange
                );
            }
            else
            {
                // Transform framebuffer color attachment back
                vks::tools::SetImageLayout(
                    copyCmd,
                    srcImage,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    imageSubresourceRange
                );


                // Change image layout of copied face to shader read
                vks::tools::SetImageLayout(
                    copyCmd,
                    dstImage,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    imageSubresourceRange
                );
            }

            if (!pCommandBuffer)
            {
                pVulkanDevice->FlushCommandBuffer(copyCmd, gfx::FOR_GRPAHIC, TRUE, TRUE);
            }
        }

        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    void* KVulkanRenderTarget2D::GetNativeImageHandle() const
    {
        return (void*)m_pTextureResource->GetNativeResourceHandle();
    }

    IKGFX_TextureResource* KVulkanRenderTarget2D::GetTextureResource() const
    {
        return m_pTextureResource;
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetSRV() const
    {
        return GetFullSRV();
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetUAV() const
    {
        return GetFullUAV();
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetFullRTV() const
    {
        return m_bForDepth || m_bHasStencil ? nullptr : m_pFullRTVorDSV;
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetFullDSV() const
    {
        return m_bForDepth || m_bHasStencil ? m_pFullRTVorDSV : nullptr;
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetFullSRV() const
    {
        return m_pFullSRV;
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetFullUAV() const
    {
        return m_pFullUAV;
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetMipSRV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_pTextureResource)
        {
            auto texDesc = m_pTextureResource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapSRVs.size());

            return m_MipmapSRVs[Index];
        }
        else
        {
            return nullptr;
        }
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetMipUAV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_pTextureResource)
        {
            auto texDesc = m_pTextureResource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapUAVs.size());

            return m_MipmapUAVs[Index];
        }
        else
        {
            return nullptr;
        }
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetMipRTV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_pTextureResource && !(m_bForDepth || m_bHasStencil))
        {
            auto texDesc = m_pTextureResource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapRTVorDSVs.size());

            return m_MipmapRTVorDSVs[Index];
        }
        else
        {
            return nullptr;
        }
    }

    IKGFX_TextureView* KVulkanRenderTarget2D::GetMipDSV(uint32_t MipLevel, uint32_t uArraySlice) const
    {
        if (m_pTextureResource && (m_bForDepth || m_bHasStencil))
        {
            auto texDesc = m_pTextureResource->GetDesc();

            CHECK_ASSERT(MipLevel < texDesc->uMipLevels);
            CHECK_ASSERT(uArraySlice < texDesc->uArraySize);

            uint32_t Index = uArraySlice * texDesc->uMipLevels + MipLevel;
            CHECK_ASSERT(Index < m_MipmapRTVorDSVs.size());

            return m_MipmapRTVorDSVs[Index];
        }
        else
        {
            return nullptr;
        }
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanLayout::KVulkanLayout()
    {
        m_pPipelineLayout = nullptr;
    }

    KVulkanLayout::~KVulkanLayout()
    {
        Destroy();
    }

    BOOL KVulkanLayout::Destroy()
    {
        BOOL bRet = FALSE;

        VkDevice pDevice = GetVkDevice();
        for (auto& it : m_vecLayoutSet)
        {
            _LayoutSet& layoutSet = it;
            if (layoutSet.m_pDescriptorSetLayout)
            {
                vks::vkDestroyDescriptorSetLayout(pDevice, layoutSet.m_pDescriptorSetLayout, nullptr);
                layoutSet.m_pDescriptorSetLayout = nullptr;
            }
            layoutSet.m_vecDescriptorSetLayoutBinding.clear();
        }
        m_vecLayoutSet.clear();

        if (m_pPipelineLayout)
        {
            vks::vkDestroyPipelineLayout(pDevice, m_pPipelineLayout, nullptr);
            m_pPipelineLayout = VK_NULL_HANDLE;
        }
        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    KVulkanLayout& KVulkanLayout::Begin()
    {
        VkDevice pDevice = GetVkDevice();
        for (auto& it : m_vecLayoutSet)
        {
            _LayoutSet& layoutSet = it;
            if (layoutSet.m_pDescriptorSetLayout)
            {
                vks::vkDestroyDescriptorSetLayout(pDevice, layoutSet.m_pDescriptorSetLayout, nullptr);
                layoutSet.m_pDescriptorSetLayout = nullptr;
            }
            layoutSet.m_vecDescriptorSetLayoutBinding.clear();
        }
        m_vecLayoutSet.clear();

        if (m_pPipelineLayout)
        {
            vks::vkDestroyPipelineLayout(pDevice, m_pPipelineLayout, nullptr);
        }

        m_vecPushConstantRange.clear();
        return *this;
    }

    KVulkanLayout& KVulkanLayout::AddLayout(uint32_t uSet, enumDescriptorType descriptorType, gfx::ShaderStageType shaderType, uint32_t binding, uint32_t descriptorCount /*= 1*/)
    {
        if (uSet >= m_vecLayoutSet.size())
        {
            m_vecLayoutSet.resize(uSet + 1);
        }

        // if (!DrvOption::bSupportDynamicUBO && descriptorType == gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
        //{
        //     descriptorType = gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // }

        // if (!DrvOption::bSupportDynamicSSBO && descriptorType == gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
        //{
        //     descriptorType = gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER;
        // }

        m_vecLayoutSet[uSet].m_vecDescriptorSetLayoutBinding.push_back(vks::initializers::DescriptorSetLayoutBinding(GetDescriptorType(descriptorType), GetShaderStageFlag(shaderType), binding, descriptorCount));

        return *this;
    }

    KVulkanLayout& KVulkanLayout::AddBindlessLayout(uint32_t uSet, enumDescriptorType descriptorType, gfx::ShaderStageType shaderType, uint32_t binding, uint32_t maxDescriptorCount)
    {
        CHECK_ASSERT(IS_BINDLESS_ENABLED);
        if (uSet >= m_vecLayoutSet.size())
        {
            m_vecLayoutSet.resize(uSet + 1);
            m_vecLayoutSet[uSet].bBindless = true;
        }

        CHECK_ASSERT(m_vecLayoutSet[uSet].bBindless); // 为了简化逻辑，bindless的set的所有binding都必须是bindless的


        if (!DrvOption::bSupportDynamicUBO && descriptorType == gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
        {
            descriptorType = gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }

        if (!DrvOption::bSupportDynamicSSBO && descriptorType == gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
        {
            descriptorType = gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        m_vecLayoutSet[uSet].m_vecDescriptorSetLayoutBinding.push_back(vks::initializers::DescriptorSetLayoutBinding(GetDescriptorType(descriptorType), GetShaderStageFlag(shaderType), binding, maxDescriptorCount));
        m_vecLayoutSet[uSet].m_vecBindingFlags.push_back(VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT);
        return *this;
    }

    KVulkanLayout& KVulkanLayout::AddCombinedLayout(uint32_t uSet, gfx::enumDescriptorType descriptorType, gfx::ShaderStageType shaderType, uint32_t binding, uint32_t descriptorCount, gfx::IKGFX_Sampler** pImmutableSamplers)
    {
        if (uSet >= m_vecLayoutSet.size())
        {
            m_vecLayoutSet.resize(uSet + 1);
        }

        if (!DrvOption::bSupportDynamicUBO && descriptorType == gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
        {
            descriptorType = gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }

        if (!DrvOption::bSupportDynamicSSBO && descriptorType == gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
        {
            descriptorType = gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        m_vecLayoutSet[uSet].m_vecDescriptorSetLayoutBinding.push_back(vks::initializers::DescriptorSetLayoutBinding(GetDescriptorType(descriptorType), GetShaderStageFlag(shaderType), binding, descriptorCount));

        if (pImmutableSamplers)
        {
            KVulkanSampler* pSampler                                                       = (KVulkanSampler*)*pImmutableSamplers;
            m_vecLayoutSet[uSet].m_vecDescriptorSetLayoutBinding.back().pImmutableSamplers = pSampler->GetVkSamplerPtr();
        }

        return *this;
    }

    KVulkanLayout& KVulkanLayout::AddPushContantRange(gfx::ShaderStageType shaderType, size_t size, uint32_t offset)
    {
        // Any two elements of pPushConstantRanges must not include the same stage in stageFlags, so should combine ranges by shader type
        // BOOL bFind = false;
        // uint32_t count = (uint32_t)m_vecPushConstantRange.size();
        // for (uint32_t i = 0; i < count; ++i)
        //{
        //   VkPushConstantRange& range = m_vecPushConstantRange[i];
        //   if (range.stageFlags == GetShaderStageFlag(shaderType))
        //   {
        //       range.size += (uint32_t)size;
        //       bFind = true;
        //   }
        // }
        // if (!bFind)
        //{
        //   m_vecPushConstantRange.push_back(vks::initializers::PushConstantRange(GetShaderStageFlag(shaderType), (uint32_t)size, 0));
        // }

        // count = (uint32_t)m_vecPushConstantRange.size();
        // uint32_t sumsize = 0;
        // for (uint32_t i = 0; i < count; ++i)
        //{
        //   m_vecPushConstantRange[i].offset = sumsize;
        //   sumsize += m_vecPushConstantRange[i].size;
        // }

        m_vecPushConstantRange.push_back(vks::initializers::PushConstantRange(GetShaderStageFlag(shaderType), (uint32_t)size, offset));

        return *this;
    }

    BOOL KVulkanLayout::End(bool bCreatePipelineLayout)
    {
        BOOL                               bRet     = false;
        VkResult                           hRetCode = VK_INCOMPLETE;
        VkDevice                           pDevice  = GetVkDevice();
        VkPipelineLayoutCreateInfo         pipelineLayoutCreateInfo{};
        std::vector<VkDescriptorSetLayout> vecDescriptorSetLayout;

        for (auto& it : m_vecLayoutSet)
        {
            _LayoutSet& layoutSet = it;
#if MERGE_UNIFORM_BINDING
            std::vector<VkDescriptorSetLayoutBinding> vecMergeBinding;

            for (auto& itt : layoutSet.m_vecDescriptorSetLayoutBinding)
            {
                VkDescriptorSetLayoutBinding& item  = itt;
                BOOL                          bFind = false;
                size_t                        n     = vecMergeBinding.size();
                for (int i = 0; i < n; ++i)
                {
                    if (vecMergeBinding[i].binding == item.binding)
                    {
                        vecMergeBinding[i].stageFlags |= item.stageFlags;
                        bFind                          = true;
                        break;
                    }
                }

                if (!bFind)
                {
                    vecMergeBinding.push_back(item);
                }
            }

            std::sort(vecMergeBinding.begin(), vecMergeBinding.end(), [](VkDescriptorSetLayoutBinding& l, VkDescriptorSetLayoutBinding& r) -> bool { return l.binding < r.binding; });
            VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::DescriptorSetLayoutCreateInfo(vecMergeBinding.data(), static_cast<uint32_t>(vecMergeBinding.size()));
#else
            VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::DescriptorSetLayoutCreateInfo(layoutSet.m_vecDescriptorSetLayoutBinding.data(), static_cast<uint32_t>(layoutSet.m_vecDescriptorSetLayoutBinding.size()));
#endif
            if (layoutSet.bBindless)
            {
                CHECK_ASSERT(IS_BINDLESS_ENABLED);
                descriptorLayout.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
                VkDescriptorSetLayoutBindingFlagsCreateInfoEXT binding_flags{};
                binding_flags.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
                binding_flags.bindingCount  = (uint32_t)layoutSet.m_vecBindingFlags.size();
                binding_flags.pBindingFlags = layoutSet.m_vecBindingFlags.data();
                binding_flags.pNext         = descriptorLayout.pNext;
                descriptorLayout.pNext      = &binding_flags;
            }
            hRetCode = vks::vkCreateDescriptorSetLayout(pDevice, &descriptorLayout, nullptr, &layoutSet.m_pDescriptorSetLayout);
            KGLOG_COM_PROCESS_ERROR(hRetCode);
            vecDescriptorSetLayout.push_back(layoutSet.m_pDescriptorSetLayout);
        }
        if (bCreatePipelineLayout)
        {
            pipelineLayoutCreateInfo = vks::initializers::PipelineLayoutCreateInfo(vecDescriptorSetLayout.data(), (uint32_t)vecDescriptorSetLayout.size());

            if (!m_vecPushConstantRange.empty())
            {
                pipelineLayoutCreateInfo.pushConstantRangeCount = (uint32_t)m_vecPushConstantRange.size();
                pipelineLayoutCreateInfo.pPushConstantRanges = m_vecPushConstantRange.data();
            }
            hRetCode = vks::vkCreatePipelineLayout(pDevice, &pipelineLayoutCreateInfo, nullptr, &m_pPipelineLayout);
            KGLOG_COM_PROCESS_ERROR(hRetCode);

        }
       
        bRet = true;
    Exit0:
        return bRet;
    }


    VkPipelineLayout KVulkanLayout::GetPipelineLayout() const
    {
        return m_pPipelineLayout;
    }

    BOOL KVulkanLayout::IsReady() const
    {
        // ASSERT(m_vecLayoutSet.size() > 0);
        // if (m_pPipelineLayout && m_vecLayoutSet.size() > 0)
        //     return true;
        // else
        //     return false;
        return m_pPipelineLayout != nullptr;
    }
    BOOL KVulkanLayout::IsDynamic(uint32_t uSet, uint32_t binding) const
    {
        BOOL bRet = false;
        for (const auto& it : m_vecLayoutSet[uSet].m_vecDescriptorSetLayoutBinding)
        {
            if (it.binding == binding &&
                (it.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || it.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC))
            {
                bRet = true;
            }
        }
        return bRet;
    }

    VkDescriptorSetLayout KVulkanLayout::GetDesriptorSetLayout(uint32_t uSet) const
    {
        if (uSet >= (uint32_t)m_vecLayoutSet.size())
        {
            KGLogPrintf(KGLOG_ERR, "error layoutSet id");
            return nullptr;
        }
        return m_vecLayoutSet[uSet].m_pDescriptorSetLayout;
    }

    BOOL KVulkanLayout::IsBindless(uint32_t uSet) const
    {
        return m_vecLayoutSet[uSet].bBindless;
    }

    uint32_t KVulkanLayout::GetLayoutSetCount() const
    {
        return (uint32_t)m_vecLayoutSet.size();
    }

    ////////////////////////////////////////////////////////////////////
    std::set<KVulkanDescriptorPool*> KVulkanDescriptorPool::m_dirtyPool;

    BOOL KVulkanDescriptorPool::IsDirtyDescriptorPool(KVulkanDescriptorPool* pPool)
    {
        if (m_dirtyPool.empty())
        {
            return false;
        }
        auto it = m_dirtyPool.find(pPool);
        if (it != m_dirtyPool.end())
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    KVulkanDescriptorPool* KVulkanDescriptorPool::GetNext()
    {
        return this->m_pNextPool;
    }

    KVulkanDescriptorPool::KVulkanDescriptorPool()
    {
        m_pDescriptorPool = nullptr;
        m_pNextPool       = nullptr;
        m_pPreviousPool   = nullptr;
    }

    KVulkanDescriptorPool::~KVulkanDescriptorPool()
    {
        VkDevice pDevice = GetVkDevice();
        if (m_uAllocedSet > 0)
        {
            KGLogPrintf(KGLOG_WARNING, "some descriptorSet not release");
        }

        if (m_pDescriptorPool)
        {
            vks::vkDestroyDescriptorPool(pDevice, m_pDescriptorPool, nullptr);
        }
        if (m_pNextPool)
        {
            // 递归删除子结点
            SAFE_DELETE(m_pNextPool);
        }
    }

    KVulkanDescriptorPool& KVulkanDescriptorPool::Begin(uint32_t maxSet, bool bBindlessSet)
    {
        VkDevice pDevice = GetVkDevice();
        if (m_pDescriptorPool)
        {
            vks::vkDestroyDescriptorPool(pDevice, m_pDescriptorPool, nullptr);
        }
        m_vecDescriptorPoolSize.clear();
        m_uMaxSet       = maxSet;
        m_uAllocedSet   = 0;
        m_bBindlessPool = bBindlessSet;
        return *this;
    }

    KVulkanDescriptorPool& KVulkanDescriptorPool::AddPoolItem(enumDescriptorType descriptorType, uint32_t uCount)
    {
        if (!DrvOption::bSupportDynamicUBO && descriptorType == gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
        {
            descriptorType = gfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }

        if (!DrvOption::bSupportDynamicSSBO && descriptorType == gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
        {
            descriptorType = gfx::DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        m_vecDescriptorPoolSize.push_back(vks::initializers::DescriptorPoolSize(GetDescriptorType(descriptorType), uCount * m_uMaxSet));
        return *this;
    }

    BOOL KVulkanDescriptorPool::End()
    {
        BOOL     bRet     = false;
        VkResult hRetCode = VK_INCOMPLETE;
        VkDevice pDevice  = GetVkDevice();

        if (m_vecDescriptorPoolSize.empty())
        {
            KGLogPrintf(KGLOG_WARNING, "[KVulkanDescriptorPool::End] descriptor pool is empty");
        }
        else
        {
            VkDescriptorPoolCreateInfo descriptorPoolInfo =
                vks::initializers::DescriptorPoolCreateInfo(
                    (uint32_t)m_vecDescriptorPoolSize.size(),
                    m_vecDescriptorPoolSize.data(),
                    m_uMaxSet
                );

            if (m_bBindlessPool && IS_BINDLESS_ENABLED)
            {
                descriptorPoolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            }
            hRetCode = vks::vkCreateDescriptorPool(pDevice, &descriptorPoolInfo, nullptr, &m_pDescriptorPool);
            KGLOG_COM_PROCESS_ERROR(hRetCode);
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    KVulkanDescriptorPool* KVulkanDescriptorPool::CreateFromHeader()
    {
        PROF_CPU();
        VkDevice               pDevice  = GetVkDevice();
        KVulkanDescriptorPool* pNewPool = new KVulkanDescriptorPool;

        pNewPool->m_vecDescriptorPoolSize.assign(m_vecDescriptorPoolSize.begin(), m_vecDescriptorPoolSize.end());
        pNewPool->m_uMaxSet     = m_uMaxSet;
        pNewPool->m_uAllocedSet = 0;

        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::DescriptorPoolCreateInfo(
                (uint32_t)pNewPool->m_vecDescriptorPoolSize.size(),
                pNewPool->m_vecDescriptorPoolSize.data(),
                pNewPool->m_uMaxSet
            );


        vks::vkCreateDescriptorPool(pDevice, &descriptorPoolInfo, nullptr, &pNewPool->m_pDescriptorPool);
        OPTICK_TAG("DescPoolSize: ", pNewPool->m_uMaxSet);
        return pNewPool;
    }

    BOOL KVulkanDescriptorPool::FreeDescriptorSet(KVulkanDescriptorSet* pDescriptorSet)
    {
        VkDevice pDevice = GetVkDevice();


        KVulkanDescriptorSet*  pVulkanDescriptorSet = (KVulkanDescriptorSet*)pDescriptorSet;
        KVulkanDescriptorPool* pPool                = pVulkanDescriptorSet->m_pRealAllocPool;
        if (pPool && pPool->m_uAllocedSet > 0)
        {
            pPool->m_uAllocedSet--;
            uint32_t uCount = pVulkanDescriptorSet->GetSetCount();
            for (uint32_t i = 0; i < uCount; ++i)
            {
                const VkDescriptorSet ps = pVulkanDescriptorSet->GetDescriptorSet(i);
                if (ps)
                {
                    const VkDescriptorSet* pSet = &ps;
                    vks::vkFreeDescriptorSets(pDevice, pPool->m_pDescriptorPool, 1, pSet);
                }
                pVulkanDescriptorSet->SetDescriptorSet(i, nullptr);
                pVulkanDescriptorSet->Clear();
            }
            pVulkanDescriptorSet->ClearPoolContainer();

            if (pPool->m_uAllocedSet == 0)
            {
                if (pPool->m_pPreviousPool)
                {
                    // 不是头结点就可以干掉了,自动删除子结点回收空间
                    KVulkanDescriptorPool* pPrev = pPool->m_pPreviousPool;
                    KVulkanDescriptorPool* pCur  = pPool;
                    KVulkanDescriptorPool* pNext = pPool->m_pNextPool;

                    pPrev->m_pNextPool = pNext;
                    if (pNext)
                    {
                        pNext->m_pPreviousPool = pPrev;
                    }

                    pCur->m_pNextPool     = nullptr;
                    pCur->m_pPreviousPool = nullptr;
                    delete (pCur);
                }
            }
        }

        return true;
    }

    VkDescriptorPool KVulkanDescriptorPool::GetPool()
    {
        return m_pDescriptorPool;
    }

    void KVulkanDescriptorPool::IncreaseAllocedSet()
    {
        ++m_uAllocedSet;
    }

    BOOL KVulkanDescriptorPool::IsFull()
    {
        if (m_uAllocedSet < m_uMaxSet)
        {
            return false;
        }
        else
        {
            if (m_uAllocedSet > m_uMaxSet)
            {
                ASSERT(0);
            }

            return true;
        }
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanDescriptorSet::KVulkanDescriptorSet(const KVulkanLayout* pLayout, KDescriptorPoolContainer* pPoolContainer)
    {
        m_pContainer = pPoolContainer;
        m_pDescriptorPool = pPoolContainer->GetDescriptorPool();
        ASSERT(pLayout);
        m_pLayout = pLayout;
        pPoolContainer->AddAlloced(this);
        m_uUpdateCheckCode = 0;

        m_pRealAllocPool                         = nullptr;
        m_bHasError                              = false;
        KVulkanDescriptorPool* pVulkanPoolHeader = pPoolContainer->GetDescriptorPool();
        if (pVulkanPoolHeader)
        {
            KVulkanDescriptorPool* pNode = pVulkanPoolHeader;

            KVulkanDescriptorPool* pPreviouseNode = nullptr;
            while (pNode)
            {
                if (!pNode->IsFull())
                {
                    break;
                }
                pPreviouseNode = pNode;
                pNode          = pNode->m_pNextPool;
            }

            if (!pNode)
            {
                pNode = pVulkanPoolHeader->CreateFromHeader();

                if (pPreviouseNode)
                {
                    pPreviouseNode->m_pNextPool = pNode;
                }

                pNode->m_pPreviousPool = pPreviouseNode;
            }

            pNode->IncreaseAllocedSet();
            m_pRealAllocPool = pNode;
        }
    }

    KVulkanDescriptorSet::~KVulkanDescriptorSet()
    {
        // KGLogPrintf(KGLOG_INFO,"delete descriptorset : %p", m_pDescriptorSet);
        if (m_pContainer)
        {
            m_pContainer->Remove(this);
        }
        m_pContainer = nullptr;
    }

    void KVulkanDescriptorSet::FitLayoutItemSize(uint32_t uSet)
    {
        PROF_CPU_DETAIL();
        if (uSet >= m_vecLayoutItem.size())
        {
            m_vecLayoutItem.resize(uSet + 1);
        }
    }

    BOOL KVulkanDescriptorSet::ValidCheck()
    {
        BOOL bRet = true;
#if DESCRIPTORSET_VALIDATE
        for(const auto t : m_vecLayoutItem)
        {
            for (const auto& it : t.m_UBOinfos)
            {
                for (const auto& itt : it.pGfxBuffers)
                {
                    uint32_t id = itt->GetCreateId();
                    if(id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }                
            }
            for (const auto& it : t.m_DynamicUBOinfos)
            {
                for (const auto& itt : it.pGfxBuffers)
                {
                    uint32_t id = itt->GetCreateId();
                    if (id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }
            }
            for (const auto& it : t.m_SSBOinfos)
            {
                for (const auto& itt : it.pGfxBuffers)
                {
                    uint32_t id = itt->GetCreateId();
                    if (id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }
            }
            for (const auto& it : t.m_DynamicSSBOinfos)
            {
                for (const auto& itt : it.pGfxBuffers)
                {
                    uint32_t id = itt->GetCreateId();
                    if (id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }
            }
            for (const auto& it : t.m_ImageTextureinfos)
            {
                for (const auto& itt : it.vkImageRef)
                {
                    uint32_t id = itt->GetCreateId();
                    if (id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }
            }
            for (const auto& it : t.m_ImageSamplerinfos)
            {
                for (const auto& itt : it.vkImageRef)
                {
                    uint32_t id = itt->GetCreateId();
                    if (id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }
            }
            for (const auto& it : t.m_RWBufferViewInfos)
            {
                for (const auto& itt : it.vkBufferRef)
                {
                    uint32_t id = itt->GetCreateId();
                    if (id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }
            }
            for (const auto& it : t.m_SamplerBufferViewInfos)
            {
                for (const auto& itt : it.vkBufferRef)
                {
                    uint32_t id = itt->GetCreateId();
                    if (id > m_uLastRefSequenceCounter)
                    {
                        bRet = false;
                        ASSERT(0);
                    }                    
                }
            }
            for (const auto& it : t.m_vecBindDynamicUBO)
            {
                uint32_t id = it.pGfxBuffer->GetRef();
                if (id > m_uLastRefSequenceCounter)
                {
                    bRet = false;
                    ASSERT(0);
                }                
            }
            
        }
#endif
        return bRet;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AutoBindUBO(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Buffer* pUBO[], const char* pcszBlockName)
    {
        if (!DrvOption::bSupportDynamicUBO || !pUBO[0]->IsDynamic())
        {
            return AddBindUBO(uSet, uBinding, uCount, pUBO, pcszBlockName);
        }
        else
        {
            return AddBindDynamicUBO(uSet, uBinding, uCount, pUBO, pcszBlockName);
        }
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindSSBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pSSBO, uint32_t uOffset, uint32_t uRange, BOOL bUAV, const char* pcszBlockName)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        BufferInfo buffInfo;
        buffInfo.uBinding      = uBinding;
        buffInfo.uCount        = 1;
        buffInfo.pcszBlockName = pcszBlockName;

        KVulkanBuffer* pVulkanSSBO = (KVulkanBuffer*)pSSBO;
        CHECK_ASSERT(pVulkanSSBO);

        VkDescriptorBufferInfo vkBufferInfo = pVulkanSSBO->GetDescriptorBufferInfo();
        vkBufferInfo.offset                 = uOffset;
        vkBufferInfo.range                  = uRange > 0 ? uRange : vkBufferInfo.range - uOffset;

        buffInfo.vkBufferInfos.push_back(vkBufferInfo);

        m_vecLayoutItem[uSet].m_SSBOinfos.emplace_back(std::move(buffInfo));

        IKGFX_Buffer* vkBuf = pSSBO;
        if (vkBuf->GetDesc()->IsAutoRes())
        {
            if(bUAV)
            {
                KGfxBarrier dstBarrier = { vkBuf,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::UAVMask };
                m_vecBarriers.emplace_back(dstBarrier);
            }
            else
            {
                KGfxBarrier dstBarrier = { vkBuf,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::SRVMask };
                m_vecBarriers.emplace_back(dstBarrier);
            }
        }

        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindDynamicSSBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pSSBO, uint32_t uOffset, uint32_t uRange, BOOL bUAV, const char* pcszBlockName)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        BufferInfo buffInfo;
        buffInfo.uBinding      = uBinding;
        buffInfo.uCount        = 1;
        buffInfo.pcszBlockName = pcszBlockName;

        KVulkanBuffer* pVulkanSSBO = (KVulkanBuffer*)pSSBO;
        if (pVulkanSSBO)
        {
            VkDescriptorBufferInfo vkBufferInfo = pVulkanSSBO->GetDescriptorBufferInfo();
            vkBufferInfo.offset                 = uOffset;
            vkBufferInfo.range                  = uRange > 0 ? uRange : vkBufferInfo.range - uOffset;

            buffInfo.vkBufferInfos.push_back(vkBufferInfo);
            buffInfo.pGfxBuffers.push_back(pSSBO);

#if defined(_DEBUG) && defined(_WIN32)
            const KGfxBufferDesc* pBufferDesc = pVulkanSSBO->GetDesc();
            ASSERT((pBufferDesc->uUsageFlags & gfx::BUFFER_USAGE_STORAGE_BUFFER_BIT));
#endif
        }

        m_vecLayoutItem[uSet].m_DynamicSSBOinfos.emplace_back(std::move(buffInfo));


        IKGFX_Buffer* vkBuf = pSSBO;
        if (vkBuf->GetDesc()->IsAutoRes())
        {
            if(bUAV)
            {
                KGfxBarrier dstBarrier = { vkBuf,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::UAVMask };
                m_vecBarriers.emplace_back(dstBarrier);
            }
            else
            {
                KGfxBarrier dstBarrier = { vkBuf,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::SRVMask };
                m_vecBarriers.emplace_back(dstBarrier);
            }
        }

        // m_vecWriteDescriptorSets.push_back(
        //   vks::initializers::WriteDescriptorSet(m_pDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, uBinding, m_DynamicSSBOinfos.data(), uCount)
        //);

        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AutoBindSSBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pSSBO, uint32_t uOffset, uint32_t uRange, BOOL bUAV, const char* pcszBlockName)
    {
        if (!DrvOption::bSupportDynamicSSBO || !pSSBO->IsDynamic())
        {
            return AddBindSSBO(uSet, uBinding, pSSBO, uOffset, uRange, bUAV, pcszBlockName);
        }
        else
        {
            return AddBindDynamicSSBO(uSet, uBinding, pSSBO, uOffset, uRange, bUAV, pcszBlockName);
        }
    }

    void KVulkanDescriptorSet::ClearPoolContainer()
    {
        m_pContainer     = nullptr;
        m_pRealAllocPool = nullptr;
    }

    gfx::KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindShareUBO(uint32_t uSet, uint32_t uBinding, IKGFX_Buffer* pUBO, uint32_t uSize, uint32_t uOffset, const char* pcszBlockName /*= nullptr*/)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        BufferInfo buffInfo;
        buffInfo.uBinding      = uBinding;
        buffInfo.uCount        = 1;
        buffInfo.pcszBlockName = pcszBlockName;

        ASSERT(uOffset < pUBO->GetDesc()->uByteWidth);
        KVulkanBuffer*      pVulkanUBO    = (KVulkanBuffer*)pUBO;
        VkDescriptorBufferInfo sDespBuffInfo = pVulkanUBO->GetDescriptorBufferInfo();
        sDespBuffInfo.offset                 = uOffset;
        sDespBuffInfo.range                  = uSize;
        buffInfo.vkBufferInfos.push_back(sDespBuffInfo);
        ASSERT(sDespBuffInfo.range < 2 * 1024 * 1024);

        m_vecLayoutItem[uSet].m_UBOinfos.emplace_back(std::move(buffInfo));
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindUBO(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Buffer* pUBO[], const char* pcszBlockName)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        BufferInfo buffInfo;
        buffInfo.uBinding      = uBinding;
        buffInfo.uCount        = uCount;
        buffInfo.pcszBlockName = pcszBlockName;

        for (uint32_t i = 0; i < uCount; i++)
        {
            KVulkanBuffer* pVulkanUBO = (KVulkanBuffer*)pUBO[i];
            buffInfo.vkBufferInfos.push_back(pVulkanUBO->GetDescriptorBufferInfo());

            const VkDescriptorBufferInfo& sBufferInfo = buffInfo.vkBufferInfos[buffInfo.vkBufferInfos.size() - 1];
            ASSERT(sBufferInfo.range < 2 * 1024 * 1024);


            IKGFX_Buffer* vkBuf = pUBO[i];
            if (vkBuf->GetDesc()->IsAutoRes())
            {
                KGfxBarrier dstBarrier = { vkBuf,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::ConstBuffer };
                m_vecBarriers.emplace_back(dstBarrier);
            }
        }
        m_vecLayoutItem[uSet].m_UBOinfos.emplace_back(std::move(buffInfo));
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindDynamicUBO(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Buffer* pUBO[], const char* pcszBlockName)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        BufferInfo buffInfo;
        buffInfo.uBinding      = uBinding;
        buffInfo.uCount        = uCount;
        buffInfo.pcszBlockName = pcszBlockName;

        for (uint32_t i = 0; i < uCount; i++)
        {
            KVulkanBuffer* pVulkanUBO = (KVulkanBuffer*)pUBO[i];
            buffInfo.vkBufferInfos.push_back(pVulkanUBO->GetDescriptorBufferInfo());
            buffInfo.pGfxBuffers.push_back(pUBO[i]);
        }

        m_vecLayoutItem[uSet].m_DynamicUBOinfos.emplace_back(std::move(buffInfo));
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindSRVArray(uint32_t uSet, uint32_t uBinding, uint32_t uNum, IKGFX_TextureView* const* ppSRVs)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        ImageInfo imageInfo;
        imageInfo.uBinding        = uBinding;
        imageInfo.uCount          = uNum;
        imageInfo.uDescriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        if (ppSRVs && uNum > 1) //&& pSRV->IsPostLoaded())
        {
#ifdef _DEBUG
            // imageInfo.strDebugName = pSRV->GetResourceName();
#endif
            for (uint32_t i = 0; i < uNum; ++i)
            {
                VkDescriptorImageInfo sDescriptorImageInfo = {};
                sDescriptorImageInfo.sampler               = nullptr;
                sDescriptorImageInfo.imageView             = (VkImageView)ppSRVs[i]->GetNativeHandle();
                sDescriptorImageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                if (sDescriptorImageInfo.imageView)
                {
                    imageInfo.vkImageInfos.push_back(sDescriptorImageInfo);
                }

                KVulkanTexture* vkTex = static_cast<KVulkanTexture*>(ppSRVs[i]->GetResource());
#if DESCRIPTORSET_VALIDATE
                imageInfo.vkImageRef.push_back(vkTex);
#endif
                if (vkTex->GetDesc()->IsAutoRes())
                {
                    auto subRange = vkTex->ResolveSubresourceRange(ppSRVs[i]->GetViewDesc().sSubresourceRange);
                    KGfxBarrier dstBarrier = { vkTex,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::SRVMask,subRange };
                    m_vecBarriers.emplace_back(dstBarrier);
                }
            }


            m_vecLayoutItem[uSet].m_ImageTextureinfos.emplace_back(std::move(imageInfo));
        }
        else
        {
            m_bHasError = true;
        }

        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindUAVArray(uint32_t uSet, uint32_t uBinding, uint32_t uNum, IKGFX_TextureView* const* ppUAVs)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        ImageInfo imageInfo;
        imageInfo.uBinding        = uBinding;
        imageInfo.uCount          = uNum;
        imageInfo.uDescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        if (ppUAVs && uNum > 0)
        {
#ifdef _DEBUG
            // imageInfo.strDebugName = pUAV->GetResourceName();
#endif

            for (uint32_t i = 0; i < uNum; ++i)
            {
                VkDescriptorImageInfo sDescriptorImageInfo = {};
                sDescriptorImageInfo.sampler               = nullptr;
                sDescriptorImageInfo.imageView             = (VkImageView)ppUAVs[i]->GetNativeHandle();
                sDescriptorImageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

                if (sDescriptorImageInfo.imageView)
                {
                    imageInfo.vkImageInfos.push_back(sDescriptorImageInfo);
                }

                KVulkanTexture* vkTex = dynamic_cast<KVulkanTexture*>(ppUAVs[i]->GetResource());

#if DESCRIPTORSET_VALIDATE
                imageInfo.vkImageRef.push_back(vkTex);
#endif
                if (vkTex->GetDesc()->IsAutoRes())
                {
                    auto subRange = vkTex->ResolveSubresourceRange(ppUAVs[i]->GetViewDesc().sSubresourceRange);
                    KGfxBarrier dstBarrier = { vkTex,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::UAVMask,subRange };
                    m_vecBarriers.emplace_back(dstBarrier);
                }
            }

            m_vecLayoutItem[uSet].m_RWImageinfos.emplace_back(std::move(imageInfo));
        }
        else
        {
            m_bHasError = true;
        }

        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindSRV(uint32_t uSet, uint32_t uBinding, IKGFX_TextureView* pSRV)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        ImageInfo imageInfo;
        imageInfo.uBinding        = uBinding;
        imageInfo.uCount          = 1;
        imageInfo.uDescriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        if (pSRV) //&& pSRV->IsPostLoaded())
        {
#ifdef _DEBUG
            // imageInfo.strDebugName = pSRV->GetResourceName();
#endif

            VkDescriptorImageInfo sDescriptorImageInfo = {};
            sDescriptorImageInfo.sampler               = nullptr;
            sDescriptorImageInfo.imageView             = (VkImageView)pSRV->GetNativeHandle();
            sDescriptorImageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            if (sDescriptorImageInfo.imageView)
            {
                imageInfo.vkImageInfos.push_back(sDescriptorImageInfo);
            }

            KVulkanTexture* vkTex = dynamic_cast<KVulkanTexture*>(pSRV->GetResource());
#if DESCRIPTORSET_VALIDATE
            imageInfo.vkImageRef.push_back(vkTex);
#endif
            if (vkTex->GetDesc()->IsAutoRes())
            {
                auto subRange = vkTex->ResolveSubresourceRange(pSRV->GetViewDesc().sSubresourceRange);
                KGfxBarrier dstBarrier = { vkTex,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::SRVMask,subRange };
                m_vecBarriers.emplace_back(dstBarrier);
            }

            m_vecLayoutItem[uSet].m_ImageTextureinfos.emplace_back(std::move(imageInfo));
        }
        else
        {
            m_bHasError = true;
        }

        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindUAV(uint32_t uSet, uint32_t uBinding, IKGFX_TextureView* pUAV)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        ImageInfo imageInfo;
        imageInfo.uBinding        = uBinding;
        imageInfo.uCount          = 1;
        imageInfo.uDescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        if (pUAV)
        {
#ifdef _DEBUG
            // imageInfo.strDebugName = pUAV->GetResourceName();
#endif

            VkDescriptorImageInfo sDescriptorImageInfo = {};
            sDescriptorImageInfo.sampler               = nullptr;
            sDescriptorImageInfo.imageView             = (VkImageView)pUAV->GetNativeHandle();
            sDescriptorImageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

            if (sDescriptorImageInfo.imageView)
            {
                imageInfo.vkImageInfos.push_back(sDescriptorImageInfo);
            }

            KVulkanTexture* vkTex = dynamic_cast<KVulkanTexture*>(pUAV->GetResource());
#if DESCRIPTORSET_VALIDATE
            imageInfo.vkImageRef.push_back(vkTex);
#endif
            if (vkTex->GetDesc()->IsAutoRes())
            {
                auto subRange = vkTex->ResolveSubresourceRange(pUAV->GetViewDesc().sSubresourceRange);
                KGfxBarrier dstBarrier = { vkTex,gfx::KGfxAccess::Unknown,gfx::KGfxAccess::UAVMask,subRange };
                m_vecBarriers.emplace_back(dstBarrier);
            }

            m_vecLayoutItem[uSet].m_RWImageinfos.emplace_back(std::move(imageInfo));
        }
        else
        {
            m_bHasError = true;
        }

        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindCBV(uint32_t uSet, uint32_t uBinding, IKGFX_BufferView* pCBV)
    {
        gfx::IKGFX_Buffer* pBuffer = pCBV->GetResource();
        AutoBindUBO(uSet, uBinding, 1, &pBuffer);
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindRWBufferView(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_BufferView* pBufViews[])
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        RWBufferViewInfo bufferViewInfo;
        bufferViewInfo.uBinding = uBinding;
        bufferViewInfo.uCount   = uCount;

        for (uint32_t i = 0; i < uCount; i++)
        {
            if (pBufViews[i])
            {                
                KGfxBarrier dstBarrier = { pBufViews[i]->GetResource(),gfx::KGfxAccess::Unknown,gfx::KGfxAccess::UAVMask };
                m_vecBarriers.emplace_back(dstBarrier);

                bufferViewInfo.vkBufferViews.push_back((VkBufferView)pBufViews[i]->GetViewHandle());
#if DESCRIPTORSET_VALIDATE
                bufferViewInfo.vkBufferRef.push_back(pBufViews[i]->GetResource());
#endif
            }
        }


        m_vecLayoutItem[uSet].m_RWBufferViewInfos.emplace_back(std::move(bufferViewInfo));
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindSampleBufferView(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_BufferView* pBufViews[])
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        SamplerBufferViewInfo bufferViewInfo;
        bufferViewInfo.uBinding = uBinding;
        bufferViewInfo.uCount   = uCount;

        for (uint32_t i = 0; i < uCount; i++)
        {
            if (pBufViews[i])
            {
                KGfxBarrier dstBarrier = { pBufViews[i]->GetResource(),gfx::KGfxAccess::Unknown,gfx::KGfxAccess::SRVMask };
                m_vecBarriers.emplace_back(dstBarrier);

                bufferViewInfo.vkBufferViews.push_back((VkBufferView)pBufViews[i]->GetViewHandle());
#if DESCRIPTORSET_VALIDATE
                bufferViewInfo.vkBufferRef.push_back(pBufViews[i]->GetResource());
#endif
            }
        }

        m_vecLayoutItem[uSet].m_SamplerBufferViewInfos.emplace_back(std::move(bufferViewInfo));
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindAccelerationStructure(uint32_t uSet, uint32_t uBinding, KRayTracingScene* accelerationStructure)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);
        auto vkRayTracingScene = static_cast<KVulkanRayTracingScene*>(accelerationStructure);
        AccelerationStructureInfo asInfo;
        asInfo.uBinding = uBinding;
        asInfo.uCount = 1;
        asInfo.accelerationStructure = vkRayTracingScene -> GetAcceleration();
        m_vecLayoutItem[uSet].m_accelerationStructureInfos.emplace_back(std::move(asInfo));
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindSampler(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Sampler* pSampler[])
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        ImageInfo imageInfo;
        imageInfo.uBinding        = uBinding;
        imageInfo.uCount          = uCount;
        imageInfo.uDescriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;

        for (uint32_t i = 0; i < uCount; i++)
        {
            KVulkanSampler* pVulkanSampler = (KVulkanSampler*)pSampler[i];
            ASSERT(pVulkanSampler);
            imageInfo.vkImageInfos.push_back(vks::initializers::DescriptorImageInfo(pVulkanSampler->GetVKSampler(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        }
        m_vecLayoutItem[uSet].m_SamplerDescriptorsInfos.emplace_back(std::move(imageInfo));

        // m_vecWriteDescriptorSets.push_back(
        //   vks::initializers::WriteDescriptorSet(m_pDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uBinding, m_SamplerDescriptorsInfos.data(), uCount)
        //);
        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::AddBindCombinedSampler(uint32_t uSet, uint32_t uBinding, uint32_t uCount, IKGFX_Sampler* pSampler[], const void* pImageViews)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);

        ImageInfo imageInfo;
        imageInfo.uBinding        = uBinding;
        imageInfo.uCount          = uCount;
        imageInfo.uDescriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

        for (uint32_t i = 0; i < uCount; i++)
        {
            KVulkanSampler* pVulkanSampler = (KVulkanSampler*)pSampler[i];
            ASSERT(pVulkanSampler);

            VkImageView* pImageView = (VkImageView*)pImageViews;
            imageInfo.vkImageInfos.push_back(vks::initializers::DescriptorImageInfo(pVulkanSampler->GetVKSampler(), *pImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        }
        m_vecLayoutItem[uSet].m_SamplerDescriptorsInfos.emplace_back(std::move(imageInfo));

        return *this;
    }

    KVulkanDescriptorSet& KVulkanDescriptorSet::Begin()
    {
        PROF_CPU_DETAIL();
        BOOL                   bRet            = false;
        VkResult               hRetCode        = VK_INCOMPLETE;
        VkDevice               pDevice         = GetVkDevice();
        KVulkanDescriptorPool* pDescriptorPool = m_pRealAllocPool;
        KGLOG_PROCESS_ERROR(pDescriptorPool);
        // KGLOG_PROCESS_ERROR(pDescriptorPool->GetPool());
        KGLOG_ASSERT_EXIT(m_pLayout);

        if (m_vecDescriptorSet.empty())
        {
            uint32_t uCount = m_pLayout->GetLayoutSetCount();
            for (uint32_t i = 0; i < uCount; ++i)
            {
                if (m_pLayout->IsBindless(i))
                {
                    // 这里不处理bindless了，只用来取个pipelinelayout我晕
                    continue;
                }
                VkDescriptorSetLayout       pl             = m_pLayout->GetDesriptorSetLayout(i);
                VkDescriptorSet             pDescriptorSet = nullptr;
                VkDescriptorSetAllocateInfo allocInfo      = vks::initializers::DescriptorSetAllocateInfo(pDescriptorPool->GetPool(), &pl, 1);
                hRetCode                                   = vks::vkAllocateDescriptorSets(pDevice, &allocInfo, &pDescriptorSet);
                ASSERT(hRetCode == VK_SUCCESS);
                KGLOG_COM_PROCESS_ERROR(hRetCode);
                m_vecDescriptorSet.push_back(pDescriptorSet);
            }
        }

        // if (!m_pDescriptorSet)
        //{
        //   KVulkanLayout* pVulkanLayout = (KVulkanLayout*)m_pLayout;
        //   VkDescriptorSetLayout pl = pVulkanLayout->GetDesriptorSetLayout();
        //   if (pDescriptorPool)
        //   {
        //       VkDescriptorSetAllocateInfo allocInfo = vks::initializers::DescriptorSetAllocateInfo(pDescriptorPool->GetPool(), &pl, 1);
        //       hRetCode = vks::vkAllocateDescriptorSets(pDevice, &allocInfo, &m_pDescriptorSet);
        //       KGLOG_COM_PROCESS_ERROR(hRetCode);
        //       //KGLogPrintf(KGLOG_INFO,"create descriptor set : %p", m_pDescriptorSet);
        //   }
        // }

        Clear();
    Exit0:
        return *this;
    }

    void KVulkanDescriptorSet::Clear()
    {
        PROF_CPU_DETAIL();
        for (auto& it : m_vecLayoutItem)
        {
            it.m_vecWriteDescriptorSets.clear();
            it.m_UBOinfos.clear();
            it.m_DynamicUBOinfos.clear();
            it.m_SSBOinfos.clear();
            it.m_DynamicSSBOinfos.clear();
            it.m_SamplerDescriptorsInfos.clear();
            it.m_ImageTextureinfos.clear();      //
            it.m_ImageSamplerinfos.clear();
            it.m_RWImageinfos.clear();           //
            it.m_RWBufferViewInfos.clear();      //
            it.m_SamplerBufferViewInfos.clear(); //
            it.m_vecBindDynamicUBO.clear();
            it.m_vecDynamicUBOOffsets.clear();
        }
        // m_vecLayoutItem.clear();
        m_bHasError        = false;
        m_uUpdateCheckCode = 0;
        m_vecBarriers.clear();
    }

    void KVulkanDescriptorSet::ClearBarrier()
    {
        m_vecBarriers.clear();
    }

    void KVulkanDescriptorSet::TransBarrier() const
    {
        gfx::GetRenderContext()->Transition(m_vecBarriers.data(), (uint32_t)m_vecBarriers.size());
    }

    BOOL KVulkanDescriptorSet::End()
    {
        PROF_CPU_DETAIL();

        VkDevice pDevice = GetVkDevice();
        BOOL     bRet    = false;
        uint32_t uCount  = (uint32_t)m_vecLayoutItem.size();
        KGLOG_ASSERT_EXIT(m_vecDescriptorSet.size() == uCount);

#ifdef _DEBUG
        // 限制一帧只能Update一次，提前暴露问题
        KGLOG_ASSERT_EXIT(m_nLastUpdateFrameMoveCount != NSEngine::GetRenderFrameMoveLoopCount());
        m_nLastUpdateFrameMoveCount = NSEngine::GetRenderFrameMoveLoopCount();
#endif
        TransBarrier();
        for (uint32_t t = 0; t < uCount; ++t)
        {
            _LayoutSetItem& item           = m_vecLayoutItem[t];
            VkDescriptorSet pDescriptorSet = m_vecDescriptorSet[t];
            item.m_vecWriteDescriptorSets.clear();

            KG_PROCESS_ERROR(!m_bHasError);

            if (DrvOption::bSupportDynamicUBO)
            {
                if (!item.m_DynamicSSBOinfos.empty())
                {
                    if (item.m_DynamicSSBOinfos.size() > 1)
                    {
                        std::sort(item.m_DynamicSSBOinfos.begin(), item.m_DynamicSSBOinfos.end(), [](BufferInfo& l, BufferInfo& r) -> bool { return l.uBinding < r.uBinding; });
                    }

                    for (int i = 0; i < item.m_DynamicSSBOinfos.size(); i++)
                    {
                        item.m_vecBindDynamicUBO.emplace_back(item.m_DynamicSSBOinfos[i].uBinding, item.m_DynamicSSBOinfos[i].pGfxBuffers[0], item.m_DynamicSSBOinfos[i].pcszBlockName);

                        item.m_vecWriteDescriptorSets.push_back(
                            vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, item.m_DynamicSSBOinfos[i].uBinding, item.m_DynamicSSBOinfos[i].vkBufferInfos.data(), item.m_DynamicSSBOinfos[i].uCount)
                        );
                    }
                }

                if (!item.m_DynamicUBOinfos.empty())
                {
                    if (item.m_DynamicUBOinfos.size() > 1)
                    {
                        std::sort(item.m_DynamicUBOinfos.begin(), item.m_DynamicUBOinfos.end(), [](BufferInfo& l, BufferInfo& r) -> bool { return l.uBinding < r.uBinding; });
                    }

                    for (int i = 0; i < item.m_DynamicUBOinfos.size(); i++)
                    {
                        item.m_vecBindDynamicUBO.emplace_back(item.m_DynamicUBOinfos[i].uBinding, item.m_DynamicUBOinfos[i].pGfxBuffers[0], item.m_DynamicUBOinfos[i].pcszBlockName);
                        item.m_vecWriteDescriptorSets.push_back(
                            vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, item.m_DynamicUBOinfos[i].uBinding, item.m_DynamicUBOinfos[i].vkBufferInfos.data(), item.m_DynamicUBOinfos[i].uCount)
                        );
                    }
                }
            }

            if (!item.m_UBOinfos.empty())
            {
                for (int i = 0; i < item.m_UBOinfos.size(); i++)
                {
                    item.m_vecWriteDescriptorSets.push_back(
                        vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, item.m_UBOinfos[i].uBinding, item.m_UBOinfos[i].vkBufferInfos.data(), item.m_UBOinfos[i].uCount)
                    );

                    const BufferInfo& sBufferInfo = item.m_UBOinfos[i];
                    for (const VkDescriptorBufferInfo& sVkDespBufferInfo : sBufferInfo.vkBufferInfos)
                    {
                        ASSERT(sVkDespBufferInfo.range < 2 * 1024 * 1024);
                    }
                }
            }

            if (!item.m_SSBOinfos.empty())
            {
                for (int i = 0; i < item.m_SSBOinfos.size(); i++)
                {
                    item.m_vecWriteDescriptorSets.push_back(
                        vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, item.m_SSBOinfos[i].uBinding, item.m_SSBOinfos[i].vkBufferInfos.data(), item.m_SSBOinfos[i].uCount)
                    );
                }
            }

            if (!item.m_SamplerDescriptorsInfos.empty())
            {
                for (int i = 0; i < item.m_SamplerDescriptorsInfos.size(); i++)
                {
                    if (item.m_SamplerDescriptorsInfos[i].uDescriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    {
                        item.m_vecWriteDescriptorSets.push_back(
                            vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, item.m_SamplerDescriptorsInfos[i].uBinding, item.m_SamplerDescriptorsInfos[i].vkImageInfos.data(), item.m_SamplerDescriptorsInfos[i].uCount)
                        );
                    }
                    else
                    {
                        item.m_vecWriteDescriptorSets.push_back(
                            vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_SAMPLER, item.m_SamplerDescriptorsInfos[i].uBinding, item.m_SamplerDescriptorsInfos[i].vkImageInfos.data(), item.m_SamplerDescriptorsInfos[i].uCount)
                        );
                    }
                }
            }

            if (!item.m_ImageTextureinfos.empty())
            {
                for (int i = 0; i < item.m_ImageTextureinfos.size(); i++)
                {
                    item.m_vecWriteDescriptorSets.push_back(
                        vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, item.m_ImageTextureinfos[i].uBinding, item.m_ImageTextureinfos[i].vkImageInfos.data(), item.m_ImageTextureinfos[i].uCount)
                    );

                    // m_vecWriteDescriptorSets.push_back(
                    //   vks::initializers::WriteDescriptorSet(m_pDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_ImageTextureinfos[i].uBinding, imageInfo.data(), m_ImageTextureinfos[i].uCount)
                    //);
                }
            }

            if (!item.m_RWImageinfos.empty())
            {
                for (int i = 0; i < item.m_RWImageinfos.size(); i++)
                {
                    item.m_vecWriteDescriptorSets.push_back(
                        vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, item.m_RWImageinfos[i].uBinding, item.m_RWImageinfos[i].vkImageInfos.data(), item.m_RWImageinfos[i].uCount)
                    );
                }
            }

            if (!item.m_RWBufferViewInfos.empty())
            {
                for (const auto& iter : item.m_RWBufferViewInfos)
                {
                    item.m_vecWriteDescriptorSets.push_back(vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, iter.uBinding, iter.vkBufferViews.data(), iter.uCount));
                }
            }

            if (!item.m_SamplerBufferViewInfos.empty())
            {
                for (const auto& iter : item.m_SamplerBufferViewInfos)
                {
                    item.m_vecWriteDescriptorSets.push_back(vks::initializers::WriteDescriptorSet(pDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, iter.uBinding, iter.vkBufferViews.data(), iter.uCount));
                }
            }
            if (!item.m_accelerationStructureInfos.empty())
            {
                for (int i = 0; i < item.m_accelerationStructureInfos.size(); i++)
                {
                    VkWriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
                    descriptor_acceleration_structure_info.accelerationStructureCount = 1;
                    descriptor_acceleration_structure_info.pAccelerationStructures    = &item.m_accelerationStructureInfos[i].accelerationStructure;
                    VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    descriptorWrite.dstSet          = pDescriptorSet;
                    descriptorWrite.dstBinding      = item.m_accelerationStructureInfos[i].uBinding;
                    descriptorWrite.descriptorCount = 1;
                    descriptorWrite.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                    // The acceleration structure descriptor has to be chained via pNext
                    descriptorWrite.pNext           = &descriptor_acceleration_structure_info;
                    item.m_vecWriteDescriptorSets.push_back(
                        descriptorWrite
                    );
                }
            }
            /*
            printf("%p %d   %d \r\n", this, nFrame , this->m_uProgramCheckCode);
            for(auto it : item.m_vecWriteDescriptorSets)
            {
                VkWriteDescriptorSet &set = it;
                if(set.pBufferInfo)
                {
                    printf("buffer: %p   %d \r\n", set.pBufferInfo->buffer, set.pBufferInfo->offset);
                }
                if(set.pImageInfo)
                {
                    printf("image: %p \r\n", set.pImageInfo->imageView);
                }
            }
            */
            vks::vkUpdateDescriptorSets(pDevice, (uint32_t)item.m_vecWriteDescriptorSets.size(), item.m_vecWriteDescriptorSets.data(), 0, nullptr);

            if (item.m_vecBindDynamicUBO.size() > 1)
            {
                std::sort(item.m_vecBindDynamicUBO.begin(), item.m_vecBindDynamicUBO.end(), [](GfxBufferInfo& l, GfxBufferInfo& r) -> bool { return l.uBinding < r.uBinding; });
            }

            // 尝试优化内存
            std::vector<VkWriteDescriptorSet> vecEmpty;
            item.m_vecWriteDescriptorSets.swap(vecEmpty);
        }

        bRet = true;
    Exit0:
#if DESCRIPTORSET_VALIDATE
        m_uLastRefSequenceCounter = gfx::KGfxRef::RefSequenceCounter;
#endif
        return bRet;
    }

    BOOL KVulkanDescriptorSet::IsInited()
    {
        if (m_pContainer && !m_vecDescriptorSet.empty())
            return true;
        else
            return false;
    }

    BOOL KVulkanDescriptorSet::HasError()
    {
        return m_bHasError;
    }

    const VkDescriptorSet KVulkanDescriptorSet::GetDescriptorSet(uint32_t uSet)
    {
        if (uSet < (uint32_t)m_vecDescriptorSet.size())
        {
            return m_vecDescriptorSet[uSet];
        }
        else
        {
            // KGLogPrintf(KGLOG_ERR, "error set id");
            return nullptr;
        }
    }

    uint32_t KVulkanDescriptorSet::GetSetCount()
    {
        return (uint32_t)m_vecDescriptorSet.size();
    }

    void KVulkanDescriptorSet::SetDescriptorSet(uint32_t uSet, VkDescriptorSet pDes)
    {
        // m_pDescriptorSet = pDes;
        if (uSet <= m_vecDescriptorSet.size())
        {
            m_vecDescriptorSet[uSet] = pDes;
        }
    }

    void KVulkanDescriptorSet::AddBindDynamicUBOIdToOffsetArray(uint32_t uSet)
    {
        PROF_CPU_DETAIL();
        FitLayoutItemSize(uSet);
        m_vecLayoutItem[uSet].m_vecDynamicUBOOffsets.push_back(0);
    }

    uint32_t KVulkanDescriptorSet::GetDynamicUBOOffsetArrayCount(uint32_t uSet)
    {
        PROF_CPU_DETAIL();
        static uint32_t MaxDynamicUBOCount = 0;
        uint32_t        uSize              = 0;

        if (uSet < m_vecLayoutItem.size())
        {
            uSize              = (uint32_t)m_vecLayoutItem[uSet].m_vecDynamicUBOOffsets.size();
            MaxDynamicUBOCount = std::max(MaxDynamicUBOCount, uSize);
            // ASSERT(MaxDynamicUBOCount <= 8);
        }

        return uSize;
    }

    uint32_t* KVulkanDescriptorSet::GetDynamicUBOOffetArray(uint32_t uSet, BOOL bDebug)
    {
        PROF_CPU_DETAIL();
        uint32_t* pRetArray    = nullptr;
        BOOL      bFind        = false;
        uint32_t  uDebugOffset = 0;
        char      name[64];
        name[0] = '\0';
        if (uSet < m_vecLayoutItem.size())
        {
            auto nSize = m_vecLayoutItem[uSet].m_vecBindDynamicUBO.size();
            ASSERT(m_vecLayoutItem[uSet].m_vecDynamicUBOOffsets.size() >= nSize);

            if (m_vecLayoutItem[uSet].m_vecDynamicUBOOffsets.size() < nSize)
            {
                m_vecLayoutItem[uSet].m_vecDynamicUBOOffsets.resize(nSize);
            }

            for (int i = 0; i < nSize; i++)
            {
                if (m_vecLayoutItem[uSet].m_vecBindDynamicUBO[i].uBinding > 1024 || !m_vecLayoutItem[uSet].m_vecBindDynamicUBO[i].pGfxBuffer)
                {
                    KGLogPrintf(
                        KGLOG_ERR,
                        " dynamic Ubo %d name %s: address %p binding sock:%d is error",
                        i,
                        m_vecLayoutItem[uSet].m_vecBindDynamicUBO[i].pcszBlockName,
                        m_vecLayoutItem[uSet].m_vecBindDynamicUBO[i].pGfxBuffer,
                        m_vecLayoutItem[uSet].m_vecBindDynamicUBO[i].uBinding
                    );
                    goto Exit0;
                }
                uint32_t uOffset                                = m_vecLayoutItem[uSet].m_vecBindDynamicUBO[i].pGfxBuffer->GetDynamicOffset();
                m_vecLayoutItem[uSet].m_vecDynamicUBOOffsets[i] = uOffset;
            }

            pRetArray = m_vecLayoutItem[uSet].m_vecDynamicUBOOffsets.data();
        }
        else
        {
            ASSERT(0);
        }

    Exit0:
        return pRetArray;
    }

    BOOL KVulkanDescriptorSet::IsFillBindData()
    {
        return m_vecLayoutItem.size() > 0;
    }


    ////////////////////////////////////////////////////////////////////
    KVulkanVertexDescriptor::KVulkanVertexDescriptor()
    {
        m_InputState = vks::initializers::PipelineVertexInputStateCreateInfo();
    }

    KVulkanVertexDescriptor::~KVulkanVertexDescriptor()
    {
    }

    KVulkanVertexDescriptor& KVulkanVertexDescriptor::Begin()
    {
        m_vecBindingDescriptions.clear();
        m_vecAttributeDescriptions.clear();
        return *this;
    }
    KVulkanVertexDescriptor& KVulkanVertexDescriptor::AddBindDescription(uint32_t binding, uint32_t stride, enumVertexInputRate inputRate)
    {
        VkVertexInputRate input = GetVertInputRate(inputRate);
        m_vecBindingDescriptions.push_back(vks::initializers::VertexInputBindingDescription(binding, stride, input));
        return *this;
    }

    KVulkanVertexDescriptor& KVulkanVertexDescriptor::AddAttribute(uint32_t binding, uint32_t location, enumVertexFormat format, uint32_t offset)
    {
        VkFormat vkFormat = GetVertFormat(format);
        m_vecAttributeDescriptions.push_back(vks::initializers::VertexInputAttributeDescription(binding, location, vkFormat, offset));
        return *this;
    }

    BOOL KVulkanVertexDescriptor::End()
    {
        m_InputState                                 = vks::initializers::PipelineVertexInputStateCreateInfo();
        m_InputState.vertexBindingDescriptionCount   = static_cast<uint32_t>(m_vecBindingDescriptions.size());
        m_InputState.pVertexBindingDescriptions      = m_vecBindingDescriptions.data();
        m_InputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vecAttributeDescriptions.size());
        m_InputState.pVertexAttributeDescriptions    = m_vecAttributeDescriptions.data();


        uint64_t    u0    = 0;
        uint64_t    u1    = 0;
        const char* p     = (const char*)m_vecBindingDescriptions.data();
        uint32_t    uSize = (uint32_t)m_vecBindingDescriptions.size() * sizeof(VkVertexInputBindingDescription);
        if (uSize)
        {
            u0 = KSTR_HELPER::GetHashCodeForMem64Bit(p, uSize, 0);
        }
        p     = (const char*)m_vecAttributeDescriptions.data();
        uSize = (uint32_t)m_vecAttributeDescriptions.size() * sizeof(VkVertexInputAttributeDescription);
        if (uSize)
        {
            u1 = KSTR_HELPER::GetHashCodeForMem64Bit(p, uSize, 0);
        }
        m_uHashCode = u0 + u1;
        return true;
    }

    uint64_t KVulkanVertexDescriptor::GetHashCode()
    {
        return m_uHashCode;
    }

    VkPipelineVertexInputStateCreateInfo* KVulkanVertexDescriptor::GetInputStateCreateInfo()
    {
        return &m_InputState;
    }

    ////////////////////////////////////////////////////////////////////

    KStageSamplerDef::KStageSamplerDef()
    {
        m_samplerState.bNeedShaderInit = true;
    }

    KStageSamplerDef::~KStageSamplerDef()
    {
    }

    void _TrimLine(char* szLine)
    {
        int n = (int)strlen(szLine);
        if (szLine[n - 1] == '\r' || szLine[n - 1] == '\n' || szLine[n - 1] == ' ')
        {
            szLine[n - 1] = '\0';
        }
    }

    // #SamplerState<_ShadowImages> min=linear mag=linear mip=none warps=border warpt=border filter=anisotropic maxanisotropy=2 comparefunc=lequal border=1 nmiplodbias=0;
    BOOL KStageSamplerDef::SetSamplerDef(const char* pcszSamplerDef)
    {
        PROF_CPU_DETAIL();
        BOOL bResult = FALSE;

        std::string strSamplerDef = pcszSamplerDef;
        std::string strSamplerDefArg;

        std::vector<std::string> vecArgs;

        const std::string                            strSpace = " ";
        const std::string                            strEmpty = "";
        std::vector<std::string>                     vecArgAndVal;
        std::unordered_map<std::string, std::string> mapArg2Val;
        decltype(mapArg2Val.begin())                 itArg2Val;

        size_t stSamplerNameBegin = std::string::npos;
        size_t stSamplerNameEnd   = std::string::npos;
        size_t stArgEnd           = std::string::npos;

        stSamplerNameBegin = strSamplerDef.find_first_of('<');
        KGLOG_ASSERT_EXIT(stSamplerNameBegin != std::string::npos);

        stSamplerNameEnd = strSamplerDef.find_first_of('>', stSamplerNameBegin);
        KGLOG_ASSERT_EXIT(stSamplerNameEnd != std::string::npos && stSamplerNameEnd > stSamplerNameBegin);

        m_strSamplerName = strSamplerDef.substr(stSamplerNameBegin + 1, stSamplerNameEnd - stSamplerNameBegin - 1);

        strSamplerDefArg = strSamplerDef.substr(stSamplerNameEnd + 1);
        stArgEnd         = strSamplerDefArg.rfind(';');
        if (stArgEnd != std::string::npos)
        {
            strSamplerDefArg = strSamplerDefArg.substr(0, stArgEnd);
        }

        KSTR_HELPER::StrSplit(strSamplerDefArg.c_str(), " ", vecArgs);

        for (std::string strArg : vecArgs)
        {
            KSTR_HELPER::ReplaceStr(strArg, strSpace, strEmpty, 0);

            if (strArg.empty())
            {
                continue;
            }

            vecArgAndVal.clear();
            KSTR_HELPER::StrSplit(strArg.c_str(), "=", vecArgAndVal);
            if (vecArgAndVal.size() != 2)
            {
                continue;
            }

            mapArg2Val.insert(std::make_pair(vecArgAndVal[0], vecArgAndVal[1]));
        }

        itArg2Val = mapArg2Val.find("min");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue == "linear")
            {
                m_samplerState.enuMinFilter = FILTER_LINEAR;
            }
            else if (strValue == "cubic")
            {
                m_samplerState.enuMinFilter = FILTER_CUBIC_IMG;
            }
            else
            {
                m_samplerState.enuMinFilter = FILTER_NEAREST;
            }
        }

        itArg2Val = mapArg2Val.find("mag");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue == "linear")
            {
                m_samplerState.enuMagFilter = FILTER_LINEAR;
            }
            else if (strValue == "cubic")
            {
                m_samplerState.enuMagFilter = FILTER_CUBIC_IMG;
            }
            else
            {
                m_samplerState.enuMagFilter = FILTER_NEAREST;
            }
        }

        itArg2Val = mapArg2Val.find("mip");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue.empty() || strValue == "none")
            {
                m_samplerState.bEnableMipmap = false;
                m_samplerState.enuMipmapMode = SAMPLER_MIPMAP_MODE_NEAREST;
                m_samplerState.fToMaxLod     = 0.0f;
            }
            else if (strValue == "nearest")
            {
                m_samplerState.bEnableMipmap = true;
                m_samplerState.enuMipmapMode = SAMPLER_MIPMAP_MODE_NEAREST;
            }
            else if (strValue == "linear")
            {
                m_samplerState.bEnableMipmap = true;
                m_samplerState.enuMipmapMode = SAMPLER_MIPMAP_MODE_LINEAR;
            }
        }

        itArg2Val = mapArg2Val.find("warps");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue == "repeat")
            {
                m_samplerState.enuAddressModeU = SAMPLER_ADDRESS_MODE_REPEAT;
            }
            else if (strValue == "clamp")
            {
                m_samplerState.enuAddressModeU = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            }
            else if (strValue == "mirror")
            {
                m_samplerState.enuAddressModeU = SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            }
            else if (strValue == "border")
            {
                m_samplerState.enuAddressModeU = SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            else
            {
                m_samplerState.enuAddressModeU = SAMPLER_ADDRESS_MODE_REPEAT;
            }
        }

        itArg2Val = mapArg2Val.find("warpt");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue == "repeat")
            {
                m_samplerState.enuAddressModeV = SAMPLER_ADDRESS_MODE_REPEAT;
            }
            else if (strValue == "clamp")
            {
                m_samplerState.enuAddressModeV = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            }
            else if (strValue == "mirror")
            {
                m_samplerState.enuAddressModeV = SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            }
            else if (strValue == "border")
            {
                m_samplerState.enuAddressModeV = SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            else
            {
                m_samplerState.enuAddressModeV = SAMPLER_ADDRESS_MODE_REPEAT;
            }
        }

        itArg2Val = mapArg2Val.find("filter");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue == "anisotropic")
            {
                auto it = mapArg2Val.find("maxanisotropy");
                if (it != mapArg2Val.end())
                {
                    const std::string& strValue2  = it->second;
                    m_samplerState.fMaxAnisotropy = (float)std::atof(strValue2.c_str());
                }
            }
        }

        itArg2Val = mapArg2Val.find("comparefunc");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue == "lequal")
            {
                m_samplerState.bCompareEnable = TRUE;
                // 目前只有阴影用到了，先这样
                m_samplerState.enuCompareFunc = gfx::SAMPLER_COMPARE_OP_LESS_OR_EQUAL;
                m_samplerState.fToMaxLod      = FLT_MAX;
            }
            else if (strValue == "gequal")
            {
                m_samplerState.bCompareEnable = TRUE;
                // 目前只有阴影用到了，先这样
                m_samplerState.enuCompareFunc = gfx::SAMPLER_COMPARE_OP_GREATER_OR_EQUAL;
                m_samplerState.fToMaxLod      = FLT_MAX;
            }
            else if (strValue == "less")
            {
                m_samplerState.bCompareEnable = TRUE;
                m_samplerState.enuCompareFunc = gfx::SAMPLER_COMPARE_OP_LESS;
            }
            else if (strValue == "equal")
            {
                m_samplerState.bCompareEnable = TRUE;
                m_samplerState.enuCompareFunc = gfx::SAMPLER_COMPARE_OP_EQUAL;
            }
        }

        itArg2Val = mapArg2Val.find("border");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue = itArg2Val->second;
            if (strValue == "1")
            {
                m_samplerState.enuBorderColor = enumBorderColor::BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            }
            else if (strValue == "0")
            {
                m_samplerState.enuBorderColor = enumBorderColor::BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            }
            else if (strValue == "2")
            {
                m_samplerState.enuBorderColor = enumBorderColor::BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            }
            else if (strValue == "3")
            {
                m_samplerState.enuBorderColor = enumBorderColor::BORDER_COLOR_INT_TRANSPARENT_BLACK;
            }
            else if (strValue == "4")
            {
                m_samplerState.enuBorderColor = enumBorderColor::BORDER_COLOR_INT_OPAQUE_BLACK;
            }
            else if (strValue == "5")
            {
                m_samplerState.enuBorderColor = enumBorderColor::BORDER_COLOR_INT_OPAQUE_WHITE;
            }
        }

        itArg2Val = mapArg2Val.find("nmiplodbias");
        if (itArg2Val != mapArg2Val.end())
        {
            const std::string& strValue  = itArg2Val->second;
            m_samplerState.fMipLodBias   = (float)std::atof(strValue.c_str());
            m_samplerState.finialMipBias = m_samplerState.fMipLodBias;
        }

        // 引擎没有单独指定enuAddressModeW的地方，就默认和m_samplerState.enuAddressModeU一样好了
        m_samplerState.enuAddressModeW = m_samplerState.enuAddressModeU;
        m_samplerState.bNeedShaderInit = false;
        m_samplerState.GetKey();
        bResult = TRUE;
    Exit0:
        return bResult;
    }

    const char* KStageSamplerDef::GetSamplerName()
    {
        return m_strSamplerName.c_str();
    }

    void KStageSamplerDef::SetSamplerName(const char* pName)
    {
        m_strSamplerName = pName;
    }

    gfx::KSamplerState* KStageSamplerDef::GetSamplerState()
    {
        return &m_samplerState;
    }
    ////////////////////////////////////////////////////////////////////

    KVulkanShaderStage::KVulkanShaderStage(int nPlatform)
    {
        ASSERT(nPlatform > 0);
        m_nPlatform          = nPlatform;
        m_pShaderProgram     = nullptr;
        m_CreatInfo          = {};
        m_specializationInfo = {};
        m_nMaterialID        = 0;

        KX3DEngineMonitor* pEngineMonitor = NSEngine::GetEngineMonitor();
        ++pEngineMonitor->m_sGraphics.nKVulkanShaderStage;
    }

    KVulkanShaderStage::~KVulkanShaderStage()
    {
        ClearSamplerDef();
        vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
        vks::KShaderProgram* pShaderProgram = m_pShaderProgram;
        if (pShaderProgram)
        {
            m_pShaderProgram                  = nullptr;
            std::string          strGroupkey  = pShaderProgram->m_shaderInfo.strGroupkey;
            gfx::ShaderStageType eShaderStage = pShaderProgram->m_shaderInfo.eShaderStage;

            int32_t val = pShaderProgram->Release();
            if (val > 0)
            {
                pVulkanDevice->RemoveShaderModule(pShaderProgram, eShaderStage, strGroupkey.c_str());
            }
            else
            {
                ASSERT(0);
                // 不应该出现这个情况吧,安全起见还是加上这个逻辑
                pVulkanDevice->RemoveDirtyShaderModules_MaybeDirtyDeletedByCreate(pShaderProgram, eShaderStage, strGroupkey.c_str());
            }
        }

        KX3DEngineMonitor* pEngineMonitor = NSEngine::GetEngineMonitor();
        --pEngineMonitor->m_sGraphics.nKVulkanShaderStage;
    }

    BOOL KVulkanShaderStage::LoadShader(
        const char*                     pcszShaderSource,
        const NSKBase::tagFileLocation& sIncludeShaderLoc,
        const char*                     pcszShaderDef,
        const char*                     pcszMacro,
        gfx::ShaderStageType            eShaderStage,
        gfx::IShaderReflector*          pReflector
    )
    {
        PROF_CPU();
        BOOL                bRet          = false;
        BOOL                bRetCode      = false;
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();

        KGLOG_ASSERT_EXIT(pcszShaderSource && pcszShaderSource[0]);

        m_shaderInfo.ustrShaderSource   = g_CachePathString(pcszShaderSource, TRUE);
        m_shaderInfo.sIncludedShaderLoc = sIncludeShaderLoc;
        m_shaderInfo.strMacro           = pcszMacro;

        ASSERT(strstr(m_shaderInfo.strMacro.c_str(), "PLATFORM") == nullptr);

        m_shaderInfo.strShaderDef = pcszShaderDef;
        m_shaderInfo.eShaderStage = eShaderStage;

        m_shaderInfo.MakeGroupKey();
        m_shaderInfo.MakeSpvPath();
        ASSERT(!m_shaderInfo.strSpvPath.empty());
        m_shaderInfo.MakeScPath();
        ASSERT(!m_shaderInfo.strScPath.empty());

        {
            bRetCode = pVulkanDevice->LoadShader(this, pcszShaderSource, sIncludeShaderLoc, pcszShaderDef, pcszMacro, eShaderStage, pReflector);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanShaderStage::CreateShader(uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector)
    {
        PROF_CPU();

        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        KGLOG_PROCESS_ERROR(!m_strShaderContent.empty());
        ASSERT(!m_pShaderProgram);

        m_pShaderProgram = nullptr;

        bRetCode = pVulkanDevice->CreateShader(&m_pShaderProgram, this, m_CreatInfo, pRetHash, pRealBuild, pReflector);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        ClearShaderContent();
        return bResult;
    }

    BOOL KVulkanShaderStage::ReCreateShader(uint32_t* pRetHash, BOOL* pRealBuild, gfx::IShaderReflector* pReflector)
    {
        PROF_CPU();

        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        KGLOG_PROCESS_ERROR(!m_strShaderContent.empty());

        bRetCode = pVulkanDevice->ReCreateShader(&m_shaderInfo, m_strShaderContent, m_CreatInfo, pRetHash, pRealBuild, pReflector);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        ClearShaderContent();
        return bResult;
    }

    VkPipelineShaderStageCreateInfo& KVulkanShaderStage::GetCreateInfo()
    {
        return m_CreatInfo;
    }

    BOOL KVulkanShaderStage::SetSpecializationMapEntry(uint32_t uStageSpecializationMapEntryCount, KSpecializationMapEntry pMapEntry[], void* pSpecializationData, uint32_t uSpecializationDataSize)
    {
        PROF_CPU_DETAIL();
        m_vecSpecializationMapEntries.resize(uStageSpecializationMapEntryCount);
        for (uint32_t i = 0; i < uStageSpecializationMapEntryCount; ++i)
        {
            VkSpecializationMapEntry entry;
            entry.constantID                 = pMapEntry[i].uConstantID;
            entry.offset                     = pMapEntry[i].uOffset;
            entry.size                       = pMapEntry[i].size;
            m_vecSpecializationMapEntries[i] = entry;
        }
        m_specializationInfo.dataSize      = uSpecializationDataSize;
        m_specializationInfo.mapEntryCount = uStageSpecializationMapEntryCount;
        m_specializationInfo.pMapEntries   = m_vecSpecializationMapEntries.data();
        m_specializationInfo.pData         = pSpecializationData;

        m_CreatInfo.pSpecializationInfo = &m_specializationInfo;
        return true;
    }

    const char* KVulkanShaderStage::GetShaderName()
    {
        return m_shaderInfo.strSpvPath.c_str();
    }

    const char* KVulkanShaderStage::GetShaderCacheFilePath()
    {
        return m_shaderInfo.strScPath.c_str();
    }

    uint32_t KVulkanShaderStage::GetPushConstantsSize()
    {
        return m_shaderInfo.uPushConstantsSize;
    }

    KShaderInfo* KVulkanShaderStage::GetShaderInfo()
    {
        return &m_shaderInfo;
    }

    uint32_t KVulkanShaderStage::GetSamplerDefCount()
    {
        return (uint32_t)m_vecStateSamplerDef.size();
    }

    IKStageSamplerDef* KVulkanShaderStage::GetSamplerDef(uint32_t i)
    {
        return m_vecStateSamplerDef[i];
    }

    IKStageSamplerDef* KVulkanShaderStage::GetSamplerDef(const char* pcszName)
    {
        PROF_CPU_DETAIL();
        KGLOG_ASSERT_EXIT(pcszName && pcszName[0]);
        for (gfx::IKStageSamplerDef* pDef : m_vecStateSamplerDef)
        {
            const char* pcszDefName = pDef->GetSamplerName();
            if (pcszDefName && strcmp(pcszDefName, pcszName) == 0)
            {
                return pDef;
            }
        }

    Exit0:
        return nullptr;
    }
    void KVulkanShaderStage::ClearSamplerDef()
    {
        PROF_CPU_DETAIL();
        for (auto it : m_vecStateSamplerDef)
        {
            SAFE_DELETE(it);
        }
        m_vecStateSamplerDef.clear();
    }

    BOOL KVulkanShaderStage::AddSamplerDef(const char* pcszSamplerDef)
    {
        PROF_CPU_DETAIL();
        BOOL              bRet     = false;
        BOOL              bRetCode = false;
        KStageSamplerDef* pDef     = new KStageSamplerDef;
        bRetCode                   = pDef->SetSamplerDef(pcszSamplerDef);
        KGLOG_PROCESS_ERROR(bRetCode);

        m_vecStateSamplerDef.push_back(pDef);

        bRet = true;
    Exit0:
        if (!bRet)
        {
            SAFE_DELETE(pDef);
        }
        return bRet;
    }

    BOOL KVulkanShaderStage::AddSamplerDef(const char* pSamplerDefName, gfx::KSamplerState* pSamplerState)
    {
#ifdef _DEBUG
        ASSERT(GetSamplerDef(pSamplerDefName) == nullptr);
#endif

        KStageSamplerDef* pDef = new KStageSamplerDef;
        pDef->SetSamplerName(pSamplerDefName);
        gfx::KSamplerState* pState = pDef->GetSamplerState();
        *pState                    = *pSamplerState;
        m_vecStateSamplerDef.emplace_back(pDef);
        return true;
    }

    void KVulkanShaderStage::SetShaderContent(const char* strMacro, const char* strHeader, const char* strBody)
    {
        m_strShaderMacro  = strMacro;
        m_strShaderHeader = strHeader;
        m_strShaderBody   = strBody;

        ASSERT(m_nPlatform > 0);
        {
            char szPlatformMacro[64]{""};

            snprintf(szPlatformMacro, sizeof(szPlatformMacro) - 1, "#define PLATFORM %d\r\n", m_nPlatform);
            szPlatformMacro[countof(szPlatformMacro) - 1] = 0;

            m_strShaderHeader.append(szPlatformMacro);
        }

        if (DrvOption::bReversePerspectiveDepthZ)
        {
            m_strShaderContent = m_strShaderHeader;
            m_strShaderContent.append("#define REVERSE_DEPTH_Z\r\n");

            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                m_strShaderContent.append("#define SHADER_MACRO_TO_SPECIALIZATION_CONSTANTS_ENABLE\r\n");
            }
            
            switch(DrvOption::GetRenderApi())
            {            
            case GFX_API::GFX_DX12_API:
                m_strShaderContent.append("#define __RHI_API 0 \r\n");                
                break;
            case GFX_API::GFX_VULKAN_API:
                m_strShaderContent.append("#define __RHI_API 1 \r\n");
                break;
            case GFX_API::GFX_PSGNM_API:
                m_strShaderContent.append("#define __RHI_API 2 \r\n");
                break;
            case GFX_API::GFX_METAL_API:
                m_strShaderContent.append("#define __RHI_API 3 \r\n");
                break;
            default:
                ASSERT(0);
                break;
            }

            if (DrvOption::bHLSLFunction_NoInline)
            {
                m_strShaderContent.append("#define  NO_INLINE  [noinline]\r\n");
            }
            else
            {
                m_strShaderContent.append("#define  NO_INLINE \r\n");
            }

#ifdef _WIN32
            if (DrvOption::uVulkanVersion == VK_API_VERSION_1_0)
            {
                m_strShaderContent.append("#define X3D_VULKAN_1_0\r\n");
            }
#endif

            m_strShaderContent.append(m_strShaderMacro);
            m_strShaderContent.append(m_strShaderBody);
        }
        else
        {
            // m_strShaderContent = m_strShaderHeader + m_strShaderMacro + m_strShaderBody;
            m_strShaderContent = m_strShaderHeader;
            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                m_strShaderContent.append("#define SHADER_MACRO_TO_SPECIALIZATION_CONSTANTS_ENABLE\r\n");
            }

            if (DrvOption::bHLSLFunction_NoInline)
            {
                m_strShaderContent.append("#define  NO_INLINE  [noinline]\r\n");
            }
            else
            {
                m_strShaderContent.append("#define  NO_INLINE \r\n");
            }

#ifdef _WIN32
            if (DrvOption::uVulkanVersion == VK_API_VERSION_1_0)
            {
                m_strShaderContent.append("#define X3D_VULKAN_1_0\r\n");
            }
#endif
            m_strShaderContent.append("#define VKSPV\r\n");

            m_strShaderContent.append(m_strShaderMacro);
            m_strShaderContent.append(m_strShaderBody);
        }
    }

    void KVulkanShaderStage::SetShaderBody(std::string&& strBody)
    {
        m_strShaderBody = std::move(strBody);

        if (DrvOption::bReversePerspectiveDepthZ)
        {
            m_strShaderContent = m_strShaderHeader;
            m_strShaderContent.append("#define REVERSE_DEPTH_Z\r\n");

#ifdef _WIN32
            if (DrvOption::uVulkanVersion == VK_API_VERSION_1_0)
            {
                m_strShaderContent.append("#define X3D_VULKAN_1_0\r\n");
            }
#endif

            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                m_strShaderContent.append("#define SHADER_MACRO_TO_SPECIALIZATION_CONSTANTS_ENABLE\r\n");
            }

            if (DrvOption::bHLSLFunction_NoInline)
            {
                m_strShaderContent.append("#define  NO_INLINE  [noinline]\r\n");
            }
            else
            {
                m_strShaderContent.append("#define  NO_INLINE \r\n");
            }

            m_strShaderContent.append(m_strShaderMacro);
            m_strShaderContent.append(m_strShaderBody);
        }
        else
        {
            // m_strShaderContent = m_strShaderHeader + m_strShaderMacro + m_strShaderBody;
            m_strShaderContent = m_strShaderHeader;
#ifdef _WIN32
            if (DrvOption::uVulkanVersion == VK_API_VERSION_1_0)
            {
                m_strShaderContent.append("#define X3D_VULKAN_1_0\r\n");
            }
#endif
            if (DrvOption::bMacroToSpicalizationConstantsEnable)
            {
                m_strShaderContent.append("#define SHADER_MACRO_TO_SPECIALIZATION_CONSTANTS_ENABLE\r\n");
            }

            if (DrvOption::bHLSLFunction_NoInline)
            {
                m_strShaderContent.append("#define  NO_INLINE  [noinline]\r\n");
            }
            else
            {
                m_strShaderContent.append("#define  NO_INLINE \r\n");
            }

            m_strShaderContent.append(m_strShaderMacro);
            m_strShaderContent.append(m_strShaderBody);
        }
    }

    void KVulkanShaderStage::ClearShaderContent()
    {
        m_strShaderMacro.clear();
        m_strShaderMacro.shrink_to_fit();

        m_strShaderHeader.clear();
        m_strShaderHeader.shrink_to_fit();

        m_strShaderBody.clear();
        m_strShaderBody.shrink_to_fit();

        m_strShaderContent.clear();
        m_strShaderContent.shrink_to_fit();

        m_strShaderFileSaveData.clear();
        m_strShaderFileSaveData.shrink_to_fit();

        m_shaderInfo.strScPath.clear();
        m_shaderInfo.strScPath.shrink_to_fit();
    }

    void KVulkanShaderStage::SetShaderFileLoadFromCache()
    {
        m_shaderInfo.bFromCachedShaderFile = TRUE;
    }

    BOOL KVulkanShaderStage::IsShaderFileLoadFromCache()
    {
        return m_shaderInfo.bFromCachedShaderFile;
    }

    void KVulkanShaderStage::SetShaderFileSaveData(std::string&& strData)
    {
        m_strShaderFileSaveData = std::move(strData);
    }

    const std::string& KVulkanShaderStage::GetShaderFileSaveData()
    {
        return m_strShaderFileSaveData;
    }

    void KVulkanShaderStage::SetEntryPoint(const char* szEntryPoint)
    {
        m_shaderInfo.strEntryPoint = szEntryPoint;
    }
    const char* KVulkanShaderStage::GetEntryPoint()
    {
        return m_shaderInfo.strEntryPoint.c_str();
    }

    void* KVulkanShaderStage::MoveOutShaderModule()
    {
        VkShaderModule pModule = nullptr;
        if (m_pShaderProgram)
        {
            pModule = m_pShaderProgram->m_pModule;
            m_pShaderProgram->m_pModule = nullptr;
        }
        return pModule;
    }

    const std::string& KVulkanShaderStage::GetShaderContent()
    {
        return m_strShaderContent;
    }

    const std::string& KVulkanShaderStage::GetShaderMacro()
    {
        return m_strShaderMacro;
    }

    const std::string& KVulkanShaderStage::GetShaderHeader()
    {
        return m_strShaderHeader;
    }

    const std::string& KVulkanShaderStage::GetShaderBody()
    {
        return m_strShaderBody;
    }

    ////////////////////////////////////////////////////////////////////

    void KSpecializationInfo::Clear()
    {
        m_item.clear();
    }

    void KSpecializationInfo::AddItem(const KSpecializationConstant& item)
    {
        m_item.push_back(item);
    }

    KSpecializationInfo::KSpecializationInfo()
    {
        m_pData = nullptr;
    }

    KSpecializationInfo::~KSpecializationInfo()
    {
        SAFE_DELETE_ARRAY(m_pData);
    }

    VkSpecializationInfo* KSpecializationInfo::Build()
    {
        VkSpecializationInfo* pRetInfo = nullptr;
        uint32_t              uCount   = (uint32_t)m_item.size();
        if (uCount)
        {
            SAFE_DELETE_ARRAY(m_pData);
            uint32_t uBytes = 0;
            for (uint32_t i = 0; i < uCount; ++i)
            {
                const KSpecializationConstant& item  = m_item[i];
                uBytes                              += item.size;
            }
            m_pData = new uint8_t[uBytes];

            m_entry.clear();
            uint32_t uBytePos = 0;
            for (uint32_t i = 0; i < uCount; ++i)
            {
                const KSpecializationConstant& item = m_item[i];
                VkSpecializationMapEntry       entry;
                entry.constantID = item.constant_id;
                entry.size       = item.size;
                entry.offset     = uBytePos;
                memcpy(&m_pData[uBytePos], &item.nValue, item.size);
                uBytePos += (uint32_t)entry.size;
                m_entry.push_back(entry);
            }

            m_info.dataSize      = uBytes;
            m_info.mapEntryCount = (uint32_t)m_entry.size();
            m_info.pMapEntries   = m_entry.data();
            m_info.pData         = m_pData;
            pRetInfo             = &m_info;
        }
        return pRetInfo;
    }


    ////////////////////////////////////////////////////////////////////
    KSpecializationConstantContainer::KSpecializationConstantContainer()
    {
    }
    KSpecializationConstantContainer::~KSpecializationConstantContainer()
    {
    }

    void KSpecializationConstantContainer::AddFloat(uint32_t stageId, uint32_t constant_id, float floatValue)
    {
        KSpecializationConstant item;
        item.shaderStage_id = stageId;
        item.constant_id    = constant_id;
        item.constant_type  = FLOAT_CONSTANT_TYPE;
        item.size           = sizeof(float);
        item.fValue         = floatValue;
        m_items.push_back(item);
    }

    void KSpecializationConstantContainer::AddInt(uint32_t stageId, uint32_t constant_id, int32_t intValue)
    {
        KSpecializationConstant item;
        item.shaderStage_id = stageId;
        item.constant_id    = constant_id;
        item.constant_type  = INT_CONSTANT_TYPE;
        item.size           = sizeof(float);
        item.nValue         = intValue;
        m_items.push_back(item);
    }

    void KSpecializationConstantContainer::AddUInt(uint32_t stageId, uint32_t constant_id, uint32_t uintValue)
    {
        KSpecializationConstant item;
        item.shaderStage_id = stageId;
        item.constant_id    = constant_id;
        item.constant_type  = UINT_CONSTANT_TYPE;
        item.size           = sizeof(float);
        item.uValue         = uintValue;
        m_items.push_back(item);
    }

    const KSpecializationConstant& KSpecializationConstantContainer::GetItem(uint32_t i)
    {
        ASSERT(i < m_items.size());
        return m_items[i];
    }

    uint32_t KSpecializationConstantContainer::GetItemCount()
    {
        return (uint32_t)m_items.size();
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanGraphicsPipeline::KVulkanGraphicsPipeline()
    {
        m_RenderState.ResetDefaultValue();
        m_pVertexDescriptor = nullptr;
        m_pPipeline         = nullptr;

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->m_sGraphics.nVkGraphicsPipelineCount;
    }

    KVulkanGraphicsPipeline::~KVulkanGraphicsPipeline()
    {
        PROF_CPU();
        Destroy();

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->m_sGraphics.nVkGraphicsPipelineCount;
    }

    BOOL KVulkanGraphicsPipeline::Create(GraphicsPipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer)
    {
        PROF_CPU();
        BOOL                              bRet           = FALSE;
        KSpecializationConstantContainer* pContainer     = (KSpecializationConstantContainer*)pSpecializationConstantContainer;
        uint64_t                          renderPassHash = 0;
        uint32_t                          uMaxStageId    = 0;
        VkResult                          hRetCode       = VK_INCOMPLETE;
        VkDevice                          pDevice        = GetVkDevice();
        vks::KVulkanDevice*               pVulkanDevice  = GetVulkanDevice();
        KGraphicDevice*                   pGraphicDevice = GetGraphicDevice();

        VkPipelineInputAssemblyStateCreateInfo           inputAssemblyState{};
        VkPipelineRasterizationStateCreateInfo           rasterizationState{};
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates{};
        VkPipelineColorBlendStateCreateInfo              colorBlendState{};
        VkPipelineDepthStencilStateCreateInfo            depthStencilState{};
        VkPipelineViewportStateCreateInfo                viewportState{};
        VkPipelineMultisampleStateCreateInfo             multisampleState{};
        std::vector<VkDynamicState>                      dynamicStateEnables;
        VkPipelineDynamicStateCreateInfo                 dynamicState{};
        VkGraphicsPipelineCreateInfo                     pipelineCreateInfo{};
        VkViewport                                       viewport{};
        VkRect2D                                         scissorRect{};
        std::vector<VkPipelineShaderStageCreateInfo>     vecShaderCreateInfo;
        KVulkanLayout*                                   pLayout                = nullptr;
        VkDynamicState                                   dynamicStateEnables1[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkDynamicState dynamicStateEnables2[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS
        };

        uint32_t     uRenderTargetCount = 0;
        KVulkanRenderPass* pRenderPass  = pDesc->pRenderPass;

        VkRenderPass pVkRenderPass = VK_NULL_HANDLE;
        pVkRenderPass              = pRenderPass->GetPass();

        m_RenderState       = *(pDesc->pRenderState);
        m_pVertexDescriptor = pDesc->pVertexDescriptor;

        uRenderTargetCount  = pRenderPass->GetDesc()->uRenderTargetCount;

        if (pContainer)
        {
            for (const auto& it : pContainer->m_items)
            {
                if (it.shaderStage_id > uMaxStageId)
                {
                    uMaxStageId = it.shaderStage_id;
                }
            }
            ASSERT(uMaxStageId < pDesc->uStageCount);
            pContainer->m_Info.resize(pDesc->uStageCount);
        }

        for (uint32_t i = 0; i < pDesc->uStageCount; ++i)
        {
            KVulkanShaderStage*             pVulkanShaderStage = (KVulkanShaderStage*)pDesc->pStage[i];
            VkPipelineShaderStageCreateInfo info               = pVulkanShaderStage->GetCreateInfo();

            if (pContainer)
            {
                uint32_t uItemCount = pContainer->GetItemCount();
                BOOL     bAdded     = false;
                pContainer->m_Info[i].Clear();
                for (uint32_t j = 0; j < uItemCount; ++j)
                {
                    const KSpecializationConstant& constant = pContainer->GetItem(j);
                    ASSERT(constant.shaderStage_id < pDesc->uStageCount);
                    if (constant.shaderStage_id == i)
                    {
                        pContainer->m_Info[i].AddItem(constant);
                        bAdded = true;
                    }
                }
                if (bAdded)
                {
                    VkSpecializationInfo* pVkspecializationInfo = pContainer->m_Info[i].Build();
                    if (pVkspecializationInfo)
                    {
                        info.pSpecializationInfo = pVkspecializationInfo;
                    }
                }
            }
            vecShaderCreateInfo.push_back(info);
        }

        // for (uint32_t i = 0; i < pDesc->uStageCount; ++i)
        //{
        //	KVulkanShaderStage* pVulkanShaderStage = (KVulkanShaderStage*)pDesc->pStage[i];


        //	pipelineCreateInfo.stageCount = static_cast<uint32_t>(vecShaderCreateInfo.size());
        //	pipelineCreateInfo.pStages = vecShaderCreateInfo.data();

        //	//if (pSpecializationConstantContainer)
        //	//{
        //	//	uint32_t uItemCount = pSpecializationConstantContainer->GetItemCount();
        //	//	for (uint32_t i = 0; i < uItemCount; ++i)
        //	//	{
        //	//		const KSpecializationConstant& pConstant = pSpecializationConstantContainer->GetItem(i);

        //	//	}
        //	//}

        //	//VkPipelineShaderStageCreateInfo info = pVulkanShaderStage->GetCreateInfo();
        //	vecShaderCreateInfo.push_back(pVulkanShaderStage->GetCreateInfo());
        //}

        // IA
        {
            inputAssemblyState.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssemblyState.pNext                  = nullptr;
            inputAssemblyState.flags                  = 0;
            inputAssemblyState.topology               = GetPrimitiveTopology(m_RenderState.drawMode);
            inputAssemblyState.primitiveRestartEnable = VK_FALSE;
        }

        // RS
        {
            rasterizationState.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizationState.pNext                   = nullptr;
            rasterizationState.flags                   = 0;
            rasterizationState.polygonMode             = GetPolygonMode(m_RenderState.polygonMode);
            rasterizationState.cullMode                = GetCullMode(m_RenderState.cullMode);
            rasterizationState.frontFace               = GetFrontFaceMode(m_RenderState.frontFaceMode);
            rasterizationState.depthClampEnable        = m_RenderState.depthClampEnable;
            rasterizationState.rasterizerDiscardEnable = m_RenderState.rasterizerDiscardEnable;
            rasterizationState.depthBiasEnable         = m_RenderState.depthBiasEnable;
            rasterizationState.depthBiasConstantFactor = m_RenderState.depthBiasConstantFactor;
            rasterizationState.depthBiasClamp          = m_RenderState.depthBiasClamp;
            rasterizationState.depthBiasSlopeFactor    = m_RenderState.depthBiasSlopeFactor;
            rasterizationState.lineWidth               = m_RenderState.lineWidth;
        }

        // color blend
        {
            // if(m_RenderState.blendAttachCount == 0)
            //{
            //	int x = 0;
            // }
            //  至少绑定了一个target吧
            // KGLOG_PROCESS_ERROR(m_RenderState.blendAttachCount > 0);
            VkPipelineColorBlendAttachmentState blendAttachmentState0;
            for (unsigned i = 0; i < KMAX_BLEND_ATTACHMENT && i < m_RenderState.blendAttachCount && i < uRenderTargetCount; ++i)
            {
                VkPipelineColorBlendAttachmentState blendAttachmentState;
                auto&                               att = m_RenderState.blendAttachment[i];
                blendAttachmentState.blendEnable        = att.blendEnable;
                blendAttachmentState.colorWriteMask     = 0;
                if (att.writeR)
                {
                    blendAttachmentState.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
                }
                if (att.writeG)
                {
                    blendAttachmentState.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
                }
                if (att.writeB)
                {
                    blendAttachmentState.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
                }
                if (att.writeA)
                {
                    blendAttachmentState.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
                }
                blendAttachmentState.srcColorBlendFactor = GetBlendFactor(att.srcColorBlendFactor);
                blendAttachmentState.dstColorBlendFactor = GetBlendFactor(att.dstColorBlendFactor);
                blendAttachmentState.srcAlphaBlendFactor = GetBlendFactor(att.srcAlphaBlendFactor);
                blendAttachmentState.dstAlphaBlendFactor = GetBlendFactor(att.dstAlphaBlendFactor);
                blendAttachmentState.colorBlendOp        = GetBlendOp(att.colorBlendOp);
                blendAttachmentState.alphaBlendOp        = GetBlendOp(att.alphaBlendOp);
                blendAttachmentStates.push_back(blendAttachmentState);
                if (i == 0)
                {
                    blendAttachmentState0 = blendAttachmentState;
                }
            }

            if (m_RenderState.blendAttachCount == 0)
            {
                ASSERT(uRenderTargetCount == 0);
            }

            // 如果后面的没指定blend方式，那么默认都用第1个的blend方式来填充后面的target，这样保证每个target都指定了blend方式
            int32_t left = (int32_t)uRenderTargetCount - (int32_t)blendAttachmentStates.size();
            if (left > 0)
            {
                for (int32_t i = 0; i < left; ++i)
                {
                    blendAttachmentStates.push_back(blendAttachmentState0);
                }
            }

            colorBlendState.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendState.pNext           = nullptr;
            colorBlendState.flags           = 0;
            colorBlendState.attachmentCount = uRenderTargetCount;
            colorBlendState.pAttachments    = uRenderTargetCount == 0 ? nullptr : &blendAttachmentStates[0];
            colorBlendState.logicOp         = VK_LOGIC_OP_CLEAR;
            colorBlendState.logicOpEnable   = VK_FALSE;
        }

        // depthstencil
        {
            depthStencilState.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencilState.pNext                 = nullptr;
            depthStencilState.flags                 = 0;
            depthStencilState.depthTestEnable       = m_RenderState.depthTestEnable;
            depthStencilState.depthWriteEnable      = m_RenderState.depthWriteEnable;
            depthStencilState.depthCompareOp        = GetDepthCompareOp(m_RenderState.depthCompareOp);
            depthStencilState.depthBoundsTestEnable = m_RenderState.depthBoundsTestEnable;
            depthStencilState.stencilTestEnable     = m_RenderState.stencilTestEnable;

            depthStencilState.front.compareMask = m_RenderState.stencilFront.compareMask;
            depthStencilState.front.writeMask   = m_RenderState.stencilFront.writeMask;
            depthStencilState.front.reference   = m_RenderState.stencilFront.reference;
            depthStencilState.front.compareOp   = GetStencilCompareOp(m_RenderState.stencilFront.stencilCompareOp);
            depthStencilState.front.failOp      = GetStencilOp(m_RenderState.stencilFront.sencilFailOp);
            depthStencilState.front.passOp      = GetStencilOp(m_RenderState.stencilFront.stencilPassOp);
            depthStencilState.front.depthFailOp = GetStencilOp(m_RenderState.stencilFront.stencilDepthFailOp);

            depthStencilState.back.compareMask = m_RenderState.stencilBack.compareMask;
            depthStencilState.back.writeMask   = m_RenderState.stencilBack.writeMask;
            depthStencilState.back.reference   = m_RenderState.stencilBack.reference;
            depthStencilState.back.compareOp   = GetStencilCompareOp(m_RenderState.stencilBack.stencilCompareOp);
            depthStencilState.back.failOp      = GetStencilOp(m_RenderState.stencilBack.sencilFailOp);
            depthStencilState.back.passOp      = GetStencilOp(m_RenderState.stencilBack.stencilPassOp);
            depthStencilState.back.depthFailOp = GetStencilOp(m_RenderState.stencilBack.stencilDepthFailOp);
            depthStencilState.minDepthBounds   = m_RenderState.minDepthBounds;
            depthStencilState.maxDepthBounds   = m_RenderState.maxDepthBounds;
        }

        // view state
        {
            viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.pNext         = nullptr;
            viewportState.flags         = 0;
            viewportState.viewportCount = 1;
            viewportState.scissorCount  = 1;
            if (m_RenderState.defaultViewPortEnable)
            {
                viewportState.pViewports = nullptr;
            }
            else
            {
                viewport.x               = m_RenderState.viewPort.x;
                viewport.y               = m_RenderState.viewPort.y;
                viewport.width           = m_RenderState.viewPort.width;
                viewport.height          = m_RenderState.viewPort.height;
                viewport.minDepth        = m_RenderState.viewPort.minDepth;
                viewport.maxDepth        = m_RenderState.viewPort.maxDepth;
                viewportState.pViewports = &viewport;
            }
            if (m_RenderState.defaultScissorEnable)
            {
                viewportState.pScissors = nullptr;
            }
            else
            {
                scissorRect.offset.x      = m_RenderState.scissor.offsetX;
                scissorRect.offset.y      = m_RenderState.scissor.offsetY;
                scissorRect.extent.width  = m_RenderState.scissor.extendWidth;
                scissorRect.extent.height = m_RenderState.scissor.extendHeight;
                viewportState.pScissors   = &scissorRect;
            }
        }

        // multisamplestate
        {
            multisampleState.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampleState.pNext                 = nullptr;
            multisampleState.flags                 = 0;
            multisampleState.sampleShadingEnable   = m_RenderState.sampleShadingEnable;
            multisampleState.rasterizationSamples  = GetSamplerCount(m_RenderState.sampleCountFlag);
            multisampleState.minSampleShading      = m_RenderState.minSampleShading;
            multisampleState.pSampleMask           = !m_RenderState.sampleMask ? VK_NULL_HANDLE : (VkSampleMask*)(&(m_RenderState.sampleMask));
            multisampleState.alphaToCoverageEnable = m_RenderState.sampleAlphaToCoverageEnable;
            multisampleState.alphaToOneEnable      = m_RenderState.sampleAlphaToOneEnable;
        }

        // dynamicStateEnable
        {
            if (m_RenderState.drawMode == PT_LINE_LIST)
            {
                dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicState.pNext             = nullptr;
                dynamicState.flags             = 0;
                dynamicState.pDynamicStates    = dynamicStateEnables1;
                dynamicState.dynamicStateCount = _countof(dynamicStateEnables1);
            }
            else
            {
                dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicState.pNext             = nullptr;
                dynamicState.flags             = 0;
                dynamicState.pDynamicStates    = dynamicStateEnables2;
                dynamicState.dynamicStateCount = _countof(dynamicStateEnables2);
            }
        }

        pLayout = pDesc->pLayout;
        KGLOG_PROCESS_ERROR(pLayout);

        pipelineCreateInfo = vks::initializers::PipelineCreateInfo(pLayout->GetPipelineLayout(), pVkRenderPass, 0);

        // 缺tessellation的部分
        pipelineCreateInfo.pVertexInputState   = ((KVulkanVertexDescriptor*)pDesc->pVertexDescriptor)->GetInputStateCreateInfo();
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState    = &colorBlendState;
        pipelineCreateInfo.pMultisampleState   = &multisampleState;
        pipelineCreateInfo.pViewportState      = &viewportState;
        pipelineCreateInfo.pDepthStencilState  = &depthStencilState;
        pipelineCreateInfo.pDynamicState       = &dynamicState;

        pipelineCreateInfo.stageCount = static_cast<uint32_t>(vecShaderCreateInfo.size());
        pipelineCreateInfo.pStages    = vecShaderCreateInfo.data();

        hRetCode = vks::vkCreateGraphicsPipelines(pDevice, pVulkanDevice->m_pPipelineCache, 1, &pipelineCreateInfo, nullptr, &m_pPipeline);
        if (hRetCode != VK_SUCCESS && hRetCode != VK_PIPELINE_COMPILE_REQUIRED_EXT)
        {
            if (hRetCode == VK_ERROR_OUT_OF_HOST_MEMORY)
            {
                KGLogPrintf(KGLOG_ERR, "KVulkanGraphicsPipeline::Create out of host memory");
            }
            else if (hRetCode == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                KGLogPrintf(KGLOG_ERR, "KVulkanGraphicsPipeline::Create out of device memory");
            }
            else if (hRetCode == VK_ERROR_INVALID_SHADER_NV)
            {
                KGLogPrintf(KGLOG_ERR, "KVulkanGraphicsPipeline::Create invalide shader");
            }
            else
            {
                KGLogPrintf(KGLOG_ERR, "KVulkanGraphicsPipeline::Create failed:%d", hRetCode);
            }

            goto Exit0;
        }

        // Exit1:
        bRet = true;
    Exit0:
        //// release temp renderpass
        // if (pRenderPass)
        //{
        //   pRenderPass->Destroy();
        //   SAFE_DELETE(pRenderPass);
        // }
        return bRet;
    }

    BOOL KVulkanGraphicsPipeline::Destroy()
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        if (m_pPipeline)
        {
            VkDevice pDevice = GetVkDevice();
            vks::vkDestroyPipeline(pDevice, m_pPipeline, nullptr);

            m_pPipeline = VK_NULL_HANDLE;
        }
        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    VkPipeline KVulkanGraphicsPipeline::GetPipeline()
    {
        return m_pPipeline;
    }

    void* KVulkanGraphicsPipeline::GetVkPipeline()
    {
        return m_pPipeline;
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanComputePipeline::KVulkanComputePipeline()
    {
        m_pPipeline = nullptr;
    }

    KVulkanComputePipeline::~KVulkanComputePipeline()
    {
        if (m_pPipeline)
        {
            VkDevice pDevice = GetVkDevice();
            vks::vkDestroyPipeline(pDevice, m_pPipeline, nullptr);
        }
    }

    BOOL KVulkanComputePipeline::Create(ComputePipelineDesc* pDesc, IKSpecializationConstantContainer* pSpecializationConstantContainer)
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        VkResult            hRetCode      = VK_INCOMPLETE;
        VkDevice            pDevice       = GetVkDevice();
        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();

        KSpecializationConstantContainer* pContainer                = (KSpecializationConstantContainer*)pSpecializationConstantContainer;
        KVulkanLayout*                    pVulkanLayout             = pDesc->pLayout;
        VkPipelineLayout                  pPipelineLayout           = pVulkanLayout->GetPipelineLayout();
        VkComputePipelineCreateInfo       computePipelineCreateInfo = vks::initializers::ComputePipelineCreateInfo(pPipelineLayout, 0);
        KVulkanShaderStage*               pVulkanShaderStage        = (KVulkanShaderStage*)pDesc->pStage;
        computePipelineCreateInfo.stage                             = pVulkanShaderStage->GetCreateInfo();

        if (pContainer)
        {
            pContainer->m_Info.resize(1);

            const int nStageIndex = 0;
            uint32_t  uItemCount  = pContainer->GetItemCount();
            BOOL      bAdded      = false;
            for (uint32_t i = 0; i < uItemCount; ++i)
            {
                const KSpecializationConstant& constant = pContainer->GetItem(i);
                if (constant.shaderStage_id == nStageIndex)
                {
                    pContainer->m_Info[nStageIndex].AddItem(constant);
                    bAdded = true;
                }
            }
            if (bAdded)
            {
                computePipelineCreateInfo.stage.pSpecializationInfo = pContainer->m_Info[nStageIndex].Build();
            }
        }

        hRetCode = vks::vkCreateComputePipelines(pDevice, pVulkanDevice->m_pPipelineCache, 1, &computePipelineCreateInfo, nullptr, &m_pPipeline);
        KGLOG_PROCESS_ERROR(hRetCode == VK_SUCCESS);

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanComputePipeline::Destroy()
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        if (m_pPipeline)
        {
            VkDevice pDevice = GetVkDevice();
            vks::vkDestroyPipeline(pDevice, m_pPipeline, nullptr);

            m_pPipeline = VK_NULL_HANDLE;
        }
        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    VkPipeline KVulkanComputePipeline::GetPipeline()
    {
        return m_pPipeline;
    }

    void* KVulkanComputePipeline::GetVkPipeline()
    {
        return m_pPipeline;
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanRenderPass::KVulkanRenderPass()
    {
        m_pRenderPass     = VK_NULL_HANDLE;
        m_uRenderPassHash = 0;
#if memlect_RenderPass
        static uint32_t uCounter = 0;
        uCounter++;
        m_pMemLectBuffer = new uint8_t[uCounter];
        if (uCounter == 9)
        {
            int x = 0;
        }
#endif
    }

    KVulkanRenderPass::~KVulkanRenderPass()
    {
        PROF_CPU();
        Destroy();
#if memlect_RenderPass
        SAFE_DELETE_ARRAY(m_pMemLectBuffer);
#endif
    }

    BOOL KVulkanRenderPass::Destroy()
    {
        PROF_CPU();
        BOOL bRet = FALSE;

        if (m_pRenderPass)
        {
            VkDevice pDevice = GetVkDevice();
            vks::vkDestroyRenderPass(pDevice, m_pRenderPass, nullptr);
            m_pRenderPass = VK_NULL_HANDLE;
        }
        bRet = TRUE;
        // Exit0:
        return bRet;
    }

    // BOOL KVulkanRenderPass::Create(KEnumRenderPass uRenderPassId)
    //{
    //   BOOL bRet = false;
    //   m_nRenderPassId = uRenderPassId;
    //   if (!m_pRenderPass)
    //   {
    //       switch (uRenderPassId)
    //       {
    //       case gfx::RENDER_PASS_MAIN:
    //           bRet = CreateMainRenderPass();
    //           break;
    //       case gfx::RENDER_PASS_SCREEN_OFFSET:
    //           bRet = CreateOffsetRenderPass();
    //           break;
    //       default:
    //           break;
    //       }
    //   }
    //   return bRet;
    // }

    BOOL KVulkanRenderPass::IsDepthReadOnly()
    {
        return m_desc.bDepthReadOnly;
    }

    BOOL KVulkanRenderPass::IsHasDepth()
    {
        return m_desc.bHasDepth;
    }
    BOOL KVulkanRenderPass::IsHasStencil()
    {
        return m_desc.bHasStencil;
    }

    BOOL KVulkanRenderPass::Create(KRenderPassDesc* pDesc)
    {
        PROF_CPU();
        BOOL     bRet         = false;
        VkDevice pDevice      = GetVkDevice();
        VkResult hResultCode  = VK_INCOMPLETE;
        BOOL     bColorAttach = FALSE;
        BOOL     bDepth       = false;
        BOOL     bStencil     = false;
        uint32_t bytesStride  = 0;

        m_desc = *pDesc;

        uint32_t colorAttachmentCount = pDesc->uRenderTargetCount;
        uint32_t depthAttachmentCount = (pDesc->enuDepthStencilFormat != enumTextureFormat::TEX_FORMAT_NONE) ? 1 : 0;

        std::vector<VkAttachmentDescription> attachments;
        attachments.resize(colorAttachmentCount + depthAttachmentCount);

        std::vector<VkAttachmentReference> color_attachment_refs;
        color_attachment_refs.resize(colorAttachmentCount);

        VkAttachmentReference depth_stencil_attachment_ref = {};

        // VkSampleCountFlagBits sample_count = GetSamplerCount(pDesc->enuSampleCount);

        for (uint32_t i = 0; i < colorAttachmentCount; ++i)
        {
            const uint32_t ssidx = i;

            // descriptions
            attachments[ssidx].flags          = DrvOption::bIsPvr ? 0 : VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
            attachments[ssidx].format         = GetTextureFormatFromTargetFormat(pDesc->vecColorFormats[i], bColorAttach, bDepth, bStencil, bytesStride);
            attachments[ssidx].samples        = GetSamplerCount(pDesc->vecSampleCount[i]);
            attachments[ssidx].loadOp         = pDesc->vecLoadActionsColor.size() ? gVkAttachmentLoadOpTranslator[pDesc->vecLoadActionsColor[i]] : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[ssidx].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[ssidx].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[ssidx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[ssidx].initialLayout  = pDesc->vecColorInitImageLayout.size() ? GetImageLayout(pDesc->vecColorInitImageLayout[i]) : VK_IMAGE_LAYOUT_UNDEFINED;

            ///// 这里这个layout怎么定
            attachments[ssidx].finalLayout = pDesc->vecColorFinalImageLayout.size() ? GetImageLayout(pDesc->vecColorFinalImageLayout[i]) : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // references
            color_attachment_refs[i].attachment = ssidx;
            color_attachment_refs[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        // Depth stencil
        if (depthAttachmentCount > 0)
        {
            uint32_t idx            = colorAttachmentCount;
            attachments[idx].flags  = DrvOption::bIsPvr ? 0 : VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
            attachments[idx].format = GetTextureFormatFromTargetFormat(pDesc->enuDepthStencilFormat, bColorAttach, bDepth, bStencil, bytesStride);

            m_desc.bHasDepth   = true;
            m_desc.bHasStencil = bStencil;

            attachments[idx].samples       = GetSamplerCount(pDesc->depthSampleCount);
            attachments[idx].loadOp        = gVkAttachmentLoadOpTranslator[pDesc->enuLoadActionDepth];
            attachments[idx].stencilLoadOp = gVkAttachmentLoadOpTranslator[pDesc->enuLoadActionStencil];
#if 0
            attachments[idx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
#else
            attachments[idx].storeOp        = gVkAttachmentStoreOpTranslator[pDesc->enuStoreActionDepth];
            attachments[idx].stencilStoreOp = gVkAttachmentStoreOpTranslator[pDesc->enuStoreActionStencil];
#endif
            attachments[idx].initialLayout = GetImageLayout(pDesc->depthInitLayout);
            // attachments[idx].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;                               // We don't care about initial layout of the attachment
            // attachments[idx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            attachments[idx].finalLayout            = GetImageLayout(pDesc->depthfinalLayout); // Attachment will be transitioned to shader read at render pass end
            depth_stencil_attachment_ref.attachment = idx;
            // depth_stencil_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            if (pDesc->bDepthReadOnly)
            {
                // 只读深度，设置成这个属性，绑定的depth还能在这个pass里面去采样，特别适合半透明绘制流程的使用
                depth_stencil_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            }
            else
            {
                depth_stencil_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
        }

        DECLARE_ZERO(VkSubpassDescription, subpass);
        subpass.flags                = 0;
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments    = NULL;
        subpass.colorAttachmentCount = colorAttachmentCount;
        subpass.pColorAttachments    = color_attachment_refs.data();
        subpass.pResolveAttachments  = NULL;
        if (depthAttachmentCount)
        {
            subpass.pDepthStencilAttachment = &depth_stencil_attachment_ref;
        }
        else
        {
            subpass.pDepthStencilAttachment = nullptr;
        }
        subpass.preserveAttachmentCount = 0;
        subpass.pPreserveAttachments    = NULL;

        uint32_t attachment_count  = colorAttachmentCount;
        attachment_count          += depthAttachmentCount;
        ASSERT(attachment_count <= 8); // 不少硬件(2022/16.3%)只支持4个InputAttacnment.

        // Subpass dependencies for layout transitions
        /*std::array<VkSubpassDependency, 2> dependencies;

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        */

        DECLARE_ZERO(VkRenderPassCreateInfo, create_info);
        create_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create_info.pNext           = NULL;
        create_info.flags           = 0;
        create_info.attachmentCount = attachment_count;
        create_info.pAttachments    = attachments.data();

        create_info.subpassCount = 1;
        create_info.pSubpasses   = &subpass;

        create_info.dependencyCount = 0;       // static_cast<uint32_t>(dependencies.size());
        create_info.pDependencies   = nullptr; // dependencies.data();


        hResultCode = vks::vkCreateRenderPass(pDevice, &create_info, nullptr, &m_pRenderPass);
        KGLOG_COM_PROCESS_ERROR(hResultCode);

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    VkRenderPass KVulkanRenderPass::GetPass()
    {
        return m_pRenderPass;
    }

    void* KVulkanRenderPass::GetRenderPassPtr()
    {
        return m_pRenderPass;
    }

    const KRenderPassDesc* KVulkanRenderPass::GetDesc() const
    {
        return &m_desc;
    }

    void KVulkanRenderPass::SetObjectName(const char* szName)
    {
        GetVulkanDevice()->SetObjectLabel(m_pRenderPass, szName);
    }

    ////////////////////////////////////////////////////////////////////
    KVulkanGraphicContext::KVulkanGraphicContext()
    {
        m_pWindowInfo         = nullptr;
        m_pSwapChain          = nullptr;
        m_uCurrentBufferIndex = 0;
        m_pDepthStencil       = nullptr;
        // m_pRenderPass_clear        = nullptr;
        // m_pRenderPass_load         = nullptr;
        m_DepthFormat         = VK_FORMAT_UNDEFINED;
        // m_pImageAcquiredSemaphoreA = nullptr;

        KGraphicDevice* pGraphicDevice = (KGraphicDevice*)GetGraphicDevice();
    }

    KVulkanGraphicContext::~KVulkanGraphicContext()
    {
        SAFE_DELETE(m_pWindowInfo);
    }

    BOOL KVulkanGraphicContext::Init(const gfx::KWindow* pWindowInfo)
    {
        PROF_CPU();
        BOOL bRet     = false;
        BOOL bRetCode = false;
        if (!m_pWindowInfo)
        {
            m_pWindowInfo = new gfx::KWindow();
        }
        *m_pWindowInfo = *pWindowInfo;

        bRetCode = _InitSwapchain();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _CreateSwapChainFences();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _CreateSwapChainSemaphoreA();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _CreateCommandBuffers();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _CreateDepthStencilRT();
        KGLOG_PROCESS_ERROR(bRetCode);

        // bRetCode = _CreateRenderPass();
        // KGLOG_PROCESS_ERROR(bRetCode);
        //
        // bRetCode = _CreateFrameBuffer();
        // KGLOG_PROCESS_ERROR(bRetCode);

        bRet = true;
    Exit0:
        return bRet;
    }

    void KVulkanGraphicContext::UnInit()
    {
        PROF_CPU();
        BOOL     bRetCode  = FALSE;
        VkDevice pvkDevice = GetVkDevice();
        if (pvkDevice)
        {
            vks::vkDeviceWaitIdle(pvkDevice);
        }

        m_pSwapChain->Cleanup();

        bRetCode = _DestroyCommandBuffers();
        KGLOG_PROCESS_ERROR(bRetCode);

        // bRetCode = _DestroyRenderPass();
        // KGLOG_PROCESS_ERROR(bRetCode);
        //
        // bRetCode = _DestroyFrameBuffer();
        // KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _DestroyDepthStencilRT();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _DestroySwapChainFences();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _DestroySwapChainSemaphoreA();
        KGLOG_PROCESS_ERROR(bRetCode);

        SAFE_DELETE(m_pSwapChain);
        m_pWindowInfo->DestroyWindowA();

    Exit0:
        return;
    }

    BOOL KVulkanGraphicContext::_InitSwapchain()
    {
        PROF_CPU();
        BOOL     bResult  = FALSE;
        BOOL     bRetCode = FALSE;
        VkResult hRetCode = VK_INCOMPLETE;

        {
            VkBool32    validDepthFormat = false;
            std::string androidProduct;

            VkPhysicalDevice pPhysicalDevice = GetVkPhysicalDevice();

            KGraphicDevice* pGraphicDevice = GetGraphicDevice();

            validDepthFormat = vks::tools::GetSupportedDepthFormat(pPhysicalDevice, &m_DepthFormat);
            KGLOG_PROCESS_ERROR(validDepthFormat);

            // #if defined(VK_USE_PLATFORM_ANDROID_KHR)
            //           // Get Android device name and manufacturer (to display along GPU name)
            //           androidProduct = "";
            //           char prop[PROP_VALUE_MAX + 1];
            //           int len = __system_property_get("ro.product.manufacturer", prop);
            //           if (len > 0) {
            //               androidProduct += std::string(prop) + " ";
            //           };
            //           len = __system_property_get("ro.product.model", prop);
            //           if (len > 0) {
            //               androidProduct += std::string(prop);
            //           };
            //           LOGD("androidProduct = %s", androidProduct.c_str());
            // #endif

            gfx::KVulkanSwapChain* pSwapChain = nullptr;
            bRetCode                    = pGraphicDevice->CreateSwapChain(&pSwapChain, m_pWindowInfo, 0);
            KGLOG_ASSERT_EXIT(bRetCode);
            m_pSwapChain = pSwapChain;
        }
        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanGraphicContext::_ResizeSwapChain()
    {
        BOOL bResult  = FALSE;
        BOOL bRetCode = FALSE;

        vks::KVulkanDevice*   pVulkanDevice  = GetVulkanDevice();
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        if (m_pWindowInfo->m_bWindowInvalidated)
        {
            pGraphicDevice->DestroySwapChain(m_pSwapChain);
            m_pSwapChain = nullptr;
            bRetCode     = _InitSwapchain();
            KGLOG_ASSERT_EXIT(bRetCode);
            m_pWindowInfo->m_bWindowInvalidated = false;
        }
        else
        {
            bRetCode = m_pSwapChain->Create(m_pWindowInfo->m_szWindowName, &m_pWindowInfo->m_uSwapChainWidth, &m_pWindowInfo->m_uSwapChainHeight, pVulkanDevice->m_Settings.m_bVsync);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KVulkanGraphicContext::_CreateCommandBuffers()
    {
        PROF_CPU();
        BOOL bRet     = FALSE;
        BOOL bRetCode = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = GetGraphicDevice();
        // Create one command buffer for each swap chain image and reuse for rendering

        uint32_t uMaxCommandCount = m_pSwapChain->m_nImageCount;
        if (DrvOption::uSwapchainMainCommandMutiple && DrvOption::uSwapchainMainCommandMutiple * m_pSwapChain->m_nImageCount < MAX_SWAP_CHAIN_COUNT)
        {
            uMaxCommandCount = DrvOption::uSwapchainMainCommandMutiple * m_pSwapChain->m_nImageCount;
        }

        // ASSERT(m_pSwapChain->m_nImageCount + DrvOption::uSwapchainMainCommandMutiple <= MAX_SWAP_CHAIN_COUNT);
        // uint32_t uMaxCommandCount = _MIN(MAX_SWAP_CHAIN_COUNT, m_pSwapChain->m_nImageCount + DrvOption::uSwapchainMainCommandMutiple);

        m_vecCommandBuffers.reserve(uMaxCommandCount);
        for (uint32_t i = 0; i < uMaxCommandCount; ++i)
        {
            KVulkanCommandBuffer* pCmdBuffer = nullptr;

            bRetCode = pGraphicDevice->CreateCommandBuffer(&pCmdBuffer, COMMAND_BUFFER_LEVEL_PRIMARY, gfx::FOR_GRPAHIC);
            KGLOG_PROCESS_ERROR(bRetCode);
            // pCmdBuffer->SetId(i % m_pSwapChain->m_nImageCount);
            pCmdBuffer->SetId(i);

            std::string strName = "MainCommandBuffer" + std::to_string(i) + "_";
            pCmdBuffer->SetObjectName(strName.c_str());
            m_vecCommandBuffers.push_back(pCmdBuffer);
        }

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicContext::_DestroyCommandBuffers()
    {
        PROF_CPU();
        BOOL bRet     = FALSE;
        BOOL bRetCode = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = GetGraphicDevice();

        for (uint32_t i = 0; i < m_vecCommandBuffers.size(); i++)
        {
            bRetCode = pGraphicDevice->DestroyCommandBuffer(m_vecCommandBuffers[i]);
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        m_vecCommandBuffers.clear();

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicContext::_CreateSwapChainFences()
    {
        PROF_CPU();
        BOOL bRet     = FALSE;
        BOOL bRetCode = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = GetGraphicDevice();

        m_vecSwapChainFences.resize(MAX_SWAP_CHAIN_COUNT);

        for (uint32_t i = 0; i < MAX_SWAP_CHAIN_COUNT; i++)
        {
            bRetCode = pGraphicDevice->CreateFence(&m_vecSwapChainFences[i]);
            KGLOG_PROCESS_ERROR(bRetCode);
            std::string str  = "SwapChainFence";
            str             += std::to_string(i);
            m_vecSwapChainFences[i]->SetObjectName(str.c_str());
        }

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicContext::_DestroySwapChainFences()
    {
        PROF_CPU();
        for (uint32_t i = 0; i < MAX_SWAP_CHAIN_COUNT; i++)
        {
            SAFE_RELEASE(m_vecSwapChainFences[i]);
        }

        return TRUE;
    }

    BOOL KVulkanGraphicContext::_CreateSwapChainSemaphoreA()
    {
        PROF_CPU();
        BOOL bRet     = FALSE;
        BOOL bRetCode = FALSE;

        gfx::KGraphicDevice* pGraphicDevice = GetGraphicDevice();

        m_vecRenderCompleteSemaphoreA.resize(MAX_SWAP_CHAIN_COUNT);
        m_vecImageAcquiredSemaphoreA.resize(MAX_SWAP_CHAIN_COUNT + 1u);

        for (uint32_t i = 0; i < m_vecRenderCompleteSemaphoreA.size(); i++)
        {
            bRetCode = pGraphicDevice->CreateSemaphoreA(&m_vecRenderCompleteSemaphoreA[i]);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        for (uint32_t i = 0; i < m_vecImageAcquiredSemaphoreA.size(); i++)
        {
            bRetCode = pGraphicDevice->CreateSemaphoreA(&m_vecImageAcquiredSemaphoreA[i]);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bRet = TRUE;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicContext::_DestroySwapChainSemaphoreA()
    {
        PROF_CPU();
        for (uint32_t i = 0; i < m_vecRenderCompleteSemaphoreA.size(); i++)
        {
            SAFE_RELEASE(m_vecRenderCompleteSemaphoreA[i]);
        }
        for (uint32_t i = 0; i < m_vecImageAcquiredSemaphoreA.size(); i++)
        {
            SAFE_RELEASE(m_vecImageAcquiredSemaphoreA[i]);
        }

        return TRUE;
    }

    BOOL KVulkanGraphicContext::ResizeWindow(gfx::KWindow* pWindow, BOOL bForce)
    {
        PROF_CPU();
        BOOL            bRet           = false;
        BOOL            bRetCode       = false;
        VkDevice        pDevice        = GetVkDevice();
        KGraphicDevice* pGraphicDevice = GetGraphicDevice();

        // KEngineOptions* pEngineOptions = NSEngine::GetEngineOptions();
        // KGLOG_PROCESS_ERROR(pWindow->m_uSwapChainWidth && pWindow->m_uSwapChainHeight);
        KGLOG_PROCESS_ERROR(pWindow->m_uWidth && pWindow->m_uHeight);
        // KG_PROCESS_SUCCESS(!bForce && m_pWindowInfo->m_uWidth == pWindow->m_uWidth && m_pWindowInfo->m_uHeight == pWindow->m_uHeight && m_pWindowInfo->m_window == pWindow->m_window);


        if (m_pWindowInfo->m_window != pWindow->m_window)
        {
            *m_pWindowInfo                      = *pWindow;
            m_pWindowInfo->m_bWindowInvalidated = true;
        }
        else
        {
            m_pWindowInfo->m_uWidth           = pWindow->m_uWidth;
            m_pWindowInfo->m_uHeight          = pWindow->m_uHeight;
            m_pWindowInfo->m_uSwapChainWidth  = pWindow->m_uSwapChainWidth;
            m_pWindowInfo->m_uSwapChainHeight = pWindow->m_uSwapChainHeight;
        }


        // Ensure all operations on the device have been finished before destroying resources
        vks::vkDeviceWaitIdle(pDevice);

        // Recreate swap chain
        bRetCode = _ResizeSwapChain();
        KGLOG_ASSERT_EXIT(bRetCode);

        bRetCode = _DestroyDepthStencilRT();
        KGLOG_PROCESS_ERROR(bRetCode);


        bRetCode = _CreateDepthStencilRT();
        KGLOG_ASSERT_EXIT(bRetCode);

        // bRetCode = _DestroyFrameBuffer();
        // KGLOG_PROCESS_ERROR(bRetCode);
        //
        // bRetCode = _CreateFrameBuffer();
        // KGLOG_PROCESS_ERROR(bRetCode);

        // Command buffers need to be recreated as they may store
        // references to the recreated frame buffer
        // bRetCode = _DestroyCommandBuffers();
        // KGLOG_PROCESS_ERROR(bRetCode);

        // bRetCode = _CreateCommandBuffers();
        // KGLOG_ASSERT_EXIT(bRetCode);


        bRetCode = _DestroySwapChainSemaphoreA();
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = _CreateSwapChainSemaphoreA();
        KGLOG_ASSERT_EXIT(bRetCode);

        vks::vkDeviceWaitIdle(pDevice);


        // Exit1:
        bRet = true;
    Exit0:
        return bRet;
    }

    // uint32_t KVulkanGraphicContext::GetRenderViewWidth()
    //{
    //	return m_pWindowInfo->m_uSwapChainWidth;
    // }

    // uint32_t KVulkanGraphicContext::GetRenderViewHeight()
    //{
    //	return m_pWindowInfo->m_uSwapChainHeight;
    // }

    uint32_t KVulkanGraphicContext::GetRenderTargetWith()
    {
        return m_pWindowInfo->m_uSwapChainWidth;
    }

    uint32_t KVulkanGraphicContext::GetRenderTargetHeight()
    {
        return m_pWindowInfo->m_uSwapChainHeight;
    }

    gfx::enumGraphicContext KVulkanGraphicContext::GetGraphicContextId()
    {
        return m_pWindowInfo->m_uId;
    }

    BOOL KVulkanGraphicContext::_CreateDepthStencilRT()
    {
        PROF_CPU();
        BOOL bRet     = FALSE;
        BOOL bRetCode = FALSE;

        KGraphicDevice* pGraphicDevice = GetGraphicDevice();
        KRenderTargetDesc rendertargetDesc = {};

        rendertargetDesc.uWidth  = m_pWindowInfo->m_uSwapChainWidth;
        rendertargetDesc.uHeight = m_pWindowInfo->m_uSwapChainHeight;
        rendertargetDesc.uDepth  = 1;
        // rendertargetDesc.eFormat = TEX_FORMAT_D24_UNORM_S8_UINT;
        rendertargetDesc.eFormat = gfx::GetDefaultDepthStencilFormat();

        rendertargetDesc.uMipLevels    = 1;
        rendertargetDesc.uArraySize    = 1;
        rendertargetDesc.eSampleCount  = SAMPLE_COUNT_1_BIT;

        sprintf(rendertargetDesc.m_szRTName, "SwapChainDepthStencilRT");

        bRetCode = pGraphicDevice->CreateRenderTarget2D(&m_pDepthStencil, &rendertargetDesc, TRUE, nullptr);
        KGLOG_ASSERT_EXIT(bRetCode);
        m_pDepthStencil->SetObjectName(rendertargetDesc.m_szRTName);
        KGLogPrintf(KGLOG_DEBUG, "SwapChain Depth RenderTarget:%s, image:%p, height:%d", rendertargetDesc.m_szRTName, m_pDepthStencil->GetNativeImageHandle(), rendertargetDesc.uHeight);

        bRet = true;
    Exit0:
        return bRet;
    }

    BOOL KVulkanGraphicContext::_DestroyDepthStencilRT()
    {
        PROF_CPU();
        BOOL bRet     = FALSE;
        BOOL bRetCode = FALSE;

        SAFE_RELEASE(m_pDepthStencil);

        bRet = TRUE;
        return bRet;
    }

    KWindow* KVulkanGraphicContext::GetWindowInfo()
    {
        return m_pWindowInfo;
    }

    gfx::KVulkanSwapChain* KVulkanGraphicContext::GetSwapChains()
    {
        return m_pSwapChain;
    }

    uint32_t KVulkanGraphicContext::_GetSwapChainImageCount()
    {
        ASSERT(m_pSwapChain);

        return m_pSwapChain->m_nImageCount;
    }

    enumTextureFormat KVulkanGraphicContext::GetSwapChainColorFormat()
    {
        return GetTextureEnumFromFormat(m_pSwapChain->m_colorFormat);
    }

    enumTextureFormat KVulkanGraphicContext::GetSwapChainDepthFormat()
    {
        return GetTextureEnumFromFormat(m_DepthFormat);
    }

    std::vector<KVulkanCommandBuffer*>& KVulkanGraphicContext::GetSwapChainCommandBuffers()
    {
        return m_vecCommandBuffers;
    }

    KVulkanCommandBuffer* KVulkanGraphicContext::GetSwapChainCommandBuffer(uint32_t id)
    {
        ASSERT(id < (uint32_t)m_vecCommandBuffers.size());
        return m_vecCommandBuffers[id];
    }

    gfx::KVulkanFence* KVulkanGraphicContext::GetSwapChainFence(uint32_t uFenceIndex)
    {
        ASSERT(uFenceIndex < m_vecSwapChainFences.size());
        return m_vecSwapChainFences[uFenceIndex];
    }

    std::vector<KRenderTarget*>& KVulkanGraphicContext::GetSwapChainRenderTarget()
    {
        ASSERT(m_pSwapChain);
        return m_pSwapChain->m_SwapChainRTs;
    }

    KRenderTarget* KVulkanGraphicContext::GetSwapChainDepthStencilRT()
    {
        return m_pDepthStencil;
    }

    uint32_t KVulkanGraphicContext::GetSwapChainImageIndex()
    {
        return m_uCurrentBufferIndex;
    }

    void KVulkanGraphicContext::ActiveSwapChainImage(uint32_t id)
    {
        m_uCurrentBufferIndex = (id % m_pSwapChain->m_nImageCount);
    }

    uint32_t KVulkanGraphicContext::GetSwapChainImageCount()
    {
        return _GetSwapChainImageCount();
    }

    KRenderTarget* KVulkanGraphicContext::GetCurSwapChainRenderTarget()
    {
        std::vector<KRenderTarget*>& vecSwapChainRT = GetSwapChainRenderTarget();
        return vecSwapChainRT.at(GetSwapChainImageIndex());
    }

    void KVulkanGraphicContext::GetRenderCompleteSemaphoreA(gfx::KVulkanSemaphore** pSemaphoreA, uint32_t uImageSemaphoreId)
    {
        ASSERT(uImageSemaphoreId < m_vecRenderCompleteSemaphoreA.size());
        *pSemaphoreA = m_vecRenderCompleteSemaphoreA[uImageSemaphoreId];
    }

    void KVulkanGraphicContext::GetImageAcquiredSemaphoreA(gfx::KVulkanSemaphore** pSemaphoreA, uint32_t uImageSemaphoreId)
    {
        ASSERT(uImageSemaphoreId < m_vecImageAcquiredSemaphoreA.size());
        *pSemaphoreA = m_vecImageAcquiredSemaphoreA[uImageSemaphoreId];
    }

    //////////////////////////////////////////////////////////////////////////
    KVulkanQueryHeap::KVulkanQueryHeap()
    {
    }

    KVulkanQueryHeap::~KVulkanQueryHeap()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    KVulkanSampler::KVulkanSampler()
    {
        m_pVkSampler = nullptr;
        // m_VkSamplerView.sampler = 0;
        // m_VkSamplerView.imageView = 0;
        // m_VkSamplerView.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    KVulkanSampler::~KVulkanSampler()
    {
        ASSERT(!m_pVkSampler);
    }

    const KSamplerState& KVulkanSampler::GetSamplerState()
    {
        return m_samplerSate;
    }

    uint64_t KVulkanSampler::GetCode()
    {
        return (uint64_t)m_pVkSampler;
    }

    BOOL KVulkanSampler::Create(const KSamplerState* pSamplerState)
    {
        BOOL                bResult       = FALSE;
        BOOL                bRetCode      = FALSE;
        VkResult            vkRetCode     = VK_INCOMPLETE;
        gfx::IKGFX_Sampler* pRetSampler   = nullptr;
        VkDevice            pVkDevice     = nullptr;
        vks::KVulkanDevice* pVulkanDevice = nullptr;
        m_samplerSate                     = *pSamplerState;

        VkSamplerCreateInfo sSamplerCreateInfo = vks::initializers::SamplerCreateInfo();

        KG_PROCESS_SUCCESS(m_pVkSampler);
        KGLOG_ASSERT_EXIT(pSamplerState);

        pVkDevice = GetVkDevice();
        KGLOG_ASSERT_EXIT(pVkDevice);

        pVulkanDevice = GetVulkanDevice();
        KGLOG_ASSERT_EXIT(pVulkanDevice);
        // Create a texture sampler
        // In Vulkan textures are accessed by samplers
        // This separates all the sampling information from the texture data. This means you could have multiple sampler objects for the same texture with different settings
        // Note: Similar to the samplers available with OpenGL 3.3

        sSamplerCreateInfo.pNext      = nullptr;
        sSamplerCreateInfo.magFilter  = gfx::GetSamplerFilter(pSamplerState->enuMagFilter);
        sSamplerCreateInfo.minFilter  = gfx::GetSamplerFilter(pSamplerState->enuMinFilter);
        sSamplerCreateInfo.mipmapMode = gfx::GetSamplerMipmapMode(pSamplerState->enuMipmapMode);

        sSamplerCreateInfo.addressModeU = gfx::GetSamplerAddressMode(pSamplerState->enuAddressModeU);
        sSamplerCreateInfo.addressModeV = gfx::GetSamplerAddressMode(pSamplerState->enuAddressModeV);
        sSamplerCreateInfo.addressModeW = gfx::GetSamplerAddressMode(pSamplerState->enuAddressModeW);
        sSamplerCreateInfo.mipLodBias   = pSamplerState->finialMipBias;

        sSamplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
        sSamplerCreateInfo.minLod    = pSamplerState->fToMinLod;
        // Set max level-of-detail to mip level count of the texture
        sSamplerCreateInfo.maxLod    = pSamplerState->fToMaxLod; // default to VK_LOD_CLAMP_NONE
        // Enable anisotropic filtering
        // This feature is optional, so we must check if it's supported on the device
        if (pSamplerState->fMaxAnisotropy == 0.0f)
        {
            sSamplerCreateInfo.maxAnisotropy    = 1.0f;
            sSamplerCreateInfo.anisotropyEnable = VK_FALSE;
        }
        else if (pVulkanDevice->m_Features.samplerAnisotropy)
        {
            // Use max. level of anisotropy for this example
            sSamplerCreateInfo.maxAnisotropy    = std::min(std::max(pSamplerState->fMaxAnisotropy, 1.0f), pVulkanDevice->m_Properties.limits.maxSamplerAnisotropy);
            sSamplerCreateInfo.anisotropyEnable = VK_TRUE;
        }
        sSamplerCreateInfo.borderColor   = gfx::GetSamplerBorderColor(pSamplerState->enuBorderColor);
        sSamplerCreateInfo.compareEnable = pSamplerState->bCompareEnable;
        sSamplerCreateInfo.compareOp     = gfx::GetSamplerCompareFunc(pSamplerState->enuCompareFunc);
        vkRetCode                        = vks::vkCreateSampler(pVkDevice, &sSamplerCreateInfo, nullptr, &m_pVkSampler);
        KGLOG_COM_PROCESS_ERROR(vkRetCode);
        KGLOG_ASSERT_EXIT(m_pVkSampler);

    Exit1:
        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL KVulkanSampler::Destroy()
    {
        VkDevice pVkDevice = GetVkDevice();
        ASSERT(pVkDevice);
        if (m_pVkSampler)
        {
            vks::vkDestroySampler(pVkDevice, m_pVkSampler, nullptr);
            m_pVkSampler = nullptr;
        }
        return TRUE;
    }

    VkSampler KVulkanSampler::GetVKSampler()
    {
        if (m_samplerSate.bEnableMipmap && m_samplerSate.enuMipmapMode == SAMPLER_MIPMAP_MODE_LINEAR)
        {
            float fLodBias = NSEngine::GetEngineOptions()->fMipLodBias;
            float fMinBias = std::min(m_samplerSate.fMipLodBias, fLodBias);

            if (!NSKMath::IsNearlyEqual(m_samplerSate.finialMipBias, fMinBias))
            {
                KGraphicDevice* pDevice     = GetGraphicDevice();
                m_pVkSampler                = nullptr;
                m_samplerSate.finialMipBias = fMinBias;
                Create(&m_samplerSate);
            }
        }

        return m_pVkSampler;
    }

    uintptr_t KVulkanSampler::GetNativeHandle()
    {
        return (uintptr_t)GetVKSampler();
    }

    VkSampler* KVulkanSampler::GetVkSamplerPtr()
    {
        if (m_samplerSate.bEnableMipmap && m_samplerSate.enuMipmapMode == SAMPLER_MIPMAP_MODE_LINEAR)
        {
            float fLodBias = NSEngine::GetEngineOptions()->fMipLodBias;
            float fMinBias = std::min(m_samplerSate.fMipLodBias, fLodBias);

            if (!NSKMath::IsNearlyEqual(m_samplerSate.finialMipBias, fMinBias))
            {
                KGraphicDevice* pDevice     = GetGraphicDevice();
                m_pVkSampler                = nullptr;
                m_samplerSate.finialMipBias = fMinBias;
                Create(&m_samplerSate);
            }
        }

        return &m_pVkSampler;
    }
    IKGFX_SamplerBindlessView* KVulkanSampler::GetBindlessView()
    {
        if (!m_pBindlessView)
        {
            m_pBindlessView = new KVulkanSamplerBindlessView(this);
        }
        return m_pBindlessView;
    }

    ////////////////////////////////////////////////////////////////////

    bool KVulkanSignalFence::IsSubmitted() const
    {
        return m_bSubmitted;
    }

    void KVulkanSignalFence::Clear()
    {
        SAFE_RELEASE(m_pFence);
        m_bSubmitted = false;
    }

    bool KVulkanSignalFence::Query()
    {
        if (m_pFence)
        {
            m_pFence->Query();
            bool bSignaled = m_uSignalFenceCounter < m_pFence->GetSignalFenceCounter();
            if (bSignaled)
            {
                m_bSubmitted = false;
            }
            return bSignaled;
        }
        else
        {
            return true;
        }
    }

    BOOL KVulkanSignalFence::Submit(KVulkanFence* pFence)
    {
        BOOL bResult = false;

        KGLOG_ASSERT_EXIT(pFence);
        if (m_pFence != pFence)
        {
            Clear();

            m_pFence = pFence;
            m_pFence->AddRef();
        }
        m_bSubmitted = true;
        m_uSignalFenceCounter = m_pFence->GetSignalFenceCounter();

        bResult = true;
    Exit0:
        return bResult;
    }

    bool KVulkanSignalFence::GetCurrentValue(uint64_t* outValue)
    {
        throw std::runtime_error("not implemented");
        return {};
    }

    void* KVulkanSignalFence::GetNativeHandle()
    {
        throw std::runtime_error("not implemented");
        return {};
    }

    ////////////////////////////////////////////////////////////////////
    KGFX_SwapchainVK::KGFX_SwapchainVK()
    {
        m_pWindow = new KWindow;
    }

    KGFX_SwapchainVK::~KGFX_SwapchainVK()
    {
        SAFE_DELETE(m_pWindow);
    }

    BOOL KGFX_SwapchainVK::Init(const KWindow* pWindowInfo)
    {
        BOOL            bRetCode = false;
        BOOL            bRet     = false;
        KGraphicDevice* pDevice  = GetGraphicDevice();
        memcpy(m_pWindow, pWindowInfo, sizeof(KWindow));
        if (!m_pGraphicContext && pDevice)
        {
            bRetCode = pDevice->CreateGraphicContext(&m_pGraphicContext, pWindowInfo);
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        bRet = true;

    Exit0:
        return true;
    }

    uint32_t KGFX_SwapchainVK::GetSwapChainRTCount()
    {
        return (uint32_t)m_pGraphicContext->GetSwapChainRenderTarget().size();
    }

	gfx::KRenderTarget* KGFX_SwapchainVK::GetCurerntSwapChainRT()
	{
		return m_pGraphicContext->GetCurSwapChainRenderTarget();
	}

    gfx::KRenderTarget* KGFX_SwapchainVK::GetDepthStencilRT()
    {
        return m_pGraphicContext->GetSwapChainDepthStencilRT();
    }

    enumTextureFormat KGFX_SwapchainVK::GetSwapChainColorFormat()
	{
		auto pSwapchain = m_pGraphicContext->GetSwapChains();
		if (pSwapchain)
		{
			return pSwapchain->GetSurfaceFormat();
		}
		return enumTextureFormat::TEX_FORMAT_NONE;
	}

	enumTextureFormat KGFX_SwapchainVK::GetSwapChainDepthFormat()
	{
		KRenderTarget* pDSRT = m_pGraphicContext->GetSwapChainDepthStencilRT();
		if (pDSRT)
		{
			return pDSRT->GetDesc().eFormat;
		}
		return enumTextureFormat::TEX_FORMAT_NONE;
	}

    BOOL KGFX_SwapchainVK::IsAcquiredImage()
    {
        return m_bAcquiredImage;
    }

    BOOL KGFX_SwapchainVK::IsBeginRender()
    {
        return m_bBeginRender;
    }

    BOOL KGFX_SwapchainVK::UnInit()
    {
        KGraphicDevice* pDevice = GetGraphicDevice();
        if (m_pGraphicContext && pDevice)
        {
            pDevice->DestroyGraphicContext(m_pGraphicContext);
        }
        return true;
    }

    BOOL KGFX_SwapchainVK::BeginRender()
    {
        uint32_t        uImageIndex          = 0;
        BOOL            bRetCode             = false;
        BOOL            bResult              = false;
        KGraphicDevice* pGraphicDevice       = GetGraphicDevice();
        m_bAcquiredImage                     = false;
        m_bBeginRender                       = false;
        KVulkanSemaphore* pImageAcquiredSemaphoreA = nullptr;

        m_uCurrentImageSemaphoreId++;
        if (m_uCurrentImageSemaphoreId > MAX_SWAP_CHAIN_COUNT)
        {
            m_uCurrentImageSemaphoreId = 0;
        }

        m_pGraphicContext->GetImageAcquiredSemaphoreA(&pImageAcquiredSemaphoreA, m_uCurrentImageSemaphoreId);
        KGLOG_PROCESS_ERROR(pImageAcquiredSemaphoreA);

        m_pCurrentImageAcquiredSemaphoreA = pImageAcquiredSemaphoreA;

        if (m_bNeedOnResize)
        {
            OnResize();
            m_bNeedOnResize       = false;
            m_bAcquiredImage      = false;
            m_bOnResizeProcessing = true;
            m_pGraphicContext->ActiveSwapChainImage(0);

            //{
            //	const auto& cmds = m_pGraphicContext->GetSwapChainCommandBuffers();
            //	for (auto it : cmds)
            //	{
            //		gfx::KVulkanCommandBuffer* pCmd = it;
            //		pCmd->Reset(true);
            //	}
            //}
        }
        else if (!m_bAcquiredImage && pGraphicDevice->AcquireNextImage(m_pGraphicContext->GetSwapChains(), pImageAcquiredSemaphoreA, NULL, &uImageIndex))
        {
            m_bAcquiredImage      = true;
            m_bOnResizeProcessing = false;
            m_pGraphicContext->ActiveSwapChainImage(uImageIndex);
        }
        else
        {
            // AcquireNextImage失败了，那么下一帧尝试重建swapchain
            m_bNeedOnResize        = true;
            m_bNeedSwapChainResize = true;
            m_bOnResizeProcessing  = true;
            goto Exit0;
        }

        bRetCode = BeginSwapchainCommand();
        KG_PROCESS_ERROR(bRetCode);

        m_bBeginRender = true;

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_SwapchainVK::Present()
    {
        BOOL            bResult        = false;
        BOOL            bRetCode       = false;
        KGraphicDevice* pGraphicDevice = GetGraphicDevice();
        KG_PROCESS_ERROR(m_bAcquiredImage);

        if (m_bBeginRender)
        {
            auto        pRenderContext = static_cast<KVulkanRenderContext*>(gfx::GetRenderContext());
            KVulkanSemaphore* pSemaphore     = nullptr;
            auto        pCommandBuffer = pRenderContext->GetVulkanCommandBuffer();

            m_pGraphicContext->GetRenderCompleteSemaphoreA(&pSemaphore, m_pGraphicContext->GetSwapChainImageIndex());
            pCommandBuffer->AddWaitSemaphores(&m_pCurrentImageAcquiredSemaphoreA, 1);

            // 天玑8100不Wait会卡死，暂时无法区分联发科，先通过gpu判断
            bool bWait = false; // DrvOption::bIsMaliGPU ? true : false;
            pRenderContext->SubmitCommandBuffer(bWait, pSemaphore);

            SwapChainPresent(pSemaphore);
        }
        m_bBeginRender = FALSE;
        bResult        = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_SwapchainVK::BeginSwapchainCommand()
    {
        BOOL bResult                         = false;
        BOOL bRetCode                        = false;
        m_bMainCommandBegan                  = false;
        gfx::IKGFX_RenderContext* pRenderContext = gfx::GetRenderContext();
        ASSERT(pRenderContext);
        KG_PROCESS_ERROR(m_bAcquiredImage);

        bRetCode = pRenderContext->BeginCommandBuffer();
        KGLOG_ASSERT_EXIT(bRetCode);

        m_bMainCommandBegan = true;

        bResult = true;
    Exit0:
        return bResult;
    }


    BOOL KGFX_SwapchainVK::EndSwapchainCommand()
    {
        BOOL                  bResult        = false;
        KVulkanRenderContext* pRenderContext = (KVulkanRenderContext*)gfx::GetRenderContext();
        KVulkanCommandBuffer* pCommandBuffer = nullptr;
        KGLOG_ASSERT_EXIT(m_bMainCommandBegan);


        pCommandBuffer = pRenderContext->GetVulkanCommandBuffer();
        pCommandBuffer->AddWaitSemaphores(&m_pCurrentImageAcquiredSemaphoreA, 1);

        pRenderContext->SubmitCommandBuffer();

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_SwapchainVK::SwapChainPresent(KVulkanSemaphore* pWaitSemaphore)
    {
        BOOL            bResult       = false;
        KEngineOptions* pEngineOption = NSEngine::GetEngineOptions();

        KGraphicDevice* pGraphicDevice = GetGraphicDevice();
        KVulkanGfxQueue*      pGraphicQueue  = pGraphicDevice->GetGraphicQueue();

        if (m_bAcquiredImage)
        {
            // pGraphicDevice->SubmitCommandBuffer(pGraphicQueue, m_pCurrentGPUDrivenCommandBuffer, FALSE, nullptr);

            uint32_t uCurrentSwapChainId = m_pGraphicContext->GetSwapChainImageIndex();

            KVulkanSwapChain* pChain = m_pGraphicContext->GetSwapChains();

            if (pEngineOption->bRenderUIDebug)
            {
                // printf("QueueSubmit\r\n");
                auto t1 = std::chrono::steady_clock::now();
                pGraphicDevice->QueuePresent(pGraphicQueue, pChain, uCurrentSwapChainId, 1, &pWaitSemaphore);
                auto t2          = std::chrono::steady_clock::now();
                m_fPresentTimeMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
            }
            else
            {
                pGraphicDevice->QueuePresent(pGraphicQueue, pChain, uCurrentSwapChainId, 1, &pWaitSemaphore);
            }

            m_bAcquiredImage = false;
        }
        else
        {
            int x = 0;
        }

        bResult = true;
        return bResult;
    }

    BOOL KGFX_SwapchainVK::OnResize()
    {
        BOOL bResult  = false;
        BOOL bRetCode = FALSE;

        KGraphicDevice* pDevice = GetGraphicDevice();
        pDevice->QueueWaitIdle(FOR_GRPAHIC);

        bRetCode = m_pGraphicContext->ResizeWindow(m_pWindow, false);
        KGLOG_ASSERT_EXIT(bRetCode);


        {
            const auto& cmds = m_pGraphicContext->GetSwapChainCommandBuffers();
            for (auto it : cmds)
            {
                KVulkanCommandBuffer* pCmd = it;
                pCmd->Reset(true);
            }
        }
        pDevice->QueueWaitIdle(FOR_GRPAHIC);
        bResult = true;
    Exit0:
        return bResult;
    }

    KWindow* KGFX_SwapchainVK::GetWindow()
    {
        return m_pWindow;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    KVulkanSamplerBindlessView::KVulkanSamplerBindlessView(KVulkanSampler* resouce)
    {
        ASSERT(resouce);
        ASSERT(IS_BINDLESS_ENABLED);
        m_pResouce = resouce;
        KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
        KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
        m_uBindlessHandle = pBindlessMgr->RequestSamplerBindlessSolt();
    }

    KVulkanSamplerBindlessView::~KVulkanSamplerBindlessView()
    {
        if (m_uBindlessHandle != UINT32_MAX)
        {
            ASSERT(IS_BINDLESS_ENABLED);
            //m_uBindlessHandle = VUlkanBindlessManager::
            KVulkanGraphicDevice* pGraphicDevice = (KVulkanGraphicDevice*)GetGraphicDevice();
            KVulkanBindlessManager* pBindlessMgr = pGraphicDevice->GetBindlessManager();
            pBindlessMgr->ReleaseSamplerBindlessSlot(m_uBindlessHandle);
            m_uBindlessHandle = UINT32_MAX;
        }
    }

    uint32_t KVulkanSamplerBindlessView::GetBindlessHandle()
    {
        return m_uBindlessHandle;
    }

    const KSamplerState& KVulkanSamplerBindlessView::GetSamplerState()
    {
        return m_pResouce->GetSamplerState();
    }

    IKGFX_Sampler* KVulkanSamplerBindlessView::GetResource()
    {
        return m_pResouce;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
} // namespace gfx

#include "KVulkanSwapChain.h"
#include "KVulkanTools.h"
#include "GFXVulkan.h"
#include "KEnginePub/Private/vulkan/KMetalView.h"
#include "KVulkanDevice.h"
#include "KVulkanInitializers.h"
#include "Engine/KGLog.h"
#include "KEngine/Public/KEngineCore.h"
#include "KEngine/Private/Render/Forward/KRenderForward.h"
#include "KEnginePub/Public/KEsDrv.h"
#include "KGFX_GraphicDeviceVK.h"
#include "KGraphicDevice.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Public/KProfileTools.h"

namespace gfx
{
    /** @brief Creates the platform specific surface abstraction of the native platform window used for presentation */
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    void KVulkanSwapChain::InitSurface(void* platformHandle, void* platformWindow)
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    void KVulkanSwapChain::InitSurface(ANativeWindow* window)
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    void KVulkanSwapChain::InitSurface(wl_display* display, wl_surface* window)
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    void KVulkanSwapChain::InitSurface(xcb_connection_t* connection, xcb_window_t window)
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
    void KVulkanSwapChain::InitSurface(void* view)
#elif defined(VK_USE_PLATFORM_OHOS)
    void KVulkanSwapChain::InitSurface(OHNativeWindow* window)
#elif defined(_DIRECT2DISPLAY)
    void KVulkanSwapChain::InitSurface(uint32_t width, uint32_t height)
#endif
    {
        VkResult err = VK_SUCCESS;

        // Create the os-specific surface
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.hinstance                   = (HINSTANCE)platformHandle;
        surfaceCreateInfo.hwnd                        = (HWND)platformWindow;
        err                                           = vks::vkCreateWin32SurfaceKHR(m_pInstance, &surfaceCreateInfo, nullptr, &m_pSurface);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType                         = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.window                        = window;
        err                                             = vks::vkCreateAndroidSurfaceKHR(m_pInstance, &surfaceCreateInfo, NULL, &m_pSurface);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
        VkIOSSurfaceCreateInfoMVK surfaceCreateInfo = {};
        surfaceCreateInfo.sType                     = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
        surfaceCreateInfo.pNext                     = NULL;
        surfaceCreateInfo.flags                     = 0;
        surfaceCreateInfo.pView                     = view;
        err                                         = vkCreateIOSSurfaceMVK(m_pInstance, &surfaceCreateInfo, nullptr, &m_pSurface);
#elif defined(VK_USE_PLATFORM_OHOS)
        VkSurfaceCreateInfoOHOS surfaceCreateInfo = {};
        surfaceCreateInfo.sType                   = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
        surfaceCreateInfo.window                  = window;
        err                                       = vks::vkCreateSurfaceOHOS(m_pInstance, &surfaceCreateInfo, NULL, &m_pSurface);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
        MakeViewMetalCompatible(view);
        VkMacOSSurfaceCreateInfoMVK surfaceCreateInfo = {};
        surfaceCreateInfo.sType                       = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
        surfaceCreateInfo.pNext                       = NULL;
        surfaceCreateInfo.flags                       = 0;
        surfaceCreateInfo.pView                       = view;
        err                                           = vkCreateMacOSSurfaceMVK(m_pInstance, &surfaceCreateInfo, NULL, &m_pSurface);
#elif defined(_DIRECT2DISPLAY)
        createDirect2DisplaySurface(width, height);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        VkWaylandSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType                         = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.display                       = display;
        surfaceCreateInfo.surface                       = window;
        err                                             = vks::vkCreateWaylandSurfaceKHR(m_pInstance, &surfaceCreateInfo, nullptr, &m_pSurface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType                     = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.connection                = connection;
        surfaceCreateInfo.window                    = window;
        err                                         = vks::vkCreateXcbSurfaceKHR(m_pInstance, &surfaceCreateInfo, nullptr, &m_pSurface);
#endif

        if (err != VK_SUCCESS)
        {
            vks::tools::ExitFatal("Could not create surface!", err);
        }

        // Get available queue family properties
        uint32_t queueCount;
        vks::vkGetPhysicalDeviceQueueFamilyProperties(m_pPhysicalDevice, &queueCount, NULL);
        assert(queueCount >= 1);

        std::vector<VkQueueFamilyProperties> queueProps(queueCount);
        vks::vkGetPhysicalDeviceQueueFamilyProperties(m_pPhysicalDevice, &queueCount, queueProps.data());

        // Iterate over each queue to learn whether it supports presenting:
        // Find a queue with present support
        // Will be used to present the swap chain images to the windowing system
        std::vector<VkBool32> supportsPresent(queueCount);
        for (uint32_t i = 0; i < queueCount; i++)
        {
            vks::vkGetPhysicalDeviceSurfaceSupportKHR(m_pPhysicalDevice, i, m_pSurface, &supportsPresent[i]);
        }

        // Search for a graphics and a present queue in the array of queue
        // families, try to find one that supports both
        uint32_t graphicsQueueNodeIndex = UINT32_MAX;
        uint32_t presentQueueNodeIndex  = UINT32_MAX;
        for (uint32_t i = 0; i < queueCount; i++)
        {
            if ((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                if (graphicsQueueNodeIndex == UINT32_MAX)
                {
                    graphicsQueueNodeIndex = i;
                }

                if (supportsPresent[i] == VK_TRUE)
                {
                    graphicsQueueNodeIndex = i;
                    presentQueueNodeIndex  = i;
                    break;
                }
            }
        }
        if (presentQueueNodeIndex == UINT32_MAX)
        {
            // If there's no queue that supports both present and graphics
            // try to find a separate present queue
            for (uint32_t i = 0; i < queueCount; ++i)
            {
                if (supportsPresent[i] == VK_TRUE)
                {
                    presentQueueNodeIndex = i;
                    break;
                }
            }
        }

        // Exit if either a graphics or a presenting queue hasn't been found
        if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX)
        {
            vks::tools::ExitFatal("Could not find a graphics and/or presenting queue!", -1);
        }

        // todo : Add support for separate graphics and presenting queue
        if (graphicsQueueNodeIndex != presentQueueNodeIndex)
        {
            vks::tools::ExitFatal("Separate graphics and presenting queues are not supported yet!", -1);
        }

        queueNodeIndex = graphicsQueueNodeIndex;

        // Get list of supported surface formats
        uint32_t formatCount = 0;
        VK_CHECK_RESULT(vks::vkGetPhysicalDeviceSurfaceFormatsKHR(m_pPhysicalDevice, m_pSurface, &formatCount, NULL));
        assert(formatCount > 0);

        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        VK_CHECK_RESULT(vks::vkGetPhysicalDeviceSurfaceFormatsKHR(m_pPhysicalDevice, m_pSurface, &formatCount, surfaceFormats.data()));

        // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
        // there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
        if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED))
        {
            m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
            m_ColorSpace  = surfaceFormats[0].colorSpace;
        }
        else
        {
            // iterate over the list of available surface format and
            // check for the presence of VK_FORMAT_B8G8R8A8_UNORM
            bool found_B8G8R8A8_UNORM = false;
            for (auto&& surfaceFormat : surfaceFormats)
            {
                if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
                {
                    m_colorFormat        = surfaceFormat.format;
                    m_ColorSpace         = surfaceFormat.colorSpace;
                    found_B8G8R8A8_UNORM = true;
                    break;
                }
            }

            // in case VK_FORMAT_B8G8R8A8_UNORM is not available
            // select the first available color format
            if (!found_B8G8R8A8_UNORM)
            {
                m_colorFormat = surfaceFormats[0].format;
                m_ColorSpace  = surfaceFormats[0].colorSpace;
            }
        }
    }

    void KVulkanSwapChain::Connect(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
    {
        this->m_pInstance       = instance;
        this->m_pPhysicalDevice = physicalDevice;
        this->m_pDevice         = device;
        // GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceSupportKHR);
        // GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
        // GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceFormatsKHR);
        // GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfacePresentModesKHR);
        // GET_DEVICE_PROC_ADDR(device, CreateSwapchainKHR);
        // GET_DEVICE_PROC_ADDR(device, DestroySwapchainKHR);
        // GET_DEVICE_PROC_ADDR(device, GetSwapchainImagesKHR);
        // GET_DEVICE_PROC_ADDR(device, AcquireNextImageKHR);
        // GET_DEVICE_PROC_ADDR(device, QueuePresentKHR);
    }

    gfx::enumTextureFormat KVulkanSwapChain::GetSurfaceFormat()
    {
        if (m_colorFormat == VK_FORMAT_B8G8R8A8_UNORM)
        {
            return TEX_FORMAT_B8G8R8A8_UNORM;
        }
        else
        {
            return TEX_FORMAT_R8G8B8A8_UNORM;
        }
    }

    BOOL KVulkanSwapChain::Create(const char* szName, uint32_t* width, uint32_t* height, BOOL vsync)
    {
#ifndef _WIN32
        vsync = true;
#endif
        BOOL                          bResult  = false;
        VkResult                      hRetCode = VK_INCOMPLETE;
        uint32_t                      presentModeCount;
        VkSurfaceCapabilitiesKHR      surfCaps;
        VkExtent2D                    swapchainExtent = {};
        VkSwapchainCreateInfoKHR      swapchainCI     = {};
        VkCompositeAlphaFlagBitsKHR   compositeAlpha;
        VkFormatProperties            formatProps;
        uint32_t                      desiredNumberOfSwapchainImages;
        VkPresentModeKHR              swapchainPresentMode;
        VkSurfaceTransformFlagsKHR    preTransform;
        std::vector<VkPresentModeKHR> presentModes;

        // 创建swapChain的rt
        KRenderTargetDesc rendertargetDesc = {};

        // Simply select the first composite alpha format available
        std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };

        VkSwapchainKHR oldSwapchain = m_pSwapChain;

        // Get physical device surface properties and formats

        hRetCode = vks::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_pPhysicalDevice, m_pSurface, &surfCaps);
        KGLOG_COM_PROCESS_ERROR(hRetCode);

        // Get available present modes

        hRetCode = vks::vkGetPhysicalDeviceSurfacePresentModesKHR(m_pPhysicalDevice, m_pSurface, &presentModeCount, NULL);
        KGLOG_COM_PROCESS_ERROR(hRetCode);
        KGLOG_PROCESS_ERROR(presentModeCount);

        presentModes.resize(presentModeCount);
        hRetCode = vks::vkGetPhysicalDeviceSurfacePresentModesKHR(m_pPhysicalDevice, m_pSurface, &presentModeCount, presentModes.data());
        KGLOG_COM_PROCESS_ERROR(hRetCode);

        // If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
        if (surfCaps.currentExtent.width == (uint32_t)-1)
        {
            // If the surface size is undefined, the size is set to
            // the size of the images requested.
            swapchainExtent.width  = *width;
            swapchainExtent.height = *height;

            // junwen: 这个地方可能存在limit问题
            // Limit Width
            if (swapchainExtent.width < surfCaps.minImageExtent.width)
            {
                swapchainExtent.width = surfCaps.minImageExtent.width;
            }
            else if (swapchainExtent.width > surfCaps.maxImageExtent.width)
            {
                swapchainExtent.width = surfCaps.maxImageExtent.width;
            }

            // Limit Height
            if (swapchainExtent.height < surfCaps.minImageExtent.height)
            {
                swapchainExtent.height = surfCaps.minImageExtent.height;
            }
            else if (swapchainExtent.height > surfCaps.maxImageExtent.height)
            {
                swapchainExtent.height = surfCaps.maxImageExtent.height;
            }

            // 修正后同步width和height
            if (swapchainExtent.width)
            {
                *width = swapchainExtent.width;
            }
            if (swapchainExtent.height)
            {
                *height = swapchainExtent.height;
            }
        }
        else
        {
            // If the surface size is defined, the swap chain size must match
            swapchainExtent = surfCaps.currentExtent;
            if (surfCaps.currentExtent.width)
            {
                *width = surfCaps.currentExtent.width;
            }
            if (surfCaps.currentExtent.height)
            {
                *height = surfCaps.currentExtent.height;
            }
        }

        m_SwapChainExtent = swapchainExtent;

#ifndef _WIN32
        // 需要重新矫正实际的SwapchainWidth和SwapchainHeight
        DrvOption::nSwapchainWidth  = m_SwapChainExtent.width;
        DrvOption::nSwapchainHeight = m_SwapChainExtent.height;
#endif

        // Select a present mode for the swapchain

        // The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
        // This mode waits for the vertical blank ("v-sync")
        swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

        // If v-sync is not requested, try to find a mailbox mode
        // It's the lowest latency non-tearing present mode available
        if (!vsync)
        {
            for (size_t i = 0; i < presentModeCount; i++)
            {
                if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
                if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) && (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR))
                {
                    swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                }
            }
        }

        // swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

        // Determine the number of images
        desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
        if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount))
        {
            desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
        }

        KGLogPrintf(KGLOG_INFO, "desired swapchain image count is:%d", desiredNumberOfSwapchainImages);

        // Find the transformation of the surface
        if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        {
            // We prefer a non-rotated transform
            preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        }
        else
        {
            preTransform = surfCaps.currentTransform;
        }

        // Find a supported composite alpha format (not all devices support alpha opaque)
        compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto& compositeAlphaFlag : compositeAlphaFlags)
        {
            if (surfCaps.supportedCompositeAlpha & compositeAlphaFlag)
            {
                compositeAlpha = compositeAlphaFlag;
                break;
            };
        }

        swapchainCI.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCI.pNext                 = NULL;
        swapchainCI.surface               = m_pSurface;
        swapchainCI.minImageCount         = desiredNumberOfSwapchainImages;
        swapchainCI.imageFormat           = m_colorFormat;
        swapchainCI.imageColorSpace       = m_ColorSpace;
        swapchainCI.imageExtent           = {*width, *height};
        swapchainCI.imageUsage            = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCI.preTransform          = (VkSurfaceTransformFlagBitsKHR)preTransform;
        swapchainCI.imageArrayLayers      = 1;
        swapchainCI.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCI.queueFamilyIndexCount = 0;
        swapchainCI.pQueueFamilyIndices   = NULL;
        swapchainCI.presentMode           = swapchainPresentMode;
        swapchainCI.oldSwapchain          = oldSwapchain;
        // Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
        swapchainCI.clipped               = VK_TRUE;
        swapchainCI.compositeAlpha        = compositeAlpha;

        // Set additional usage flag for blitting from the swapchain images if supported
        vks::vkGetPhysicalDeviceFormatProperties(m_pPhysicalDevice, m_colorFormat, &formatProps);
        if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT_KHR) || (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
        {
            swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        hRetCode = vks::vkCreateSwapchainKHR(m_pDevice, &swapchainCI, nullptr, &m_pSwapChain);
        KGLOG_COM_PROCESS_ERROR(hRetCode);

        // If an existing swap chain is re-created, destroy the old swap chain
        // This also cleans up all the presentable images
        if (oldSwapchain != VK_NULL_HANDLE)
        {
            for (uint32_t i = 0; i < m_nImageCount; i++)
            {
                SAFE_RELEASE(m_SwapChainRTs[i]);
                // vkDestroyImageView(device, buffers[i].view, nullptr);
                //SAFE_DELETE(m_ImageBarriers[i]);
                // SAFE_DELETE(m_ColorAttachmentBarriers[i]);
            }

            vks::vkDestroySwapchainKHR(m_pDevice, oldSwapchain, nullptr);
        }
        hRetCode = vks::vkGetSwapchainImagesKHR(m_pDevice, m_pSwapChain, &m_nImageCount, NULL);
        KGLOG_COM_PROCESS_ERROR(hRetCode);

        assert(m_nImageCount <= MAX_SWAP_CHAIN_COUNT);
        KGLogPrintf(KGLOG_INFO, "max swapchain image count is:%d", m_nImageCount);

        // Get the swap chain images
        m_Images.resize(m_nImageCount);
        hRetCode = vks::vkGetSwapchainImagesKHR(m_pDevice, m_pSwapChain, &m_nImageCount, m_Images.data());
        KGLOG_COM_PROCESS_ERROR(hRetCode);

        DrvOption::nSwapChainCount = m_nImageCount;

        rendertargetDesc.uWidth  = *width;
        rendertargetDesc.uHeight = *height;
        rendertargetDesc.uDepth  = 1;

        if (m_colorFormat == VK_FORMAT_B8G8R8A8_UNORM)
        {
            rendertargetDesc.eFormat = TEX_FORMAT_B8G8R8A8_UNORM;
        }
        else
        {
            rendertargetDesc.eFormat = TEX_FORMAT_R8G8B8A8_UNORM;
        }

        rendertargetDesc.uMipLevels    = 1;
        rendertargetDesc.uArraySize    = 1;
        rendertargetDesc.eSampleCount  = SAMPLE_COUNT_1_BIT;

        m_SwapChainRTs.resize(m_nImageCount);

        for (uint32_t i = 0; i < m_nImageCount; i++)
        {
            rendertargetDesc.cpNativeHandle = (void*)(m_Images[i]);
            sprintf(rendertargetDesc.m_szRTName, "SwapChainRenderTex%d", i);
            KGFX_GetGraphicDevice()->CreateRenderTarget(&m_SwapChainRTs[i], &rendertargetDesc, TRUE, nullptr);
            m_SwapChainRTs[i]->SetObjectName(rendertargetDesc.m_szRTName);
            KGLogPrintf(KGLOG_DEBUG, "SwapChain Color RenderTarget:%s, image:%p, height:%d", rendertargetDesc.m_szRTName, m_Images[i], rendertargetDesc.uHeight);
        }

        bResult = true;
    Exit0:        
        return bResult;
    }


    VkResult KVulkanSwapChain::AcquireNextImage(gfx::KVulkanSemaphore* pSemaphore, gfx::KVulkanFence* pFence, uint32_t* imageIndex)
    {
        // By setting timeout to UINT64_MAX we will always wait until the next image has been acquired or an actual error is thrown
        // With that we don't have to handle VK_NOT_READY
        assert(m_pDevice != VK_NULL_HANDLE);
        assert(m_pSwapChain != VK_NULL_HANDLE);
        assert(pSemaphore || pFence);
#ifdef _WIN32
        // 不设置超时时间，intel显卡会卡死
        const uint64_t timeouttime = 1000 * 1000 * 1000;
#else
        // 手机平台不会频繁创建swapchain,先不设置超时时间
        const uint64_t timeouttime = UINT64_MAX;
#endif

        VkResult vk_res = {};
        if (pFence != NULL)
        {
            vk_res = vks::vkAcquireNextImageKHR(m_pDevice, m_pSwapChain, timeouttime, VK_NULL_HANDLE, pFence->GetFence(), imageIndex);
            if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
            {
                *imageIndex = -1;
                pFence->Reset();
            }
            else
            {
                pFence->Submit();
            }
        }
        else
        {
            vk_res = vks::vkAcquireNextImageKHR(m_pDevice, m_pSwapChain, timeouttime, pSemaphore->GetSemaphore(), VK_NULL_HANDLE, imageIndex);
            if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
            {
                *imageIndex = -1;
            }
        }

        if (vk_res != VK_SUCCESS)
        {
            KGLogPrintf(KGLOG_WARNING, "KVulkanSwapChain::AcquireNextImage not success!!! code:%d", vk_res);
        }

        return vk_res;
    }

    VkResult KVulkanSwapChain::QueuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore)
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType            = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext            = NULL;
        presentInfo.swapchainCount   = 1;
        presentInfo.pSwapchains      = &m_pSwapChain;
        presentInfo.pImageIndices    = &imageIndex;
        // Check if a wait semaphore has been specified to wait for before presenting the image
        if (waitSemaphore != VK_NULL_HANDLE)
        {
            presentInfo.pWaitSemaphores    = &waitSemaphore;
            presentInfo.waitSemaphoreCount = 1;
        }

        return vks::vkQueuePresentKHR(queue, &presentInfo);
    }

    BOOL KVulkanSwapChain::IsClean()
    {
        if (m_pSwapChain == VK_NULL_HANDLE)
        {
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }

    void KVulkanSwapChain::Cleanup()
    {
        if (m_pSwapChain != VK_NULL_HANDLE)
        {
            for (uint32_t i = 0; i < m_nImageCount; i++)
            {
                SAFE_RELEASE(m_SwapChainRTs[i]);
                //SAFE_DELETE(m_ImageBarriers[i]);
                // SAFE_DELETE(m_ColorAttachmentBarriers[i]);
            }

            // for (uint32_t i = 0; i < imageCount; i++)
            //{
            //   vkDestroyImageView(device, buffers[i].view, nullptr);
            // }
        }
        if (m_pSurface != VK_NULL_HANDLE)
        {
            vks::vkDestroySwapchainKHR(m_pDevice, m_pSwapChain, nullptr);
            vks::vkDestroySurfaceKHR(m_pInstance, m_pSurface, nullptr);
        }


        m_pSurface   = VK_NULL_HANDLE;
        m_pSwapChain = VK_NULL_HANDLE;
    }

#if defined(_DIRECT2DISPLAY)
    /**
     * Create direct to display surface
     */
    void KVulkanSwapChain::CreateDirect2DisplaySurface(uint32_t width, uint32_t height)
    {
        uint32_t displayPropertyCount;

        // Get display property
        vks::vkGetPhysicalDeviceDisplayPropertiesKHR(m_pPhysicalDevice, &displayPropertyCount, NULL);
        VkDisplayPropertiesKHR* pDisplayProperties = new VkDisplayPropertiesKHR[displayPropertyCount];
        vks::vkGetPhysicalDeviceDisplayPropertiesKHR(m_pPhysicalDevice, &displayPropertyCount, pDisplayProperties);

        // Get plane property
        uint32_t planePropertyCount;
        vks::vkGetPhysicalDeviceDisplayPlanePropertiesKHR(m_pPhysicalDevice, &planePropertyCount, NULL);
        VkDisplayPlanePropertiesKHR* pPlaneProperties = new VkDisplayPlanePropertiesKHR[planePropertyCount];
        vks::vkGetPhysicalDeviceDisplayPlanePropertiesKHR(m_pPhysicalDevice, &planePropertyCount, pPlaneProperties);

        VkDisplayKHR                display = VK_NULL_HANDLE;
        VkDisplayModeKHR            displayMode;
        VkDisplayModePropertiesKHR* pModeProperties;
        bool                        foundMode = false;

        for (uint32_t i = 0; i < displayPropertyCount; ++i)
        {
            display = pDisplayProperties[i].display;
            uint32_t modeCount;
            vks::vkGetDisplayModePropertiesKHR(m_pPhysicalDevice, display, &modeCount, NULL);
            pModeProperties = new VkDisplayModePropertiesKHR[modeCount];
            vks::vkGetDisplayModePropertiesKHR(m_pPhysicalDevice, display, &modeCount, pModeProperties);

            for (uint32_t j = 0; j < modeCount; ++j)
            {
                const VkDisplayModePropertiesKHR* mode = &pModeProperties[j];

                if (mode->parameters.visibleRegion.width == width && mode->parameters.visibleRegion.height == height)
                {
                    displayMode = mode->displayMode;
                    foundMode   = true;
                    break;
                }
            }
            if (foundMode)
            {
                break;
            }
            delete[] pModeProperties;
        }

        if (!foundMode)
        {
            vks::tools::ExitFatal("Can't find a display and a display mode!", -1);
            return;
        }

        // Search for a best plane we can use
        uint32_t      bestPlaneIndex = UINT32_MAX;
        VkDisplayKHR* pDisplays      = NULL;
        for (uint32_t i = 0; i < planePropertyCount; i++)
        {
            uint32_t planeIndex = i;
            uint32_t displayCount;
            vks::vkGetDisplayPlaneSupportedDisplaysKHR(m_pPhysicalDevice, planeIndex, &displayCount, NULL);
            if (pDisplays)
            {
                delete[] pDisplays;
            }
            pDisplays = new VkDisplayKHR[displayCount];
            vks::vkGetDisplayPlaneSupportedDisplaysKHR(m_pPhysicalDevice, planeIndex, &displayCount, pDisplays);

            // Find a display that matches the current plane
            bestPlaneIndex = UINT32_MAX;
            for (uint32_t j = 0; j < displayCount; j++)
            {
                if (display == pDisplays[j])
                {
                    bestPlaneIndex = i;
                    break;
                }
            }
            if (bestPlaneIndex != UINT32_MAX)
            {
                break;
            }
        }

        if (bestPlaneIndex == UINT32_MAX)
        {
            vks::tools::ExitFatal("Can't find a plane for displaying!", -1);
            return;
        }

        VkDisplayPlaneCapabilitiesKHR planeCap;
        vks::vkGetDisplayPlaneCapabilitiesKHR(m_pPhysicalDevice, displayMode, bestPlaneIndex, &planeCap);
        VkDisplayPlaneAlphaFlagBitsKHR alphaMode = (VkDisplayPlaneAlphaFlagBitsKHR)0;

        if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR)
        {
            alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
        }
        else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR)
        {
            alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
        }
        else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR)
        {
            alphaMode = VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
        }
        else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)
        {
            alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        }

        VkDisplaySurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType              = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.pNext              = NULL;
        surfaceInfo.flags              = 0;
        surfaceInfo.displayMode        = displayMode;
        surfaceInfo.planeIndex         = bestPlaneIndex;
        surfaceInfo.planeStackIndex    = pPlaneProperties[bestPlaneIndex].currentStackIndex;
        surfaceInfo.transform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        surfaceInfo.globalAlpha        = 1.0;
        surfaceInfo.alphaMode          = alphaMode;
        surfaceInfo.imageExtent.width  = width;
        surfaceInfo.imageExtent.height = height;

        VkResult result = vkCreateDisplayPlaneSurfaceKHR(m_pInstance, &surfaceInfo, NULL, &m_pSurface);
        if (result != VK_SUCCESS)
        {
            vks::tools::ExitFatal("Failed to create surface!", result);
        }

        delete[] pDisplays;
        delete[] pModeProperties;
        delete[] pDisplayProperties;
        delete[] pPlaneProperties;
    }
#endif

    BOOL KVulkanSwapChain::TakeSnapshot(std::function<void(const unsigned char*, int, int)> fnCallback)
    {
        // BOOL bResult = false;
        //        BOOL bRetCode;
        //        uint32_t uWidth;
        //        uint32_t uHeight;
        //        unsigned char* pData = nullptr;
        //
        //	    bRetCode = GetSnapshotExtent(&uWidth, &uHeight);
        //        KGLOG_PROCESS_ERROR(bRetCode);
        //
        //        pData = (unsigned char*)malloc(uWidth * uHeight * 4);
        //        KGLOG_PROCESS_ERROR(pData != nullptr);
        //
        //        bRetCode = TakeSnapshot(uIndex, pData, uWidth, uHeight, [&fnCallback](unsigned char* pPixels, int nWidth, int nHeight)
        //        {
        //            fnCallback(pPixels, nWidth, nHeight);
        //        });
        //
        //        bResult = true;
        // Exit0:
        //        SAFE_FREE(pData);

        // 这里只做CacheData，uIndex 不接受，只取当前的swapchain
        if (!m_takeScreenRecording.bRecordingFrame)
        {
            m_takeSnapShot.bCaptureFrame     = true;
            m_takeSnapShot.fnCaptureCallBack = fnCallback;
            return true;
        }
        else
        {
            return false;
        }
    }
    void HandleRecordingFrame(unsigned char* pData, bool bRGBA, uint32_t ClientWidth, uint32_t ClientHeight, uint64_t uRowPitch)
    {
        PROF_CPU_DETAIL("HandleRecordingFrame");

        KGLOG_PROCESS_ERROR(NSEngine::g_pEngineCore);
        NSEngine::g_pEngineCore->HandleScreenVideoRecordFrame(pData, bRGBA, ClientWidth, ClientHeight, uRowPitch);
    Exit0:
        return;
    }
    BOOL KVulkanSwapChain::TakeScreenRecording(std::string strPath, int nFps, bool bTest)
    {
        BOOL bResult = false;

        m_bTest = bTest;
        if (!m_bStart)
        {
            m_bStart = true;
            if (!m_takeSnapShot.bCaptureFrame)
            {
                uint32_t uWidth   = 0;
                uint32_t uHeight  = 0;
                BOOL     bRetCode = false;
                bRetCode          = GetSnapshotExtent(&uWidth, &uHeight);
                KGLOG_PROCESS_ERROR(bRetCode);
                KG_ASSERT_EXIT(uWidth > 0);
                KG_ASSERT_EXIT(uHeight > 0);
                if (NSEngine::g_pEngineCore && !bTest)
                {
                    NSEngine::g_pEngineCore->StartScreenVideoRecord(strPath.c_str(), uWidth, uHeight, nFps);
                }
            }
        }
        else
        {
            StopScreenRecording();
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    void KVulkanSwapChain::StopScreenRecording()
    {
        m_bStart = false;
        m_bTest  = true;

        m_takeScreenRecording.clean();
        NSEngine::g_pEngineCore->StopScreenVideoRecord();
    }


    BOOL KVulkanSwapChain::PostTakeSnapShot(uint32_t uSwapChainIndex)
    {
        BOOL                 bResult        = false;
        BOOL                 bRetCode       = false;
        uint32_t             uWidth         = 0;
        uint32_t             uHeight        = 0;
        VkResult             hRetCode       = VkResult::VK_ERROR_UNKNOWN;
        VkDevice             pDevice        = GetVkDevice();
        vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        bool                 supportsBlit   = true;
        VkFormatProperties   formatProps;

        KG_PROCESS_ERROR(m_takeSnapShot.bCaptureFrame);

        // 进行命令录制
        KGLOG_PROCESS_ERROR(m_takeSnapShot.fnCaptureCallBack);

        bRetCode = GetSnapshotExtent(&uWidth, &uHeight);
        KGLOG_PROCESS_ERROR(bRetCode);

        {
            // Check blit support for source and destination
            gfx::KBlitRegion src = {0, 0, uWidth, uHeight};
            gfx::KBlitRegion dst = {0, 0, uWidth, uHeight};

            KG_ASSERT_EXIT(uWidth > 0);
            KG_ASSERT_EXIT(uHeight > 0);

            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, m_colorFormat, &formatProps);
            if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            {
                std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
                supportsBlit = false;
            }

            // Check if the device supports blitting to linear images
            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
            if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            {
                std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
                supportsBlit = false;
            }

            // Source for the copy is the last rendered swapchain image
            VkImage srcImage = m_Images[uSwapChainIndex];

            // Create the linear tiled destination image to copy to and to read the memory from
            VkImageCreateInfo imageCreateCI(vks::initializers::ImageCreateInfo());
            imageCreateCI.imageType     = VK_IMAGE_TYPE_2D;
            // Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
            imageCreateCI.format        = VK_FORMAT_R8G8B8A8_UNORM;
            imageCreateCI.extent.width  = uWidth;
            imageCreateCI.extent.height = uHeight;
            imageCreateCI.extent.depth  = 1;
            imageCreateCI.arrayLayers   = 1;
            imageCreateCI.mipLevels     = 1;
            imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateCI.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageCreateCI.tiling        = VK_IMAGE_TILING_LINEAR;
            imageCreateCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT;


            // Create the image
            VkMemoryRequirements memRequirements;

            if (DrvOption::bX3D_VK_USE_VMA)
            {
                BOOL bRet = pVulkanDevice->VMACreateImage(imageCreateCI, VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_TO_CPU, m_takeSnapShot.pDestImage, m_takeSnapShot.pVMAllocation);
                KGLOG_PROCESS_ERROR(bRet);
            }
            else
            {
                vks::vkCreateImage(pDevice, &imageCreateCI, nullptr, &m_takeSnapShot.pDestImage);

                // Create memory to back up the image
                VkMemoryAllocateInfo memAllocInfo(vks::initializers::MemoryAllocateInfo());

                vks::vkGetImageMemoryRequirements(pDevice, m_takeSnapShot.pDestImage, &memRequirements);
                memAllocInfo.allocationSize   = memRequirements.size;
                m_takeSnapShot.allocationSize = memRequirements.size;
                // Memory must be host visible to copy from
                memAllocInfo.memoryTypeIndex  = pVulkanDevice->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

                hRetCode = pVulkanDevice->AllocateMemory(pDevice, &memAllocInfo, nullptr, &m_takeSnapShot.pDstImageMemory, &m_takeSnapShot.uMemoryOffset, (uint32_t)memRequirements.alignment);
                KGLOG_COM_PROCESS_ERROR(hRetCode);

                vks::vkBindImageMemory(pDevice, m_takeSnapShot.pDestImage, m_takeSnapShot.pDstImageMemory, m_takeSnapShot.uMemoryOffset);
            }

            auto pRenderContext = gfx::GetRenderContext();

            // 录制命令到主commandBuffer blit command
            VkCommandBuffer copyCmd = (VkCommandBuffer)pRenderContext->GetCommandBufferNativeHandle();

            // translate begin
            // dst
            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                m_takeSnapShot.pDestImage,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );

            // src
            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                srcImage,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );

            if (supportsBlit)
            {
                VkOffset3D srcOffset;
                srcOffset.x = src.uX;
                srcOffset.y = src.uY;
                srcOffset.z = 0;

                VkOffset3D srcBlitSize;
                srcBlitSize.x = src.uWidth;
                srcBlitSize.y = src.uHeight;
                srcBlitSize.z = 1;

                VkOffset3D destOffset;
                destOffset.x = dst.uX;
                destOffset.y = dst.uY;
                destOffset.z = 0;

                VkOffset3D destBlitSize;
                destBlitSize.x = dst.uWidth;
                destBlitSize.y = dst.uHeight;
                destBlitSize.z = 1;

                VkImageBlit imageBlitRegion{};
                imageBlitRegion.srcSubresource.aspectMask = GetImageAspectMask(m_colorFormat);
                imageBlitRegion.srcSubresource.layerCount = 1;
                // imageBlitRegion.srcOffsets[0] = srcOffset;
                imageBlitRegion.srcOffsets[1]             = srcBlitSize;
                imageBlitRegion.dstSubresource.aspectMask = GetImageAspectMask(VK_FORMAT_R8G8B8A8_UNORM);
                imageBlitRegion.dstSubresource.layerCount = 1;
                // imageBlitRegion.dstOffsets[0] = destOffset;
                imageBlitRegion.dstOffsets[1]             = destBlitSize;

                // Issue the blit command
                vks::vkCmdBlitImage(
                    copyCmd,
                    srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    m_takeSnapShot.pDestImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &imageBlitRegion,
                    VK_FILTER_NEAREST
                );
            }
            else
            {
                // Otherwise use image copy (requires us to manually flip components)
                VkImageCopy imageCopyRegion{};
                imageCopyRegion.srcSubresource.aspectMask = GetImageAspectMask(m_colorFormat);
                imageCopyRegion.srcSubresource.layerCount = 1;
                imageCopyRegion.dstSubresource.aspectMask = GetImageAspectMask(VK_FORMAT_R8G8B8A8_UNORM);
                imageCopyRegion.dstSubresource.layerCount = 1;
                imageCopyRegion.extent.width              = src.uWidth;
                imageCopyRegion.extent.height             = src.uHeight;
                imageCopyRegion.extent.depth              = 1;

                // Issue the copy command
                vks::vkCmdCopyImage(
                    copyCmd,
                    srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    m_takeSnapShot.pDestImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &imageCopyRegion
                );
            }

            // translate end
            //  Transition destination image to general layout, which is the required layout for mapping the image memory later on
            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                m_takeSnapShot.pDestImage,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );

            vks::tools::InsertImageMemoryBarrier(
                copyCmd,
                srcImage,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            );

            bResult = true;
        }

    Exit0:
        // 执行错误了,并且在CaptureFrame的情况
        if (!bResult && m_takeSnapShot.bCaptureFrame)
        {
            m_takeSnapShot.clean();
        }

        return bResult;
    }


    BOOL KVulkanSwapChain::FinishTakeSnapShot()
    {
        PROF_CPU();

        BOOL           bResult = false;
        VkDevice       pDevice = GetVkDevice();
        uint32_t       uWidth  = 0;
        uint32_t       uHeight = 0;
        unsigned char* pData   = NULL;

        KG_PROCESS_ERROR(m_takeSnapShot.bCaptureFrame);

        KGLOG_PROCESS_ERROR(m_takeSnapShot.fnCaptureCallBack);

        // callback copy imagedata
        {
            bResult = GetSnapshotExtent(&uWidth, &uHeight);
            KGLOG_PROCESS_ERROR(bResult);

            // Get layout of the image (including row pitch)
            VkImageSubresource  subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
            VkSubresourceLayout subResourceLayout;
            vks::vkGetImageSubresourceLayout(pDevice, m_takeSnapShot.pDestImage, &subResource, &subResourceLayout);
            vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
            gfx::KGFX_GraphicDeviceVK* pGraphicDevice = KGFX_GetGraphicDeviceVKInternal();
            bool                 supportsBlit   = true;

            // 需要等待GPU完成
            pGraphicDevice->QueueWaitIdle(gfx::enumForProcessType::FOR_GRPAHIC);

            // Check blit support for source and destination
            VkFormatProperties formatProps;
            pData = (unsigned char*)malloc(uWidth * uHeight * 4);
            KGLOG_PROCESS_ERROR(pData != nullptr);

            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, m_colorFormat, &formatProps);
            if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            {
                std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
                supportsBlit = false;
            }

            // Check if the device supports blitting to linear images
            vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
            if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            {
                std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
                supportsBlit = false;
            }

            // Map image memory so we can start copying from it
            const char* data = nullptr;
            if (DrvOption::bX3D_VK_USE_VMA)
            {
                pVulkanDevice->VMAMapMemory(m_takeSnapShot.pVMAllocation, (void**)&data);
            }
            else
            {
                vks::vkMapMemory(pDevice, m_takeSnapShot.pDstImageMemory, m_takeSnapShot.uMemoryOffset, m_takeSnapShot.allocationSize, 0, (void**)&data);
            }

            data += subResourceLayout.offset;

            // If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have to manually swizzle color components
            bool colorSwizzle = false;
            // Check if source is BGR
            // Note: Not complete, only contains most common and basic BGR surface formats for demonstration purposes
            if (!supportsBlit)
            {
                std::vector<VkFormat> formatsBGR = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM};
                colorSwizzle                     = (std::find(formatsBGR.begin(), formatsBGR.end(), m_colorFormat) != formatsBGR.end());
            }

            if (!colorSwizzle)
            {
                unsigned char* pCacheCursor = pData;
                for (uint32_t y = 0; y < uHeight; y++)
                {
                    unsigned int* row = (unsigned int*)data;
                    for (uint32_t x = 0; x < uWidth; x++)
                    {
                        *pCacheCursor = *((char*)row);
                        ++pCacheCursor;
                        *pCacheCursor = *((char*)row + 1);
                        ++pCacheCursor;
                        *pCacheCursor = *((char*)row + 2);
                        ++pCacheCursor;
                        *pCacheCursor = *((char*)row + 3);
                        ;
                        ++pCacheCursor;

                        row++;
                    }
                    data += subResourceLayout.rowPitch;
                }
                m_takeSnapShot.fnCaptureCallBack(pData, uWidth, uHeight);
            }
            else
            {
                unsigned char* pCacheCursor = pData;
                for (uint32_t y = 0; y < uHeight; y++)
                {
                    unsigned int* row = (unsigned int*)data;
                    for (uint32_t x = 0; x < uWidth; x++)
                    {
                        *pCacheCursor = *((char*)row + 2);
                        ++pCacheCursor;
                        *pCacheCursor = *((char*)row + 1);
                        ++pCacheCursor;
                        *pCacheCursor = *((char*)row);
                        ++pCacheCursor;
                        *pCacheCursor = *((char*)row + 3);
                        ;
                        ++pCacheCursor;

                        row++;
                    }
                    data += subResourceLayout.rowPitch;
                }
                m_takeSnapShot.fnCaptureCallBack(pData, uWidth, uHeight);
            }

            if (DrvOption::bX3D_VK_USE_VMA)
            {
                pVulkanDevice->VMAUnmapMemory(m_takeSnapShot.pVMAllocation);
            }
            else
            {
                vks::vkUnmapMemory(pDevice, m_takeSnapShot.pDstImageMemory);
            }
        }

        bResult = true;

    Exit0:
        SAFE_FREE(pData);
        m_takeSnapShot.clean();
        return bResult;
    }

    BOOL KVulkanSwapChain::PostTakeScreenFrameRecording(uint32_t uSwapChainIndex)
    {
        PROF_CPU();
        BOOL                 bResult        = false;
        BOOL                 bRetCode       = false;
        uint32_t             uWidth         = 0;
        uint32_t             uHeight        = 0;
        VkResult             hRetCode       = VkResult::VK_ERROR_UNKNOWN;
        VkDevice             pDevice        = GetVkDevice();
        vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        bool                 supportsBlit   = true;
        VkFormatProperties   formatProps;

        if (m_bStart)
        {
            // 进行录制
            m_takeScreenRecording.bRecordingFrame = true;

            m_takeScreenRecording.fnCaptureCallBack = HandleRecordingFrame;

            bRetCode = GetSnapshotExtent(&uWidth, &uHeight);
            KGLOG_PROCESS_ERROR(bRetCode);

            {
                // Check blit support for source and destination
                gfx::KBlitRegion src = {0, 0, uWidth, uHeight};
                gfx::KBlitRegion dst = {0, 0, uWidth, uHeight};

                KG_ASSERT_EXIT(uWidth > 0);
                KG_ASSERT_EXIT(uHeight > 0);

                vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, m_colorFormat, &formatProps);
                if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
                {
                    std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
                    supportsBlit = false;
                }

                // Check if the device supports blitting to linear images
                vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
                if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
                {
                    std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
                    supportsBlit = false;
                }

                // Source for the copy is the last rendered swapchain image
                VkImage srcImage = m_Images[uSwapChainIndex];

                // Create the linear tiled destination image to copy to and to read the memory from
                VkImageCreateInfo imageCreateCI(vks::initializers::ImageCreateInfo());
                imageCreateCI.imageType     = VK_IMAGE_TYPE_2D;
                // Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
                imageCreateCI.format        = VK_FORMAT_R8G8B8A8_UNORM;
                imageCreateCI.extent.width  = uWidth;
                imageCreateCI.extent.height = uHeight;
                imageCreateCI.extent.depth  = 1;
                imageCreateCI.arrayLayers   = 1;
                imageCreateCI.mipLevels     = 1;
                imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageCreateCI.samples       = VK_SAMPLE_COUNT_1_BIT;
                imageCreateCI.tiling        = VK_IMAGE_TILING_LINEAR;
                imageCreateCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT;


                // Create the image
                VkMemoryRequirements memRequirements;

                if (DrvOption::bX3D_VK_USE_VMA)
                {
                    BOOL bRet = pVulkanDevice->VMACreateImage(imageCreateCI, VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_TO_CPU, m_takeScreenRecording.pDestImage, m_takeScreenRecording.pVMAllocation);
                    KGLOG_PROCESS_ERROR(bRet);
                }
                else
                {
                    vks::vkCreateImage(pDevice, &imageCreateCI, nullptr, &m_takeScreenRecording.pDestImage);

                    // Create memory to back up the image
                    VkMemoryAllocateInfo memAllocInfo(vks::initializers::MemoryAllocateInfo());

                    vks::vkGetImageMemoryRequirements(pDevice, m_takeScreenRecording.pDestImage, &memRequirements);
                    memAllocInfo.allocationSize          = memRequirements.size;
                    m_takeScreenRecording.allocationSize = memRequirements.size;
                    // Memory must be host visible to copy from
                    memAllocInfo.memoryTypeIndex         = pVulkanDevice->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

                    hRetCode = pVulkanDevice->AllocateMemory(pDevice, &memAllocInfo, nullptr, &m_takeScreenRecording.pDstImageMemory, &m_takeScreenRecording.uMemoryOffset, (uint32_t)memRequirements.alignment);
                    KGLOG_COM_PROCESS_ERROR(hRetCode);

                    vks::vkBindImageMemory(pDevice, m_takeScreenRecording.pDestImage, m_takeScreenRecording.pDstImageMemory, m_takeScreenRecording.uMemoryOffset);
                }


                auto pRenderContext = gfx::KGFX_GetGraphicDevice()->GetRenderContext();

                // 录制命令到主commandBuffer blit command
                VkCommandBuffer copyCmd = (VkCommandBuffer)pRenderContext->GetCommandBufferNativeHandle();

                // translate begin
                // dst
                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    m_takeScreenRecording.pDestImage,
                    0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );

                // src
                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    srcImage,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );

                if (supportsBlit)
                {
                    VkOffset3D srcOffset;
                    srcOffset.x = src.uX;
                    srcOffset.y = src.uY;
                    srcOffset.z = 0;

                    VkOffset3D srcBlitSize;
                    srcBlitSize.x = src.uWidth;
                    srcBlitSize.y = src.uHeight;
                    srcBlitSize.z = 1;

                    VkOffset3D destOffset;
                    destOffset.x = dst.uX;
                    destOffset.y = dst.uY;
                    destOffset.z = 0;

                    VkOffset3D destBlitSize;
                    destBlitSize.x = dst.uWidth;
                    destBlitSize.y = dst.uHeight;
                    destBlitSize.z = 1;

                    VkImageBlit imageBlitRegion{};
                    imageBlitRegion.srcSubresource.aspectMask = GetImageAspectMask(m_colorFormat);
                    imageBlitRegion.srcSubresource.layerCount = 1;
                    // imageBlitRegion.srcOffsets[0] = srcOffset;
                    imageBlitRegion.srcOffsets[1]             = srcBlitSize;
                    imageBlitRegion.dstSubresource.aspectMask = GetImageAspectMask(VK_FORMAT_R8G8B8A8_UNORM);
                    imageBlitRegion.dstSubresource.layerCount = 1;
                    // imageBlitRegion.dstOffsets[0] = destOffset;
                    imageBlitRegion.dstOffsets[1]             = destBlitSize;

                    // Issue the blit command
                    vks::vkCmdBlitImage(
                        copyCmd,
                        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        m_takeScreenRecording.pDestImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &imageBlitRegion,
                        VK_FILTER_NEAREST
                    );
                }
                else
                {
                    // Otherwise use image copy (requires us to manually flip components)
                    VkImageCopy imageCopyRegion{};
                    imageCopyRegion.srcSubresource.aspectMask = GetImageAspectMask(m_colorFormat);
                    imageCopyRegion.srcSubresource.layerCount = 1;
                    imageCopyRegion.dstSubresource.aspectMask = GetImageAspectMask(VK_FORMAT_R8G8B8A8_UNORM);
                    imageCopyRegion.dstSubresource.layerCount = 1;
                    imageCopyRegion.extent.width              = src.uWidth;
                    imageCopyRegion.extent.height             = src.uHeight;
                    imageCopyRegion.extent.depth              = 1;

                    // Issue the copy command
                    vks::vkCmdCopyImage(
                        copyCmd,
                        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        m_takeScreenRecording.pDestImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &imageCopyRegion
                    );
                }

                // translate end
                //  Transition destination image to general layout, which is the required layout for mapping the image memory later on
                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    m_takeScreenRecording.pDestImage,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_MEMORY_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );

                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    srcImage,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );
            }
        }

        bResult = true;

    Exit0:
        // 执行错误了,并且在CaptureFrame的情况
        if (!bResult && m_takeScreenRecording.bRecordingFrame)
        {
            m_takeScreenRecording.clean();
        }

        return bResult;
    }

    BOOL KVulkanSwapChain::FinishTakeScreenFrameRecording()
    {
        PROF_CPU_DETAIL("FinishTakeScreenFrameRecording");
        BOOL           bResult = false;
        VkDevice       pDevice = GetVkDevice();
        uint32_t       uWidth  = 0;
        uint32_t       uHeight = 0;
        unsigned char* pData   = NULL;
        if (m_bStart)
        {
            KG_PROCESS_ERROR(m_takeScreenRecording.bRecordingFrame);

            KGLOG_PROCESS_ERROR(m_takeScreenRecording.fnCaptureCallBack);

            // callback copy imagedata
            {
                bResult = GetSnapshotExtent(&uWidth, &uHeight);
                KGLOG_PROCESS_ERROR(bResult);
                // Get layout of the image (including row pitch)
                VkImageSubresource  subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
                VkSubresourceLayout subResourceLayout;
                vks::vkGetImageSubresourceLayout(pDevice, m_takeScreenRecording.pDestImage, &subResource, &subResourceLayout);
                vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
                gfx::KGFX_GraphicDeviceVK* pGraphicDevice = KGFX_GetGraphicDeviceVKInternal();
                bool                 supportsBlit   = true;

                // 需要等待GPU完成
                pGraphicDevice->QueueWaitIdle(gfx::enumForProcessType::FOR_GRPAHIC);

                // Check blit support for source and destination
                VkFormatProperties formatProps;
                pData = (unsigned char*)malloc(subResourceLayout.rowPitch * uHeight);
                KGLOG_PROCESS_ERROR(pData != nullptr);

                vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, m_colorFormat, &formatProps);
                if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
                {
                    std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
                    supportsBlit = false;
                }

                // Check if the device supports blitting to linear images
                vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
                if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
                {
                    std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
                    supportsBlit = false;
                }
                // Map image memory so we can start copying from it
                const char* data = nullptr;
                if (DrvOption::bX3D_VK_USE_VMA)
                {
                    pVulkanDevice->VMAMapMemory(m_takeScreenRecording.pVMAllocation, (void**)&data);
                }
                else
                {
                    vks::vkMapMemory(pDevice, m_takeScreenRecording.pDstImageMemory, m_takeScreenRecording.uMemoryOffset, m_takeScreenRecording.allocationSize, 0, (void**)&data);
                }

                data += subResourceLayout.offset;

                // If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have to manually swizzle color components
                bool colorSwizzle = false;
                // Check if source is BGR
                // Note: Not complete, only contains most common and basic BGR surface formats for demonstration purposes
                PROF_CPU_DETAIL("SaveImageData");
                if (!supportsBlit)
                {
                    std::vector<VkFormat> formatsBGR = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM};
                    colorSwizzle                     = (std::find(formatsBGR.begin(), formatsBGR.end(), m_colorFormat) != formatsBGR.end());
                }
                if (!colorSwizzle)
                {
                    unsigned char* pCacheCursor = pData;
                    memcpy(pCacheCursor, data, subResourceLayout.rowPitch * uHeight);
                    m_takeScreenRecording.fnCaptureCallBack(pData, true, uWidth, uHeight, subResourceLayout.rowPitch);
                }
                else
                {
                    unsigned char* pCacheCursor = pData;
                    memcpy(pCacheCursor, data, subResourceLayout.rowPitch * uHeight);
                    m_takeScreenRecording.fnCaptureCallBack(pData, false, uWidth, uHeight, subResourceLayout.rowPitch);
                }
                if (DrvOption::bX3D_VK_USE_VMA)
                {
                    pVulkanDevice->VMAUnmapMemory(m_takeScreenRecording.pVMAllocation);
                }
                else
                {
                    vks::vkUnmapMemory(pDevice, m_takeScreenRecording.pDstImageMemory);
                }
            }
        }


        bResult = true;

    Exit0:
        // SAFE_FREE(pData);
        m_takeScreenRecording.clean();
        return bResult;
    }

    BOOL KVulkanSwapChain::PostTakeScreenFrameRecordingNoUI(uint32_t uSwapChainIndex, void* pImage, uint32_t uWidth, uint32_t uHeight)
    {
        PROF_CPU();
        BOOL                 bResult        = false;
        BOOL                 bRetCode       = false;
        // uint32_t uWidth = 0;
        // uint32_t uHeight = 0;
        VkResult             hRetCode       = VkResult::VK_ERROR_UNKNOWN;
        VkDevice             pDevice        = GetVkDevice();
        vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        bool                 supportsBlit   = true;
        VkFormatProperties   formatProps;

        if (m_bStart && pImage)
        {
            // 进行录制
            m_takeScreenRecording.bRecordingFrame = true;

            m_takeScreenRecording.fnCaptureCallBack = HandleRecordingFrame;

            // bRetCode = GetSnapshotExtent(&uWidth, &uHeight);
            // KGLOG_PROCESS_ERROR(bRetCode);

            {
                // Source for the copy is the last rendered swapchain image
                VkImage srcImage = (VkImage)pImage;

                // Check blit support for source and destination
                gfx::KBlitRegion src = {0, 0, uWidth, uHeight};
                gfx::KBlitRegion dst = {0, 0, uWidth, uHeight};

                KG_ASSERT_EXIT(uWidth > 0);
                KG_ASSERT_EXIT(uHeight > 0);

                vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, m_colorFormat, &formatProps);
                if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
                {
                    // std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
                    supportsBlit = false;
                }

                // Check if the device supports blitting to linear images
                vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
                if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
                {
                    // std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
                    supportsBlit = false;
                }


                // Create the linear tiled destination image to copy to and to read the memory from
                VkImageCreateInfo imageCreateCI(vks::initializers::ImageCreateInfo());
                imageCreateCI.imageType     = VK_IMAGE_TYPE_2D;
                // Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
                imageCreateCI.format        = VK_FORMAT_R8G8B8A8_UNORM;
                imageCreateCI.extent.width  = uWidth;
                imageCreateCI.extent.height = uHeight;
                imageCreateCI.extent.depth  = 1;
                imageCreateCI.arrayLayers   = 1;
                imageCreateCI.mipLevels     = 1;
                imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageCreateCI.samples       = VK_SAMPLE_COUNT_1_BIT;
                imageCreateCI.tiling        = VK_IMAGE_TILING_LINEAR;
                imageCreateCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT;


                // Create the image
                VkMemoryRequirements memRequirements;

                if (DrvOption::bX3D_VK_USE_VMA)
                {
                    BOOL bRet = pVulkanDevice->VMACreateImage(imageCreateCI, VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_TO_CPU, m_takeScreenRecording.pDestImage, m_takeScreenRecording.pVMAllocation);
                    KGLOG_PROCESS_ERROR(bRet);
                }
                else
                {
                    vks::vkCreateImage(pDevice, &imageCreateCI, nullptr, &m_takeScreenRecording.pDestImage);

                    // Create memory to back up the image
                    VkMemoryAllocateInfo memAllocInfo(vks::initializers::MemoryAllocateInfo());

                    vks::vkGetImageMemoryRequirements(pDevice, m_takeScreenRecording.pDestImage, &memRequirements);
                    memAllocInfo.allocationSize          = memRequirements.size;
                    m_takeScreenRecording.allocationSize = memRequirements.size;
                    // Memory must be host visible to copy from
                    memAllocInfo.memoryTypeIndex         = pVulkanDevice->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

                    hRetCode = pVulkanDevice->AllocateMemory(pDevice, &memAllocInfo, nullptr, &m_takeScreenRecording.pDstImageMemory, &m_takeScreenRecording.uMemoryOffset, (uint32_t)memRequirements.alignment);
                    KGLOG_COM_PROCESS_ERROR(hRetCode);

                    vks::vkBindImageMemory(pDevice, m_takeScreenRecording.pDestImage, m_takeScreenRecording.pDstImageMemory, m_takeScreenRecording.uMemoryOffset);
                }

                auto            pRenderContext = gfx::GetRenderContext();
                // 录制命令到主commandBuffer blit command
                VkCommandBuffer copyCmd = (VkCommandBuffer)pRenderContext->GetCommandBufferNativeHandle();

                // translate begin
                // dst
                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    m_takeScreenRecording.pDestImage,
                    0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );

                // src
                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    srcImage,
                    VK_ACCESS_SHADER_READ_BIT, // VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );

                if (supportsBlit)
                {
                    VkOffset3D srcOffset;
                    srcOffset.x = src.uX;
                    srcOffset.y = src.uY;
                    srcOffset.z = 0;

                    VkOffset3D srcBlitSize;
                    srcBlitSize.x = src.uWidth;
                    srcBlitSize.y = src.uHeight;
                    srcBlitSize.z = 1;

                    VkOffset3D destOffset;
                    destOffset.x = dst.uX;
                    destOffset.y = dst.uY;
                    destOffset.z = 0;

                    VkOffset3D destBlitSize;
                    destBlitSize.x = dst.uWidth;
                    destBlitSize.y = dst.uHeight;
                    destBlitSize.z = 1;

                    VkImageBlit imageBlitRegion{};
                    imageBlitRegion.srcSubresource.aspectMask = GetImageAspectMask(m_colorFormat);
                    imageBlitRegion.srcSubresource.layerCount = 1;
                    // imageBlitRegion.srcOffsets[0] = srcOffset;
                    imageBlitRegion.srcOffsets[1]             = srcBlitSize;
                    imageBlitRegion.dstSubresource.aspectMask = GetImageAspectMask(VK_FORMAT_R8G8B8A8_UNORM);
                    imageBlitRegion.dstSubresource.layerCount = 1;
                    // imageBlitRegion.dstOffsets[0] = destOffset;
                    imageBlitRegion.dstOffsets[1]             = destBlitSize;

                    // Issue the blit command
                    vks::vkCmdBlitImage(
                        copyCmd,
                        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        m_takeScreenRecording.pDestImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &imageBlitRegion,
                        VK_FILTER_NEAREST
                    );
                }
                else
                {
                    // Otherwise use image copy (requires us to manually flip components)
                    VkImageCopy imageCopyRegion{};
                    imageCopyRegion.srcSubresource.aspectMask = GetImageAspectMask(m_colorFormat);
                    imageCopyRegion.srcSubresource.layerCount = 1;
                    imageCopyRegion.dstSubresource.aspectMask = GetImageAspectMask(VK_FORMAT_R8G8B8A8_UNORM);
                    imageCopyRegion.dstSubresource.layerCount = 1;
                    imageCopyRegion.extent.width              = src.uWidth;
                    imageCopyRegion.extent.height             = src.uHeight;
                    imageCopyRegion.extent.depth              = 1;

                    // Issue the copy command
                    vks::vkCmdCopyImage(
                        copyCmd,
                        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        m_takeScreenRecording.pDestImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &imageCopyRegion
                    );
                }

                // translate end
                //  Transition destination image to general layout, which is the required layout for mapping the image memory later on
                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    m_takeScreenRecording.pDestImage,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_MEMORY_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );

                vks::tools::InsertImageMemoryBarrier(
                    copyCmd,
                    srcImage,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT, // VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                );
            }
        }

        bResult = true;

    Exit0:
        // 执行错误了,并且在CaptureFrame的情况
        if (!bResult && m_takeScreenRecording.bRecordingFrame)
        {
            m_takeScreenRecording.clean();
        }

        return bResult;
    }

    BOOL KVulkanSwapChain::FinishTakeScreenFrameRecordingNoUI(uint32_t uWidth, uint32_t uHeight)
    {
        PROF_CPU();
        BOOL           bResult = false;
        VkDevice       pDevice = GetVkDevice();
        // uint32_t uWidth = 0;
        // uint32_t uHeight = 0;
        unsigned char* pData   = NULL;
        if (m_bStart)
        {
            KG_PROCESS_ERROR(m_takeScreenRecording.bRecordingFrame);

            KGLOG_PROCESS_ERROR(m_takeScreenRecording.fnCaptureCallBack);

            // callback copy imagedata
            {
                // bResult = GetSnapshotExtent(&uWidth, &uHeight);
                // KGLOG_PROCESS_ERROR(bResult);
                //  Get layout of the image (including row pitch)
                VkImageSubresource  subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
                VkSubresourceLayout subResourceLayout;
                vks::vkGetImageSubresourceLayout(pDevice, m_takeScreenRecording.pDestImage, &subResource, &subResourceLayout);
                vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
                gfx::KGFX_GraphicDeviceVK* pGraphicDevice = KGFX_GetGraphicDeviceVKInternal();
                bool                 supportsBlit   = true;

                // 需要等待GPU完成
                pGraphicDevice->QueueWaitIdle(gfx::enumForProcessType::FOR_GRPAHIC);

                // Check blit support for source and destination
                pData = (unsigned char*)malloc(subResourceLayout.rowPitch * uHeight);
                KGLOG_PROCESS_ERROR(pData != nullptr);

                const char* data = nullptr;
                if (DrvOption::bX3D_VK_USE_VMA)
                {
                    pVulkanDevice->VMAMapMemory(m_takeScreenRecording.pVMAllocation, (void**)&data);
                }
                else
                {
                    vks::vkMapMemory(pDevice, m_takeScreenRecording.pDstImageMemory, m_takeScreenRecording.uMemoryOffset, m_takeScreenRecording.allocationSize, 0, (void**)&data);
                }

                data += subResourceLayout.offset;

                // If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have to manually swizzle color components
                bool colorSwizzle = false;
                // Check if source is BGR
                // Note: Not complete, only contains most common and basic BGR surface formats for demonstration purposes
                PROF_CPU_DETAIL("SaveImageData");
                unsigned char* pCacheCursor = pData;

                // 从FSR拿的数据，都是RGBA
                memcpy(pCacheCursor, data, subResourceLayout.rowPitch * uHeight);
                if (!m_bTest)
                {
                    m_takeScreenRecording.fnCaptureCallBack(pData, true, uWidth, uHeight, subResourceLayout.rowPitch);
                }
                if (DrvOption::bX3D_VK_USE_VMA)
                {
                    pVulkanDevice->VMAUnmapMemory(m_takeScreenRecording.pVMAllocation);
                }
                else
                {
                    vks::vkUnmapMemory(pDevice, m_takeScreenRecording.pDstImageMemory);
                }
            }
        }


        bResult = true;

    Exit0:
        if (m_bTest)
        {
            SAFE_FREE(pData);
        }
        // 释放分配的内存在IPPENCODER处理数据后进行
        // SAFE_FREE(pData);
        m_takeScreenRecording.clean();
        return bResult;
    }

    BOOL KVulkanSwapChain::GetSnapshotExtent(uint32_t* puWidth, uint32_t* puHeight) const
    {
        BOOL bResult = false;

        if (puWidth != nullptr)
            *puWidth = m_SwapChainExtent.width;
        if (puHeight != nullptr)
            *puHeight = m_SwapChainExtent.height;

        bResult = true;
        return bResult;
    }

    //    BOOL KVulkanSwapChain::TakeSnapshot(uint32_t uIndex,
    //        unsigned char* pData, uint32_t uWidth, uint32_t uHeight,
    //        std::function<void(unsigned char*, int, int)> fnCallback)
    //    {
    //        BOOL bRet = false;
    //        VkResult hRetCode = VkResult::VK_ERROR_UNKNOWN;
    //        VkDevice pDevice = GetVkDevice();
    //        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
    //        gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
    //        bool supportsBlit = true;
    //        // Check blit support for source and destination
    //        VkFormatProperties formatProps;
    //        gfx::KBlitRegion src = { 0, 0, uWidth, uHeight };
    //        gfx::KBlitRegion dst = { 0, 0, uWidth, uHeight };
    //
    //        KG_ASSERT_EXIT(pData);
    //        KG_ASSERT_EXIT(uWidth > 0);
    //        KG_ASSERT_EXIT(uHeight > 0);
    //        KGLOG_PROCESS_ERROR(fnCallback);
    //
    //        vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, m_colorFormat, &formatProps);
    //		if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
    //		{
    //            std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
    //            supportsBlit = false;
    //        }
    //
    //        // Check if the device supports blitting to linear images
    //        vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
    //		if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
    //		{
    //            std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
    //            supportsBlit = false;
    //        }
    //
    //        {
    //            // Source for the copy is the last rendered swapchain image
    //            VkImage srcImage = m_Images[uIndex];
    //
    //            // Create the linear tiled destination image to copy to and to read the memory from
    //            VkImageCreateInfo imageCreateCI(vks::initializers::ImageCreateInfo());
    //            imageCreateCI.imageType = VK_IMAGE_TYPE_2D;
    //            // Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
    //            imageCreateCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    //            imageCreateCI.extent.width = uWidth;
    //            imageCreateCI.extent.height = uHeight;
    //            imageCreateCI.extent.depth = 1;
    //            imageCreateCI.arrayLayers = 1;
    //            imageCreateCI.mipLevels = 1;
    //            imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    //            imageCreateCI.samples = VK_SAMPLE_COUNT_1_BIT;
    //            imageCreateCI.tiling = VK_IMAGE_TILING_LINEAR;
    //            imageCreateCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    //            // Create the image
    //            VkImage dstImage = VK_NULL_HANDLE;
    //            VmaAllocation pVMAllocation = nullptr;
    //            uint32_t uMemoryOffset = 0;
    //            KVkDeviceMemory dstImageMemory = VK_NULL_HANDLE;
    //            VkMemoryRequirements memRequirements;
    //
    ////#if X3D_VK_USE_VMA
    //            if (DrvOption::bX3D_VK_USE_VMA)
    //            {
    //                BOOL bRet = pVulkanDevice->VMACreateImage(imageCreateCI, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, dstImage, pVMAllocation);
    //                KGLOG_COM_PROCESS_ERROR(bRet);
    //            }
    //            else
    //            {
    ////#else
    //                vks::vkCreateImage(pDevice, &imageCreateCI, nullptr, &dstImage);
    //                // Create memory to back up the image
    //
    //                VkMemoryAllocateInfo memAllocInfo(vks::initializers::MemoryAllocateInfo());
    //
    //                vks::vkGetImageMemoryRequirements(pDevice, dstImage, &memRequirements);
    //                memAllocInfo.allocationSize = memRequirements.size;
    //                // Memory must be host visible to copy from
    //                memAllocInfo.memoryTypeIndex = pVulkanDevice->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    //
    //
    //                hRetCode = pVulkanDevice->AllocateMemory(pDevice, &memAllocInfo, nullptr, &dstImageMemory, &uMemoryOffset, (uint32_t)memRequirements.alignment);
    //                KGLOG_COM_PROCESS_ERROR(hRetCode);
    //
    //                vks::vkBindImageMemory(pDevice, dstImage, dstImageMemory, uMemoryOffset);
    //            }
    ////#endif
    //
    //            gfx::_Blit(m_colorFormat, srcImage, src, VK_FORMAT_R8G8B8A8_UNORM, dstImage, dst, true);
    //
    //            // Get layout of the image (including row pitch)
    //            VkImageSubresource subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    //            VkSubresourceLayout subResourceLayout;
    //            vks::vkGetImageSubresourceLayout(pDevice, dstImage, &subResource, &subResourceLayout);
    //
    //            // Map image memory so we can start copying from it
    //
    //            const char *data = nullptr;
    ////#if X3D_VK_USE_VMA
    //            if (DrvOption::bX3D_VK_USE_VMA)
    //            {
    //                pVulkanDevice->VMAMapMemory(pVMAllocation, (void **)&data);
    //            }
    //            else
    //            {
    ////#else
    //                vks::vkMapMemory(pDevice, dstImageMemory, uMemoryOffset, memRequirements.size, 0, (void **)&data);
    //            }
    ////#endif
    //
    //            data += subResourceLayout.offset;
    //
    //            // If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have to manually swizzle color components
    //            bool colorSwizzle = false;
    //            // Check if source is BGR
    //            // Note: Not complete, only contains most common and basic BGR surface formats for demonstration purposes
    //            if (!supportsBlit)
    //            {
    //                std::vector<VkFormat> formatsBGR = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM};
    //                colorSwizzle = (std::find(formatsBGR.begin(), formatsBGR.end(), m_colorFormat) != formatsBGR.end());
    //            }
    //
    //            if (!colorSwizzle)
    //            {
    //                unsigned char *pCacheCursor = pData;
    //                for (uint32_t y = 0; y < uHeight; y++)
    //                {
    //                    unsigned int *row = (unsigned int *)data;
    //                    for (uint32_t x = 0; x < uWidth; x++)
    //                    {
    //                        *pCacheCursor = *((char *)row);
    //                        ++pCacheCursor;
    //                        *pCacheCursor = *((char *)row + 1);
    //                        ++pCacheCursor;
    //                        *pCacheCursor = *((char *)row + 2);
    //                        ++pCacheCursor;
    //                        *pCacheCursor = *((char *)row + 3);
    //                        ;
    //                        ++pCacheCursor;
    //
    //                        row++;
    //                    }
    //                    data += subResourceLayout.rowPitch;
    //                }
    //                fnCallback(pData, uWidth, uHeight);
    //            }
    //            else
    //            {
    //                unsigned char *pCacheCursor = pData;
    //                for (uint32_t y = 0; y < uHeight; y++)
    //                {
    //                    unsigned int *row = (unsigned int *)data;
    //                    for (uint32_t x = 0; x < uWidth; x++)
    //                    {
    //                        *pCacheCursor = *((char *)row + 2);
    //                        ++pCacheCursor;
    //                        *pCacheCursor = *((char *)row + 1);
    //                        ++pCacheCursor;
    //                        *pCacheCursor = *((char *)row);
    //                        ++pCacheCursor;
    //                        *pCacheCursor = *((char *)row + 3);
    //                        ;
    //                        ++pCacheCursor;
    //
    //                        row++;
    //                    }
    //                    data += subResourceLayout.rowPitch;
    //                }
    //                fnCallback(pData, uWidth, uHeight);
    //            }
    //
    //            // Clean up resources
    ////#if X3D_VK_USE_VMA
    //            if (DrvOption::bX3D_VK_USE_VMA)
    //            {
    //                pVulkanDevice->VMAUnmapMemory(pVMAllocation);
    //                pVulkanDevice->VMADestroyImage(dstImage, pVMAllocation);
    //            }
    ////#else
    //            else
    //            {
    //                vks::vkUnmapMemory(pDevice, dstImageMemory);
    //                // vks::vkFreeMemory(pDevice, dstImageMemory, nullptr);
    //                if (dstImageMemory)
    //                {
    //                    pVulkanDevice->FreeMemory(pDevice, dstImageMemory, nullptr, uMemoryOffset, memRequirements.size);
    //                }
    //
    //                vks::vkDestroyImage(pDevice, dstImage, nullptr);
    //            }
    ////#endif
    //        }
    //
    //        bRet = true;
    //    Exit0:
    //        return true;
    //    }

    uint8_t HalfToByte(uint16_t* pVal)
    {
        uint16_t aVal      = *pVal;
        int      nSignVal  = (aVal >> 15);
        int      nExponent = ((aVal >> 10) & 0x01f);
        int      nMantissa = (aVal & 0x03ff);
        int      nRawFloat32Data;

        if (nExponent == 31)
        {
            nExponent = 255;
        }
        else if (nExponent == 0)
        {
            nExponent = 0;
        }
        else
        {
            nExponent += (127 - 15);
        }

        nMantissa       <<= (23 - 10);
        nRawFloat32Data   = (nSignVal << 31) | (nExponent << 23) | nMantissa;
        float data        = *((float*)&nRawFloat32Data);
        data             *= 255.0f;
        uint8_t bits      = (uint8_t)NSKMath::Clamp(data, 0.0f, 255.0f);
        return bits;
    }

    BOOL KVulkanSwapChain::TakeSnapshotForMainPlayer(std::function<void(unsigned char*, int, int)> fnCallback)
    {
        return FALSE;
    }

    BOOL KVulkanSwapChain::TakeSnapshotForMainPlayerWithAlpha(std::function<void(unsigned char*, int, int)> fnCallback)
    {
        // 		BOOL           bRet    = false;
        // 		BOOL           bResult = false;
        // 		uint8_t*       pData = nullptr;
        // 		VkDevice             pDevice        = GetVkDevice();
        // 		vks::KVulkanDevice*  pVulkanDevice  = GetVulkanDevice();
        // 		gfx::KGraphicDevice* pGraphicDevice = gfx::GetGraphicDevice();
        // 		bool                 supportsBlit   = true;
        // 		// Check blit support for source and destination
        // 		VkFormatProperties   formatProps;
        //
        // 		KGLOG_PROCESS_ERROR(fnCallback);
        //
        // 		// Check if the device supports blitting to linear images, snap shot format is optimalTiling, so it need check
        // 		vks::vkGetPhysicalDeviceFormatProperties(pVulkanDevice->m_pPhysicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &formatProps);
        // 		if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
        // 		{
        // 			std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
        // 			supportsBlit = false;
        // 		}
        //
        //         //new dst rt is linear
        // 		if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
        // 		{
        // 			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
        // 			supportsBlit = false;
        // 		}
        //
        //         {
        //             KEngineCore* pEngineCore = NSEngine::GetEngineCoreInterface();
        //             IKRender* pRender = pEngineCore->GetRender();
        //             KRenderTarget* pSnapshotRT = pRender->OwnerSnapshotRT();
        //             KGLOG_PROCESS_ERROR(pSnapshotRT);
        //             VkImage srcImage = (VkImage)pSnapshotRT->GetVKImage();
        //             uint32_t uWidth = pSnapshotRT->GetWidth();
        //             uint32_t uHeight = pSnapshotRT->GetHeight();
        //             gfx::KBlitRegion src = { 0, 0, uWidth, uHeight };
        //             gfx::KBlitRegion dst = { 0, 0, uWidth, uHeight };
        //             pData = (uint8_t*)malloc(uWidth * uHeight * 4);
        //             KG_ASSERT_EXIT(pData);
        //             KG_ASSERT_EXIT(uWidth > 0);
        //             KG_ASSERT_EXIT(uHeight > 0);
        //
        //             // Create the linear tiled destination image to copy to and to read the memory from
        //             VkImageCreateInfo imageCreateCI(vks::initializers::ImageCreateInfo());
        //             imageCreateCI.imageType = VK_IMAGE_TYPE_2D;
        //             // Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
        //             imageCreateCI.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        //             imageCreateCI.extent.width = uWidth;
        //             imageCreateCI.extent.height = uHeight;
        //             imageCreateCI.extent.depth = 1;
        //             imageCreateCI.arrayLayers = 1;
        //             imageCreateCI.mipLevels = 1;
        //             imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        //             imageCreateCI.samples = VK_SAMPLE_COUNT_1_BIT;
        //             imageCreateCI.tiling = VK_IMAGE_TILING_LINEAR;
        //             imageCreateCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        //             // Create the image
        //             VkImage dstImage = VK_NULL_HANDLE;
        // #if X3D_VK_USE_VMA
        //             VmaAllocation pVMAllocation = nullptr;
        //             BOOL          bRet = pVulkanDevice->VMACreateImage(imageCreateCI, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, dstImage, pVMAllocation);
        //             KGLOG_COM_PROCESS_ERROR(bRet);
        // #else
        //             vks::vkCreateImage(pDevice, &imageCreateCI, nullptr, &dstImage);
        //             // Create memory to back up the image
        //             VkMemoryRequirements memRequirements;
        //             VkMemoryAllocateInfo memAllocInfo(vks::initializers::MemoryAllocateInfo());
        //             KVkDeviceMemory      dstImageMemory = VK_NULL_HANDLE;
        //             vks::vkGetImageMemoryRequirements(pDevice, dstImage, &memRequirements);
        //             memAllocInfo.allocationSize = memRequirements.size;
        //             // Memory must be host visible to copy from
        //             memAllocInfo.memoryTypeIndex = pVulkanDevice->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        //
        //             uint32_t uMemoryOffset = 0;
        //             VkResult hRetCode = pVulkanDevice->AllocateMemory(pDevice, &memAllocInfo, nullptr, &dstImageMemory, &uMemoryOffset, (uint32_t)memRequirements.alignment);
        //             KGLOG_COM_PROCESS_ERROR(hRetCode);
        //
        //             vks::vkBindImageMemory(pDevice, dstImage, dstImageMemory, uMemoryOffset);
        // #endif
        //
        //             gfx::_Blit(VK_FORMAT_R16G16B16A16_SFLOAT, srcImage, src, VK_FORMAT_R16G16B16A16_SFLOAT, dstImage, dst, false);
        //
        //             // Get layout of the image (including row pitch)
        //             VkImageSubresource  subResource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        //             VkSubresourceLayout subResourceLayout;
        //             vks::vkGetImageSubresourceLayout(pDevice, dstImage, &subResource, &subResourceLayout);
        //
        //             // Map image memory so we can start copying from it
        //
        //             uint8_t* data = nullptr;
        // #if X3D_VK_USE_VMA
        //             pVulkanDevice->VMAMapMemory(pVMAllocation, (void**)&data);
        // #else
        //             vks::vkMapMemory(pDevice, dstImageMemory, uMemoryOffset, memRequirements.size, 0, (void**)&data);
        // #endif
        //
        //             data += subResourceLayout.offset;
        //             uint8_t* pCacheCursor = pData;
        //
        //             for (uint32_t y = 0; y < uHeight; y++)
        //             {
        //                 uint16_t* row = (uint16_t*)data;
        //                 for (uint32_t x = 0; x < uWidth; x++)
        //                 {
        //                     *pCacheCursor++ = HalfToByte(row);
        //                     *pCacheCursor++ = HalfToByte(row + 1);
        //                     *pCacheCursor++ = HalfToByte(row + 2);
        //                     *pCacheCursor++ = HalfToByte(row + 3);
        //
        //                     row += 4;
        //                 }
        //
        //                 data += subResourceLayout.rowPitch;
        //             }
        //
        //             fnCallback(pData, uWidth, uHeight);
        //
        //             // Clean up resources
        //             SAFE_RELEASE(pSnapshotRT);
        // 			pSnapshotRT = nullptr;
        // #if X3D_VK_USE_VMA
        //             pVulkanDevice->VMAUnmapMemory(pVMAllocation);
        //             pVulkanDevice->VMADestroyImage(dstImage, pVMAllocation);
        // #else
        //             vks::vkUnmapMemory(pDevice, dstImageMemory);
        //             // vks::vkFreeMemory(pDevice, dstImageMemory, nullptr);
        //             if (dstImageMemory)
        //             {
        //                 pVulkanDevice->FreeMemory(pDevice, dstImageMemory, nullptr, uMemoryOffset, memRequirements.size);
        //             }
        //
        //             vks::vkDestroyImage(pDevice, dstImage, nullptr);
        // #endif
        //         }
        //
        // 		bRet = true;
        // 	Exit0:
        // 		return true;
        return FALSE;
    }

    void KVulkanSwapChain::TaskSnapShotObject::clean()
    {
        if (!bCaptureFrame)
        {
            return;
        }

        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        VkDevice            pDevice       = GetVkDevice();

        bCaptureFrame     = false;
        fnCaptureCallBack = nullptr;

        if (DrvOption::bX3D_VK_USE_VMA)
        {
            if (pVMAllocation && pDestImage)
            {
                pVulkanDevice->VMADestroyImage(pDestImage, pVMAllocation);
            }
        }
        else
        {
            if (pDstImageMemory)
            {
                pVulkanDevice->FreeMemory(pDevice, pDstImageMemory, nullptr, uMemoryOffset, (uint32_t)allocationSize);
            }

            vks::vkDestroyImage(pDevice, pDestImage, nullptr);
        }

        pDestImage      = VK_NULL_HANDLE;
        pDstImageMemory = VK_NULL_HANDLE;
        uMemoryOffset   = 0;
        allocationSize  = 0;
        pVMAllocation   = VK_NULL_HANDLE;
    }


    void KVulkanSwapChain::TaskScreenRecordingObject::clean()
    {
        if (!bRecordingFrame)
        {
            return;
        }

        vks::KVulkanDevice* pVulkanDevice = GetVulkanDevice();
        VkDevice            pDevice       = GetVkDevice();

        bRecordingFrame   = false;
        fnCaptureCallBack = nullptr;

        if (DrvOption::bX3D_VK_USE_VMA)
        {
            if (pVMAllocation && pDestImage)
            {
                pVulkanDevice->VMADestroyImage(pDestImage, pVMAllocation);
            }
        }
        else
        {
            if (pDstImageMemory)
            {
                pVulkanDevice->FreeMemory(pDevice, pDstImageMemory, nullptr, uMemoryOffset, (uint32_t)allocationSize);
            }

            vks::vkDestroyImage(pDevice, pDestImage, nullptr);
        }

        pDestImage      = VK_NULL_HANDLE;
        pDstImageMemory = VK_NULL_HANDLE;
        uMemoryOffset   = 0;
        allocationSize  = 0;
        pVMAllocation   = VK_NULL_HANDLE;
    }

} // namespace vks

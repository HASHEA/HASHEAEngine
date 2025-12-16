#pragma once

#include <stdlib.h>
#include <string>
#include <assert.h>
#include <stdio.h>
#include <vector>

#include "KVulkanFunc.h"
#include "KGBaseDef/Public/core_base_macro.h"
#include "../IGFX_Private.h"
#include "GFXVulkan.h"

#ifdef __ANDROID__
#include "KVulkan.h"
#endif

// Macro to get a procedure address based on a vulkan instance
// #define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                        \
//{                                                                       \
//	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetInstanceProcAddr(inst, "vk"#entrypoint)); \
//	if (fp##entrypoint == NULL)                                         \
//	{																    \
//		exit(1);                                                        \
//	}                                                                   \
//}
//
//// Macro to get a procedure address based on a vulkan device
// #define GET_DEVICE_PROC_ADDR(dev, entrypoint)                           \
//{                                                                       \
//	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetDeviceProcAddr(dev, "vk"#entrypoint));   \
//	if (fp##entrypoint == NULL)                                         \
//	{																    \
//		exit(1);                                                        \
//	}                                                                   \
//}
namespace gfx
{
    struct KSwapChainBuffers
    {
        VkImage     image;
        VkImageView view;
    };

    class KVulkanSwapChain : public KGfxRef
    {
        friend class gfx::KVulkanGraphicContext;

    public:
        VkSurfaceKHR   m_pSurface   = nullptr;
        VkSwapchainKHR m_pSwapChain = VK_NULL_HANDLE;

    protected:
        VkInstance       m_pInstance;
        VkDevice         m_pDevice;
        VkPhysicalDevice m_pPhysicalDevice;

        // Function pointers
        // PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
        // PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
        // PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
        // PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
        // PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
        // PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
        // PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
        // PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
        // PFN_vkQueuePresentKHR fpQueuePresentKHR;

        VkFormat        m_colorFormat;
        VkColorSpaceKHR m_ColorSpace;
        /** @brief Handle to the current swap chain, required for recreation */

        VkExtent2D m_SwapChainExtent;

        std::vector<VkImage>                 m_Images;
        // std::vector<KSwapChainBuffers> buffers;
        std::vector<KRenderTarget*>          m_SwapChainRTs;
        /** @brief Queue family index of the detected graphics and presenting device queue */
        uint32_t                             queueNodeIndex = UINT32_MAX;
        uint32_t                             m_nImageCount;

    private:
        struct TaskSnapShotObject
        {
            bool                                                bCaptureFrame = false;
            std::function<void(const unsigned char*, int, int)> fnCaptureCallBack;
            // int nCaptureSwapChainIndex = -1;

            VkImage pDestImage = VK_NULL_HANDLE;

            KVkDeviceMemory pDstImageMemory = VK_NULL_HANDLE;
            uint32_t        uMemoryOffset   = 0;
            VkDeviceSize    allocationSize  = 0;

            VmaAllocation pVMAllocation = nullptr;

            void clean();
        };


        TaskSnapShotObject m_takeSnapShot;

        struct TaskScreenRecordingObject
        {
            bool                                                                                      bRecordingFrame = false;
            std::function<void(unsigned char*, bool, unsigned int, unsigned int, unsigned long long)> fnCaptureCallBack;

            VkImage pDestImage = VK_NULL_HANDLE;

            KVkDeviceMemory pDstImageMemory = VK_NULL_HANDLE;
            uint32_t        uMemoryOffset   = 0;
            VkDeviceSize    allocationSize  = 0;

            VmaAllocation pVMAllocation = nullptr;

            void clean();
        };
        TaskScreenRecordingObject m_takeScreenRecording;
        bool                      m_bStart = false;
        bool                      m_bTest  = true;

    public:
        /** @brief Creates the platform specific surface abstraction of the native platform window used for presentation */
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        void InitSurface(void* platformHandle, void* platformWindow);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        void InitSurface(ANativeWindow* window);
#elif defined(VK_USE_PLATFORM_OHOS)
        void InitSurface(NativeWindow* window);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        void InitSurface(wl_display* display, wl_surface* window);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        void InitSurface(xcb_connection_t* connection, xcb_window_t window);
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
        void InitSurface(void* view);
#elif defined(_DIRECT2DISPLAY)
        void InitSurface(uint32_t width, uint32_t height);
#endif

        gfx::enumTextureFormat GetSurfaceFormat();

        /**
         * Set instance, physical and logical device to use for the swapchain and get all required function pointers
         *
         * @param instance Vulkan instance to use
         * @param physicalDevice Physical device used to query properties and formats relevant to the swapchain
         * @param device Logical representation of the device to create the swapchain for
         *
         */
        void Connect(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);


        /**
         * Create the swapchain and get it's images with given width and height
         *
         * @param width Pointer to the width of the swapchain (may be adjusted to fit the requirements of the swapchain)
         * @param height Pointer to the height of the swapchain (may be adjusted to fit the requirements of the swapchain)
         * @param vsync (Optional) Can be used to force vsync'd rendering (by using VK_PRESENT_MODE_FIFO_KHR as presentation mode)
         */
        BOOL Create(const char* szName, uint32_t* width, uint32_t* height, BOOL vsync = false);

        /**
         * Acquires the next image in the swap chain
         *
         * @param presentCompleteSemaphore (Optional) Semaphore that is signaled when the image is ready for use
         * @param imageIndex Pointer to the image index that will be increased if the next image could be acquired
         *
         * @note The function will always wait until the next image has been acquired by setting timeout to UINT64_MAX
         *
         * @return VkResult of the image acquisition
         */
        VkResult AcquireNextImage(gfx::KVulkanSemaphore* pSemaphore, gfx::KVulkanFence* pFence, uint32_t* imageIndex);

        /**
         * Queue an image for presentation
         *
         * @param queue Presentation queue for presenting the image
         * @param imageIndex Index of the swapchain image to queue for presentation
         * @param waitSemaphore (Optional) Semaphore that is waited on before the image is presented (only used if != VK_NULL_HANDLE)
         *
         * @return VkResult of the queue presentation
         */
        VkResult QueuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE);

        BOOL IsClean();

        //gfx::KPipelineBarrier& GetSwapImageBarrier(uint32_t imageIndex);
        //gfx::KPipelineBarrier& GetSwapAttachmentBarrier(uint32_t imageIndex);
        /**
         * Destroy and free Vulkan resources used for the swapchain
         */
        void                   Cleanup();

        bool IsStartFrameRecording() { return m_bStart; };
#if defined(_DIRECT2DISPLAY)
        /**
         * Create direct to display surface
         */
        void CreateDirect2DisplaySurface(uint32_t width, uint32_t height);
#endif

        BOOL TakeSnapshot(std::function<void(const unsigned char*, int, int)> fnCallback);
        BOOL TakeScreenRecording(std::string strPath, int nFps, bool bTest);
        void StopScreenRecording();
        BOOL GetSnapshotExtent(uint32_t* puWidth, uint32_t* puHeight) const;
        // BOOL TakeSnapshot(uint32_t uIndex, unsigned char* pData, uint32_t uWidth, uint32_t uHeight,
        //	std::function<void(unsigned char*, int, int)> fnCallback);

        BOOL PostTakeSnapShot(uint32_t uSwapChainIndex);
        BOOL FinishTakeSnapShot();

        BOOL PostTakeScreenFrameRecording(uint32_t uSwapChainIndex);
        BOOL FinishTakeScreenFrameRecording();

        BOOL PostTakeScreenFrameRecordingNoUI(uint32_t uSwapChainIndex, void* pImage, uint32_t uWidth, uint32_t uHeight);
        BOOL FinishTakeScreenFrameRecordingNoUI(uint32_t uWidth, uint32_t uHeight);

        BOOL TakeSnapshotForMainPlayer(std::function<void(unsigned char*, int, int)> fnCallback);
        BOOL TakeSnapshotForMainPlayerWithAlpha(std::function<void(unsigned char*, int, int)> fnCallback);

        void* GetSurface()
        {
            return m_pSurface;
        }
    };
} // namespace vks

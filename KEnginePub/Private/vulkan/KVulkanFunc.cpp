#include "KVulkanFunc.h"
#include "KVulkanFuncStub.h"
#include "KEnginePub/Public/IKEngineOption.h"
#include "KEnginePub/Public/IKEnginePerformance.h"
#include "KBase/Public/str/KStrHelper.h"
#include "KBase/Public/thread/KThread.h"
//////////////////////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"
#include "KBase/Public/KMemLeak.h"

namespace vks
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
    VkResult vkCreateWin32SurfaceKHR(
        VkInstance                         instance,
        const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*       pAllocator,
        VkSurfaceKHR*                      pSurface
    )
    {
        return vkfunc::vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    }
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
    VkResult vkCreateAndroidSurfaceKHR(
        VkInstance                           instance,
        const VkAndroidSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*         pAllocator,
        VkSurfaceKHR*                        pSurface
    )
    {
        return vkfunc::vkCreateAndroidSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    }

    VkResult vkGetAndroidHardwareBufferPropertiesANDROID(
        VkDevice                                  device,
        const struct AHardwareBuffer*             buffer,
        VkAndroidHardwareBufferPropertiesANDROID* pProperties
    )
    {
        return vkfunc::vkGetAndroidHardwareBufferPropertiesANDROID(device, buffer, pProperties);
    }

    VkResult vkCreateSamplerYcbcrConversion(
        VkDevice                                  device,
        const VkSamplerYcbcrConversionCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*              pAllocator,
        VkSamplerYcbcrConversion*                 pYcbcrConversion
    )
    {
        return vkfunc::vkCreateSamplerYcbcrConversion(device, pCreateInfo, pAllocator, pYcbcrConversion);
    }

    void vkDestroySamplerYcbcrConversion(
        VkDevice                     device,
        VkSamplerYcbcrConversion     ycbcrConversion,
        const VkAllocationCallbacks* pAllocator
    )
    {
        return vkfunc::vkDestroySamplerYcbcrConversion(device, ycbcrConversion, pAllocator);
    }
#endif

#ifdef VK_USE_PLATFORM_OHOS
    VkResult vkCreateSurfaceOHOS(
        VkInstance                     instance,
        const VkSurfaceCreateInfoOHOS* pCreateInfo,
        const VkAllocationCallbacks*   pAllocator,
        VkSurfaceKHR*                  pSurface
    )
    {
        return vkfunc::vkCreateSurfaceOHOS(instance, pCreateInfo, pAllocator, pSurface);
    }
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult vkCreateXlibSurfaceKHR(
        VkInstance                        instance,
        const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*      pAllocator,
        VkSurfaceKHR*                     pSurface
    );
    {
        return vkfunc::vkCreateXlibSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    }
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult vkCreateXcbSurfaceKHR(
        VkInstance                       instance,
        const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*     pAllocator,
        VkSurfaceKHR*                    pSurface
    )
    {
        return vkfunc::vkCreateXcbSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    }
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult vkCreateWaylandSurfaceKHR(
        VkInstance                           instance,
        const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*         pAllocator,
        VkSurfaceKHR*                        pSurface
    )
    {
        return vkfunc::vkCreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    }
#endif

#ifdef VK_USE_PLATFORM_MACOS_MVK
    VkResult vkCreateMacOSSurfaceMVK(
        VkInstance                         instance,
        const VkMacOSSurfaceCreateInfoMVK* pCreateInfo,
        const VkAllocationCallbacks*       pAllocator,
        VkSurfaceKHR*                      pSurface
    )
    {
        return vkfunc::vkCreateMacOSSurfaceMVK(instance, pCreateInfo, pAllocator, pSurface);
    }
#endif

#ifdef USE_PLATFORM_NULLWS
    VkResult vkGetPhysicalDeviceDisplayPropertiesKHR(
        VkPhysicalDevice        physicalDevice,
        uint32_t*               pPropertyCount,
        VkDisplayPropertiesKHR* pProperties
    )
    {
        return vkfunc::vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, pPropertyCount, pProperties);
    }

    VkResult vkGetDisplayModePropertiesKHR(
        VkPhysicalDevice            physicalDevice,
        VkDisplayKHR                display,
        uint32_t*                   pPropertyCount,
        VkDisplayModePropertiesKHR* pProperties
    )
    {
        return vkfunc::vkGetDisplayModePropertiesKHR(physicalDevice, display, pPropertyCount, pProperties);
    }

    VkResult vkCreateDisplayPlaneSurfaceKHR(
        VkInstance                           instance,
        const VkDisplaySurfaceCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*         pAllocator,
        VkSurfaceKHR*                        pSurface
    )
    {
        return vkfunc::vkCreateDisplayPlaneSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    }
#endif
    /***************************************************** ray tracing ***********************************************/
    void vkCreateAccelerationStructureKHR(
        VkDevice                                    device,
        const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*                pAllocator,
        VkAccelerationStructureKHR*                 pAccelerationStructure
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCreateAccelerationStructureKHR(device, pCreateInfo, pAllocator, pAccelerationStructure);
    }

    void vkDestroyAccelerationStructureKHR(
        VkDevice                     device,
        VkAccelerationStructureKHR   accelerationStructure,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyAccelerationStructureKHR(device, accelerationStructure, pAllocator);
    }

    void vkCmdBuildAccelerationStructuresKHR(
        VkCommandBuffer                                        commandBuffer,
        uint32_t                                               infoCount,
        const VkAccelerationStructureBuildGeometryInfoKHR*     pInfos,
        const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
    }

    void vkCmdBuildAccelerationStructuresIndirectKHR(
        VkCommandBuffer                                    commandBuffer,
        uint32_t                                           infoCount,
        const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
        const VkDeviceAddress*                             pIndirectDeviceAddresses,
        const uint32_t*                                    pIndirectStrides,
        const uint32_t* const*                             ppMaxPrimitiveCounts
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdBuildAccelerationStructuresIndirectKHR(commandBuffer, infoCount, pInfos, pIndirectDeviceAddresses, pIndirectStrides, ppMaxPrimitiveCounts);
    }

    VkResult vkBuildAccelerationStructuresKHR(
        VkDevice                                               device,
        VkDeferredOperationKHR                                 deferredOperation,
        uint32_t                                               infoCount,
        const VkAccelerationStructureBuildGeometryInfoKHR*     pInfos,
        const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkBuildAccelerationStructuresKHR(device, deferredOperation, infoCount, pInfos, ppBuildRangeInfos);
    }

    VkResult vkCopyAccelerationStructureKHR(
        VkDevice                                  device,
        VkDeferredOperationKHR                    deferredOperation,
        const VkCopyAccelerationStructureInfoKHR* pInfo
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCopyAccelerationStructureKHR(device, deferredOperation, pInfo);
    }

    VkResult vkCopyAccelerationStructureToMemoryKHR(
        VkDevice                                          device,
        VkDeferredOperationKHR                            deferredOperation,
        const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCopyAccelerationStructureToMemoryKHR(device, deferredOperation, pInfo);
    }

    VkResult vkCopyMemoryToAccelerationStructureKHR(
        VkDevice                                          device,
        VkDeferredOperationKHR                            deferredOperation,
        const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCopyMemoryToAccelerationStructureKHR(device, deferredOperation, pInfo);
    }

    VkResult vkWriteAccelerationStructuresPropertiesKHR(
        VkDevice                          device,
        uint32_t                          accelerationStructureCount,
        const VkAccelerationStructureKHR* pAccelerationStructures,
        VkQueryType                       queryType,
        size_t                            dataSize,
        void*                             pData,
        size_t                            stride
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkWriteAccelerationStructuresPropertiesKHR(device, accelerationStructureCount, pAccelerationStructures, queryType, dataSize, pData, stride);
    }

    void vkCmdCopyAccelerationStructureKHR(
        VkCommandBuffer                           commandBuffer,
        const VkCopyAccelerationStructureInfoKHR* pInfo
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyAccelerationStructureKHR(commandBuffer, pInfo);
    }

    void vkCmdCopyAccelerationStructureToMemoryKHR(
        VkCommandBuffer                                   commandBuffer,
        const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyAccelerationStructureToMemoryKHR(commandBuffer, pInfo);
    }

    void vkCmdCopyMemoryToAccelerationStructureKHR(
        VkCommandBuffer                                   commandBuffer,
        const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyMemoryToAccelerationStructureKHR(commandBuffer, pInfo);
    }

    VkDeviceAddress vkGetAccelerationStructureDeviceAddressKHR(
        VkDevice                                           device,
        const VkAccelerationStructureDeviceAddressInfoKHR* pInfo
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetAccelerationStructureDeviceAddressKHR(device, pInfo);
    }

    void vkCmdWriteAccelerationStructuresPropertiesKHR(
        VkCommandBuffer                   commandBuffer,
        uint32_t                          accelerationStructureCount,
        const VkAccelerationStructureKHR* pAccelerationStructures,
        VkQueryType                       queryType,
        VkQueryPool                       queryPool,
        uint32_t                          firstQuery
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
    }

    void vkGetDeviceAccelerationStructureCompatibilityKHR(
        VkDevice                                     device,
        const VkAccelerationStructureVersionInfoKHR* pVersionInfo,
        VkAccelerationStructureCompatibilityKHR*     pCompatibility
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetDeviceAccelerationStructureCompatibilityKHR(device, pVersionInfo, pCompatibility);
    }

    void vkGetAccelerationStructureBuildSizesKHR(
        VkDevice                                           device,
        VkAccelerationStructureBuildTypeKHR                buildType,
        const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
        const uint32_t*                                    pMaxPrimitiveCounts,
        VkAccelerationStructureBuildSizesInfoKHR*          pSizeInfo
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetAccelerationStructureBuildSizesKHR(device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);
    }

    void vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth)
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdTraceRaysKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
    }

    VkResult vkCreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateRayTracingPipelinesKHR(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    }

    VkResult vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData)
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
    }

    void vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress)
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdTraceRaysIndirectKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, indirectDeviceAddress);
    }

    VkDeviceSize vkGetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline pipeline, uint32_t group, VkShaderGroupShaderKHR groupShader)
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetRayTracingShaderGroupStackSizeKHR(device, pipeline, group, groupShader);
    }

    void vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize)
    {
    }

    void vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties)
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
    }

    VkResult vkGetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData)
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
    }
    /*********************************************************************************************************************/

    VkDeviceAddress vkGetBufferDeviceAddress(
        VkDevice                         device,
        const VkBufferDeviceAddressInfo* pInfo
    )
    {
        // 目前vk1.2才加入，先用khr，参数都一样，后面改吧
        return vkfunc::vkGetBufferDeviceAddressKHR(device, pInfo);
    }

    void vkGetDescriptorSetLayoutSupport(
        VkDevice                                    device,
        const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
        VkDescriptorSetLayoutSupport* pSupport)
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetDescriptorSetLayoutSupport(device, pCreateInfo, pSupport);
    }

    VkResult vkGetPhysicalDeviceSurfaceSupportKHR(
        VkPhysicalDevice physicalDevice,
        uint32_t         queueFamilyIndex,
        VkSurfaceKHR     surface,
        VkBool32*        pSupported
    )
    {
        return vkfunc::vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
    }

    VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(
        VkPhysicalDevice  physicalDevice,
        VkSurfaceKHR      surface,
        uint32_t*         pPresentModeCount,
        VkPresentModeKHR* pPresentModes
    )
    {
        return vkfunc::vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, pPresentModes);
    }


    VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        VkPhysicalDevice          physicalDevice,
        VkSurfaceKHR              surface,
        VkSurfaceCapabilitiesKHR* pSurfaceCapabilities
    )
    {
        return vkfunc::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
    }


    VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(
        VkPhysicalDevice    physicalDevice,
        VkSurfaceKHR        surface,
        uint32_t*           pSurfaceFormatCount,
        VkSurfaceFormatKHR* pSurfaceFormats
    )
    {
        return vkfunc::vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
    }

    VkResult vkCreateSwapchainKHR(
        VkDevice                        device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*    pAllocator,
        VkSwapchainKHR*                 pSwapchain
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }

    VkResult vkGetSwapchainImagesKHR(
        VkDevice       device,
        VkSwapchainKHR swapchain,
        uint32_t*      pSwapchainImageCount,
        VkImage*       pSwapchainImages
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
    }

    VkResult vkAcquireNextImageKHR(
        VkDevice       device,
        VkSwapchainKHR swapchain,
        uint64_t       timeout,
        VkSemaphore    semaphore,
        VkFence        fence,
        uint32_t*      pImageIndex
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    }

    VkResult vkQueuePresentKHR(
        VkQueue                 queue,
        const VkPresentInfoKHR* pPresentInfo
    )
    {
        OPTICK_ELAPSE_EVENT();
        return vkfunc::vkQueuePresentKHR(queue, pPresentInfo);
    }

    void vkDestroySwapchainKHR(
        VkDevice                     device,
        VkSwapchainKHR               swapchain,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkDestroySwapchainKHR(device, swapchain, pAllocator);
    }

    VkResult vkCreateInstance(
        const VkInstanceCreateInfo*  pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkInstance*                  pInstance
    )
    {
        return vkfunc::vkCreateInstance(pCreateInfo, pAllocator, pInstance);
    }

    VkResult vkEnumeratePhysicalDevices(
        VkInstance        instance,
        uint32_t*         pPhysicalDeviceCount,
        VkPhysicalDevice* pPhysicalDevices
    )
    {
        return vkfunc::vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    }

    void vkDestroyInstance(
        VkInstance                   instance,
        const VkAllocationCallbacks* pAllocator
    )
    {
        vkfunc::vkDestroyInstance(instance, pAllocator);
    }

    void vkDestroySurfaceKHR(
        VkInstance                   instance,
        VkSurfaceKHR                 surface,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroySurfaceKHR(instance, surface, pAllocator);
    }

    PFN_vkVoidFunction vkGetInstanceProcAddr(
        VkInstance  instance,
        const char* pName
    )
    {
        return vkfunc::vkGetInstanceProcAddr(instance, pName);
    }

    PFN_vkVoidFunction vkGetDeviceProcAddr(
        VkDevice    device,
        const char* pName
    )
    {
        return vkfunc::vkGetDeviceProcAddr(device, pName);
    }

    void vkGetPhysicalDeviceFeatures(
        VkPhysicalDevice          physicalDevice,
        VkPhysicalDeviceFeatures* pFeatures
    )
    {
        vkfunc::vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures);
    }

    BOOL vkGetPhysicalDeviceFeatures2KHR(
        VkPhysicalDevice           physicalDevice,
        VkPhysicalDeviceFeatures2* pFeatures
    )
    {
        if (vkfunc::vkGetPhysicalDeviceFeatures2KHR)
        {
            vkfunc::vkGetPhysicalDeviceFeatures2KHR(physicalDevice, pFeatures);
            return true;
        }
        else
        {
            return false;
        }
    }

    void vkGetPhysicalDeviceFormatProperties(
        VkPhysicalDevice    physicalDevice,
        VkFormat            format,
        VkFormatProperties* pFormatProperties
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceFormatProperties(physicalDevice, format, pFormatProperties);
    }

    VkResult vkGetPhysicalDeviceImageFormatProperties(
        VkPhysicalDevice         physicalDevice,
        VkFormat                 format,
        VkImageType              type,
        VkImageTiling            tiling,
        VkImageUsageFlags        usage,
        VkImageCreateFlags       flags,
        VkImageFormatProperties* pImageFormatProperties
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetPhysicalDeviceImageFormatProperties(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
    }

    void vkGetPhysicalDeviceProperties(
        VkPhysicalDevice            physicalDevice,
        VkPhysicalDeviceProperties* pProperties
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    }

    void vkGetPhysicalDeviceProperties2KHR(
        VkPhysicalDevice             physicalDevice,
        VkPhysicalDeviceProperties2* pProperties
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceProperties2KHR(physicalDevice, pProperties);
    }

    void vkGetPhysicalDeviceQueueFamilyProperties(
        VkPhysicalDevice         physicalDevice,
        uint32_t*                pQueueFamilyPropertyCount,
        VkQueueFamilyProperties* pQueueFamilyProperties
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
    }

    void vkGetPhysicalDeviceMemoryProperties(
        VkPhysicalDevice                  physicalDevice,
        VkPhysicalDeviceMemoryProperties* pMemoryProperties
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
    }


    void vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceMemoryProperties2KHR(physicalDevice, pMemoryProperties);
    }


    VkResult vkCreateDevice(
        VkPhysicalDevice             physicalDevice,
        const VkDeviceCreateInfo*    pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice*                    pDevice
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    }

    void vkDestroyDevice(
        VkDevice                     device,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyDevice(device, pAllocator);
    }

    VkResult vkEnumerateInstanceExtensionProperties(
        const char*            pLayerName,
        uint32_t*              pPropertyCount,
        VkExtensionProperties* pProperties
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
    }

    VkResult vkEnumerateDeviceExtensionProperties(
        VkPhysicalDevice       physicalDevice,
        const char*            pLayerName,
        uint32_t*              pPropertyCount,
        VkExtensionProperties* pProperties
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
    }

    VkResult vkEnumerateInstanceLayerProperties(
        uint32_t*          pPropertyCount,
        VkLayerProperties* pProperties
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult vkEnumerateDeviceLayerProperties(
        VkPhysicalDevice   physicalDevice,
        uint32_t*          pPropertyCount,
        VkLayerProperties* pProperties
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkEnumerateDeviceLayerProperties(physicalDevice, pPropertyCount, pProperties);
    }

    void vkGetDeviceQueue(
        VkDevice device,
        uint32_t queueFamilyIndex,
        uint32_t queueIndex,
        VkQueue* pQueue
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
    }

    VkResult vkQueueSubmit(
        VkQueue             queue,
        uint32_t            submitCount,
        const VkSubmitInfo* pSubmits,
        VkFence             fence
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkQueueSubmit(queue, submitCount, pSubmits, fence);
    }

    VkResult vkQueueWaitIdle(
        VkQueue queue
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkQueueWaitIdle(queue);
    }

    VkResult vkDeviceWaitIdle(
        VkDevice device
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkDeviceWaitIdle(device);
    }

    std::unordered_map<KVkDeviceMemory, VkDeviceSize> _g_mapVkAllocMemory2Size;
    std::mutex                                        _g_mapVkAllocMemory2Size_lock;

    VkResult vkAllocateMemory(
        VkDevice                     device,
        const VkMemoryAllocateInfo*  pAllocateInfo,
        const VkAllocationCallbacks* pAllocator,
        KVkDeviceMemory*             pMemory
    )
    {
        PROF_CPU();

        KVkDeviceMemory_T* pM       = nullptr;
        VkDeviceMemory     pMem     = nullptr;
        VkResult           vkResult = vkfunc::vkAllocateMemory(device, pAllocateInfo, pAllocator, &pMem);
        // ASSERT(vkResult == VK_SUCCESS);

        if (vkResult == VK_SUCCESS)
        {
            pM = new KVkDeviceMemory_T;
            pM->Lock();

            pM->m_pMem     = pMem;
            pM->m_threadid = GetThreadId();
            *pMemory       = pM;

            _g_mapVkAllocMemory2Size_lock.lock();
            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            ++pPerfMonitor->nVkAllocMemoryCount;
            auto itRet = _g_mapVkAllocMemory2Size.insert(std::make_pair(pM, pAllocateInfo->allocationSize));
            ASSERT(itRet.second);
            pPerfMonitor->nVkAllocMemorySize += (int32_t)pAllocateInfo->allocationSize;
            _g_mapVkAllocMemory2Size_lock.unlock();
            pM->Unlock();
        }
        else
        {
            // 走vma可能在某个堆上分配失败，会尝试从另一个堆上分配
        }

        return vkResult;
    }

    void vkFreeMemory(
        VkDevice                     device,
        KVkDeviceMemory              pMemory,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU();
        // if (NSEngine::GetEngineOptions()->bMultiThreadRender)
        //{
        pMemory->Lock();
        //}
        vkfunc::vkFreeMemory(device, pMemory->m_pMem, pAllocator);

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->nVkAllocMemoryCount;

        _g_mapVkAllocMemory2Size_lock.lock();
        auto itFind = _g_mapVkAllocMemory2Size.find(pMemory);
        ASSERT(itFind != _g_mapVkAllocMemory2Size.end());
        pPerfMonitor->nVkAllocMemorySize -= (int32_t)itFind->second;
        _g_mapVkAllocMemory2Size.erase(itFind);
        _g_mapVkAllocMemory2Size_lock.unlock();


        pMemory->Unlock();

        delete (pMemory);
    }

    // #include <assert.h>

    VkResult vkMapMemory(
        VkDevice         device,
        KVkDeviceMemory  pMemory,
        VkDeviceSize     offset,
        VkDeviceSize     size,
        VkMemoryMapFlags flags,
        void**           ppData
    )
    {
        VkResult result = VK_INCOMPLETE;
        PROF_CPU_DEEP();
        // if (NSEngine::GetEngineOptions()->bMultiThreadRender)
        //{
        // assert(!pMemory->m_bLocked);
        pMemory->Lock();
        //
        // printf("%p:%u:%d lock %d \r\n", pMemory->m_pMem, d, offset, GetThreadId());

        //}

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->nVkMapMemoryCount;

        if (pMemory->m_mappedCount != 0)
        {
            ++pMemory->m_mappedCount;
            *ppData = (uint8_t*)pMemory->m_pMappedData + offset;
            result  = VK_SUCCESS;
        }
        else
        {
            result = vkfunc::vkMapMemory(device, pMemory->m_pMem, 0, VK_WHOLE_SIZE, flags, &pMemory->m_pMappedData);

            // uint64_t d = (uint64_t)pMemory->m_pMem;
            // printf("%p:%u:%d mapped %d \r\n", pMemory->m_pMem, d, offset, GetThreadId());

            *ppData = (uint8_t*)pMemory->m_pMappedData + offset;
            if (result == VK_SUCCESS)
            {
                ++pMemory->m_mappedCount;
            }
        }

        pMemory->Unlock();

        // return vkfunc::vkMapMemory(device, pMemory->m_pMem, offset, size, flags, ppData);
        return result;
    }

    void vkUnmapMemory(
        VkDevice        device,
        KVkDeviceMemory pMemory
    )
    {
        PROF_CPU_DEEP();

        ASSERT(pMemory);
        if (pMemory)
        {
            pMemory->Lock();

            if (pMemory->m_mappedCount)
            {
                --pMemory->m_mappedCount;
                if (pMemory->m_mappedCount == 0)
                {
                    pMemory->m_pMappedData = nullptr;
                    vkfunc::vkUnmapMemory(device, pMemory->m_pMem);

                    // uint64_t d = (uint64_t)pMemory->m_pMem;
                    // printf("%p:%u unmapped %d \r\n", pMemory->m_pMem, d, GetThreadId());
                }
            }

            pMemory->Unlock();

            KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
            --pPerfMonitor->nVkMapMemoryCount;
        }
    }

    VkResult vkFlushMappedMemoryRanges(
        VkDevice                   device,
        uint32_t                   memoryRangeCount,
        const VkMappedMemoryRange* pMemoryRanges
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkFlushMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
    }

    VkResult vkInvalidateMappedMemoryRanges(
        VkDevice                   device,
        uint32_t                   memoryRangeCount,
        const VkMappedMemoryRange* pMemoryRanges
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkInvalidateMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
    }

    void vkGetDeviceMemoryCommitment(
        VkDevice        device,
        KVkDeviceMemory memory,
        VkDeviceSize*   pCommittedMemoryInBytes
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetDeviceMemoryCommitment(device, memory->m_pMem, pCommittedMemoryInBytes);
    }

    VkResult vkBindBufferMemory(
        VkDevice        device,
        VkBuffer        buffer,
        KVkDeviceMemory memory,
        VkDeviceSize    memoryOffset
    )
    {
        VkResult result = VK_INCOMPLETE;
        PROF_CPU_DEEP();
        memory->Lock();
        result = vkfunc::vkBindBufferMemory(device, buffer, memory->m_pMem, memoryOffset);
        memory->Unlock();
        return result;
    }

    VkResult vkBindImageMemory(
        VkDevice        device,
        VkImage         image,
        KVkDeviceMemory memory,
        VkDeviceSize    memoryOffset
    )
    {
        VkResult result = VK_INCOMPLETE;
        PROF_CPU_DEEP();
        memory->Lock();
        result = vkfunc::vkBindImageMemory(device, image, memory->m_pMem, memoryOffset);
        memory->Unlock();
        return result;
    }

    void vkGetBufferMemoryRequirements(
        VkDevice              device,
        VkBuffer              buffer,
        VkMemoryRequirements* pMemoryRequirements
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
    }

    void vkGetImageMemoryRequirements(
        VkDevice              device,
        VkImage               image,
        VkMemoryRequirements* pMemoryRequirements
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetImageMemoryRequirements(device, image, pMemoryRequirements);
    }

    void vkGetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetBufferMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
    }

    void vkGetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetImageMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
    }

    void vkGetImageSparseMemoryRequirements(
        VkDevice                         device,
        VkImage                          image,
        uint32_t*                        pSparseMemoryRequirementCount,
        VkSparseImageMemoryRequirements* pSparseMemoryRequirements
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetImageSparseMemoryRequirements(device, image, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
    }

    void vkGetPhysicalDeviceSparseImageFormatProperties(
        VkPhysicalDevice               physicalDevice,
        VkFormat                       format,
        VkImageType                    type,
        VkSampleCountFlagBits          samples,
        VkImageUsageFlags              usage,
        VkImageTiling                  tiling,
        uint32_t*                      pPropertyCount,
        VkSparseImageFormatProperties* pProperties
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetPhysicalDeviceSparseImageFormatProperties(physicalDevice, format, type, samples, usage, tiling, pPropertyCount, pProperties);
    }

    VkResult vkQueueBindSparse(
        VkQueue                 queue,
        uint32_t                bindInfoCount,
        const VkBindSparseInfo* pBindInfo,
        VkFence                 fence
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkQueueBindSparse(queue, bindInfoCount, pBindInfo, fence);
    }

    VkResult vkCreateFence(
        VkDevice                     device,
        const VkFenceCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkFence*                     pFence
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateFence(device, pCreateInfo, pAllocator, pFence);
    }

    void vkDestroyFence(
        VkDevice                     device,
        VkFence                      fence,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkDestroyFence(device, fence, pAllocator);
    }

    VkResult vkResetFences(
        VkDevice       device,
        uint32_t       fenceCount,
        const VkFence* pFences
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkResetFences(device, fenceCount, pFences);
    }

    VkResult vkGetFenceStatus(
        VkDevice device,
        VkFence  fence
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetFenceStatus(device, fence);
    }

    VkResult vkWaitForFences(
        VkDevice       device,
        uint32_t       fenceCount,
        const VkFence* pFences,
        VkBool32       waitAll,
        uint64_t       timeout
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkWaitForFences(device, fenceCount, pFences, waitAll, timeout);
    }

    VkResult vkCreateSemaphore(
        VkDevice                     device,
        const VkSemaphoreCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSemaphore*                 pSemaphore
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateSemaphore(device, pCreateInfo, pAllocator, pSemaphore);
    }

    void vkDestroySemaphore(
        VkDevice                     device,
        VkSemaphore                  semaphore,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroySemaphore(device, semaphore, pAllocator);
    }

    VkResult vkCreateEvent(
        VkDevice                     device,
        const VkEventCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkEvent*                     pEvent
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateEvent(device, pCreateInfo, pAllocator, pEvent);
    }

    void vkDestroyEvent(
        VkDevice                     device,
        VkEvent                      event,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyEvent(device, event, pAllocator);
    }

    VkResult vkGetEventStatus(
        VkDevice device,
        VkEvent  event
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetEventStatus(device, event);
    }

    VkResult vkSetEvent(
        VkDevice device,
        VkEvent  event
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkSetEvent(device, event);
    }

    VkResult vkResetEvent(
        VkDevice device,
        VkEvent  event
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkResetEvent(device, event);
    }

    VkResult vkCreateQueryPool(
        VkDevice                     device,
        const VkQueryPoolCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkQueryPool*                 pQueryPool
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateQueryPool(device, pCreateInfo, pAllocator, pQueryPool);
    }

    void vkDestroyQueryPool(
        VkDevice                     device,
        VkQueryPool                  queryPool,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyQueryPool(device, queryPool, pAllocator);
    }

    VkResult vkGetQueryPoolResults(
        VkDevice           device,
        VkQueryPool        queryPool,
        uint32_t           firstQuery,
        uint32_t           queryCount,
        size_t             dataSize,
        void*              pData,
        VkDeviceSize       stride,
        VkQueryResultFlags flags
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetQueryPoolResults(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
    }

    std::unordered_map<VkBuffer, VkDeviceSize> _g_mapVkAllocBuffer2Size;
    std::mutex                                 _g_mapVkAllocBuffer2Size_lock;
    VkResult                                   vkCreateBuffer(
                                          VkDevice                     device,
                                          const VkBufferCreateInfo*    pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkBuffer*                    pBuffer
                                      )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();

        VkResult vkResult = vkfunc::vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
        ASSERT(vkResult == VK_SUCCESS);

        _g_mapVkAllocBuffer2Size_lock.lock();
        ++pPerfMonitor->m_sVkBuffer.nVkBufferCount;
        pPerfMonitor->m_sVkBuffer.nVkAllocBufferSize += (int32_t)pCreateInfo->size;
        auto itRet                                    = _g_mapVkAllocBuffer2Size.insert(std::make_pair(*pBuffer, pCreateInfo->size));
        ASSERT(itRet.second);
        _g_mapVkAllocBuffer2Size_lock.unlock();

        return vkResult;
    }

    void vkDestroyBuffer(
        VkDevice                     device,
        VkBuffer                     buffer,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        _g_mapVkAllocBuffer2Size_lock.lock();
        --pPerfMonitor->m_sVkBuffer.nVkBufferCount;
        auto itFind = _g_mapVkAllocBuffer2Size.find(buffer);
        ASSERT(itFind != _g_mapVkAllocBuffer2Size.end());
        pPerfMonitor->m_sVkBuffer.nVkAllocBufferSize -= (int32_t)itFind->second;
        _g_mapVkAllocBuffer2Size.erase(itFind);
        _g_mapVkAllocBuffer2Size_lock.unlock();

        return vkfunc::vkDestroyBuffer(device, buffer, pAllocator);
    }

    VkResult vkCreateBufferView(
        VkDevice                      device,
        const VkBufferViewCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*  pAllocator,
        VkBufferView*                 pView
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateBufferView(device, pCreateInfo, pAllocator, pView);
    }

    void vkDestroyBufferView(
        VkDevice                     device,
        VkBufferView                 bufferView,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkDestroyBufferView(device, bufferView, pAllocator);
    }

    VkResult vkCreateImage(
        VkDevice                     device,
        const VkImageCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkImage*                     pImage
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->m_sVkImage.nVkImageCount;

        return vkfunc::vkCreateImage(device, pCreateInfo, pAllocator, pImage);
    }

    void vkDestroyImage(
        VkDevice                     device,
        VkImage                      image,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->m_sVkImage.nVkImageCount;

        vkfunc::vkDestroyImage(device, image, pAllocator);
    }

    void vkGetImageSubresourceLayout(
        VkDevice                  device,
        VkImage                   image,
        const VkImageSubresource* pSubresource,
        VkSubresourceLayout*      pLayout
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetImageSubresourceLayout(device, image, pSubresource, pLayout);
    }

    VkResult vkCreateImageView(
        VkDevice                     device,
        const VkImageViewCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkImageView*                 pView
    )
    {
        PROF_CPU_DEEP();
        VkResult result = vkfunc::vkCreateImageView(device, pCreateInfo, pAllocator, pView);
        return result;
    }

    void vkDestroyImageView(
        VkDevice                     device,
        VkImageView                  imageView,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkDestroyImageView(device, imageView, pAllocator);
    }

    VkResult vkCreateShaderModule(
        VkDevice                        device,
        const VkShaderModuleCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*    pAllocator,
        VkShaderModule*                 pShaderModule
    )
    {
        PROF_CPU_DEEP();
        VkResult eResult = vkfunc::vkCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
        if (eResult == VK_SUCCESS)
        {
            KX3DEngineMonitor* pEngineMonitor = NSEngine::GetEngineMonitor();
            ++pEngineMonitor->m_sGraphics.nVkShaderModule;
        }
        return eResult;
    }

    void vkDestroyShaderModule(
        VkDevice                     device,
        VkShaderModule               shaderModule,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyShaderModule(device, shaderModule, pAllocator);

        KX3DEngineMonitor* pEngineMonitor = NSEngine::GetEngineMonitor();
        --pEngineMonitor->m_sGraphics.nVkShaderModule;
    }

    VkResult vkCreatePipelineCache(
        VkDevice                         device,
        const VkPipelineCacheCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*     pAllocator,
        VkPipelineCache*                 pPipelineCache
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreatePipelineCache(device, pCreateInfo, pAllocator, pPipelineCache);
    }

    void vkDestroyPipelineCache(
        VkDevice                     device,
        VkPipelineCache              pipelineCache,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyPipelineCache(device, pipelineCache, pAllocator);
    }

    VkResult vkGetPipelineCacheData(
        VkDevice        device,
        VkPipelineCache pipelineCache,
        size_t*         pDataSize,
        void*           pData
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkGetPipelineCacheData(device, pipelineCache, pDataSize, pData);
    }

    VkResult vkMergePipelineCaches(
        VkDevice               device,
        VkPipelineCache        dstCache,
        uint32_t               srcCacheCount,
        const VkPipelineCache* pSrcCaches
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkMergePipelineCaches(device, dstCache, srcCacheCount, pSrcCaches);
    }

    VkResult vkCreateGraphicsPipelines(
        VkDevice                            device,
        VkPipelineCache                     pipelineCache,
        uint32_t                            createInfoCount,
        const VkGraphicsPipelineCreateInfo* pCreateInfos,
        const VkAllocationCallbacks*        pAllocator,
        VkPipeline*                         pPipelines
    )
    {
        PROF_CPU_DEEP();

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->m_sGraphics.nVkPipelineCount;

        return vkfunc::vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    }

    VkResult vkCreateComputePipelines(
        VkDevice                           device,
        VkPipelineCache                    pipelineCache,
        uint32_t                           createInfoCount,
        const VkComputePipelineCreateInfo* pCreateInfos,
        const VkAllocationCallbacks*       pAllocator,
        VkPipeline*                        pPipelines
    )
    {
        PROF_CPU_DEEP();

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->m_sGraphics.nVkPipelineCount;

        return vkfunc::vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    }

    void vkDestroyPipeline(
        VkDevice                     device,
        VkPipeline                   pipeline,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();

        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->m_sGraphics.nVkPipelineCount;

        return vkfunc::vkDestroyPipeline(device, pipeline, pAllocator);
    }

    VkResult vkCreatePipelineLayout(
        VkDevice                          device,
        const VkPipelineLayoutCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*      pAllocator,
        VkPipelineLayout*                 pPipelineLayout
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
    }

    void vkDestroyPipelineLayout(
        VkDevice                     device,
        VkPipelineLayout             pipelineLayout,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyPipelineLayout(device, pipelineLayout, pAllocator);
    }

    std::map<uint64_t, VkSampler> samplerCache;
    VkResult                      vkCreateSampler(
                             VkDevice                     device,
                             const VkSamplerCreateInfo*   pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkSampler*                   pSampler
                         )
    {
        PROF_CPU_DEEP();
        // return vkfunc::vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
        VkResult ret = VK_INCOMPLETE;

        uint64_t hash = KSTR_HELPER::GetHashCodeForMem64Bit((char*)pCreateInfo, sizeof(VkSamplerCreateInfo));
        auto     it   = samplerCache.find(hash);
        if (it != samplerCache.end())
        {
            *pSampler = it->second;
            ret       = VK_SUCCESS;
        }
        else
        {
            ret = vkfunc::vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
            samplerCache.insert(std::make_pair<>(hash, *pSampler));
        }
        // detected device memory lack debug point here;
        return ret;
    }

    void vkDestroySampler(
        VkDevice                     device,
        VkSampler                    sampler,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        // move to vkDestroySamplers on destory device
        // vkfunc::vkDestroySampler(device, sampler, pAllocator);
    }

    void vkDestroySamplers(VkDevice device)
    {
        PROF_CPU_DEEP();
        for (auto it : samplerCache)
        {
            VkSampler pSampler = it.second;
            vkfunc::vkDestroySampler(device, pSampler, nullptr);
        }
        samplerCache.clear();
    }

    VkResult vkCreateDescriptorSetLayout(
        VkDevice                               device,
        const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*           pAllocator,
        VkDescriptorSetLayout*                 pSetLayout
    )
    {
        PROF_CPU_DEEP();
        VkResult result = vkfunc::vkCreateDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
        return result;
    }

    void vkDestroyDescriptorSetLayout(
        VkDevice                     device,
        VkDescriptorSetLayout        descriptorSetLayout,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyDescriptorSetLayout(device, descriptorSetLayout, pAllocator);
    }

    VkResult vkCreateDescriptorPool(
        VkDevice                          device,
        const VkDescriptorPoolCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*      pAllocator,
        VkDescriptorPool*                 pDescriptorPool
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
        ++pMonitor->m_sGraphics.nVkDescriptorPoolCount;
        return vkfunc::vkCreateDescriptorPool(device, pCreateInfo, pAllocator, pDescriptorPool);
    }

    void vkDestroyDescriptorPool(
        VkDevice                     device,
        VkDescriptorPool             descriptorPool,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
        --pMonitor->m_sGraphics.nVkDescriptorPoolCount;
        return vkfunc::vkDestroyDescriptorPool(device, descriptorPool, pAllocator);
    }

    VkResult vkResetDescriptorPool(
        VkDevice                   device,
        VkDescriptorPool           descriptorPool,
        VkDescriptorPoolResetFlags flags
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkResetDescriptorPool(device, descriptorPool, flags);
    }

    VkResult vkAllocateDescriptorSets(
        VkDevice                           device,
        const VkDescriptorSetAllocateInfo* pAllocateInfo,
        VkDescriptorSet*                   pDescriptorSets
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
        ++pMonitor->m_sGraphics.nVkDescriptorSetCount;
        return vkfunc::vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
    }

    VkResult vkFreeDescriptorSets(
        VkDevice               device,
        VkDescriptorPool       descriptorPool,
        uint32_t               descriptorSetCount,
        const VkDescriptorSet* pDescriptorSets
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pMonitor = NSEngine::GetEngineMonitor();
        --pMonitor->m_sGraphics.nVkDescriptorSetCount;
        return vkfunc::vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
    }

    void vkUpdateDescriptorSets(
        VkDevice                    device,
        uint32_t                    descriptorWriteCount,
        const VkWriteDescriptorSet* pDescriptorWrites,
        uint32_t                    descriptorCopyCount,
        const VkCopyDescriptorSet*  pDescriptorCopies
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
    }

    VkResult vkCreateFramebuffer(
        VkDevice                       device,
        const VkFramebufferCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*   pAllocator,
        VkFramebuffer*                 pFramebuffer
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        ++pPerfMonitor->m_sGraphics.nVkFrameBufferCount;

        return vkfunc::vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    }

    void vkDestroyFramebuffer(
        VkDevice                     device,
        VkFramebuffer                framebuffer,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        KX3DEngineMonitor* pPerfMonitor = NSEngine::GetEngineMonitor();
        --pPerfMonitor->m_sGraphics.nVkFrameBufferCount;

        vkfunc::vkDestroyFramebuffer(device, framebuffer, pAllocator);
    }

    VkResult vkCreateRenderPass(
        VkDevice                      device,
        const VkRenderPassCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*  pAllocator,
        VkRenderPass*                 pRenderPass
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    }

    void vkDestroyRenderPass(
        VkDevice                     device,
        VkRenderPass                 renderPass,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyRenderPass(device, renderPass, pAllocator);
    }

    void vkGetRenderAreaGranularity(
        VkDevice     device,
        VkRenderPass renderPass,
        VkExtent2D*  pGranularity
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkGetRenderAreaGranularity(device, renderPass, pGranularity);
    }

    VkResult vkCreateCommandPool(
        VkDevice                       device,
        const VkCommandPoolCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*   pAllocator,
        VkCommandPool*                 pCommandPool
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
    }

    void vkDestroyCommandPool(
        VkDevice                     device,
        VkCommandPool                commandPool,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyCommandPool(device, commandPool, pAllocator);
    }

    VkResult vkResetCommandPool(
        VkDevice                device,
        VkCommandPool           commandPool,
        VkCommandPoolResetFlags flags
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkResetCommandPool(device, commandPool, flags);
    }

    void vkTrimCommandPool(
        VkDevice               device,
        VkCommandPool          commandPool,
        VkCommandPoolTrimFlags flags
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkTrimCommandPool(device, commandPool, flags);
    }

    VkResult vkAllocateCommandBuffers(
        VkDevice                           device,
        const VkCommandBufferAllocateInfo* pAllocateInfo,
        VkCommandBuffer*                   pCommandBuffers
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
    }

    void vkFreeCommandBuffers(
        VkDevice               device,
        VkCommandPool          commandPool,
        uint32_t               commandBufferCount,
        const VkCommandBuffer* pCommandBuffers
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
    }

    VkResult vkBeginCommandBuffer(
        VkCommandBuffer                 commandBuffer,
        const VkCommandBufferBeginInfo* pBeginInfo
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkBeginCommandBuffer(commandBuffer, pBeginInfo);
    }

    VkResult vkEndCommandBuffer(
        VkCommandBuffer commandBuffer
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkEndCommandBuffer(commandBuffer);
    }

    VkResult vkResetCommandBuffer(
        VkCommandBuffer           commandBuffer,
        VkCommandBufferResetFlags flags
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkResetCommandBuffer(commandBuffer, flags);
    }

    void vkCmdBindPipeline(
        VkCommandBuffer     commandBuffer,
        VkPipelineBindPoint pipelineBindPoint,
        VkPipeline          pipeline
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
    }

    void vkCmdSetViewport(
        VkCommandBuffer   commandBuffer,
        uint32_t          firstViewport,
        uint32_t          viewportCount,
        const VkViewport* pViewports
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
    }

    void vkCmdSetScissor(
        VkCommandBuffer commandBuffer,
        uint32_t        firstScissor,
        uint32_t        scissorCount,
        const VkRect2D* pScissors
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
    }

    void vkCmdSetLineWidth(
        VkCommandBuffer commandBuffer,
        float           lineWidth
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetLineWidth(commandBuffer, lineWidth);
    }

    void vkCmdSetDepthBias(
        VkCommandBuffer commandBuffer,
        float           depthBiasConstantFactor,
        float           depthBiasClamp,
        float           depthBiasSlopeFactor
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetDepthBias(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
    }

    void vkCmdSetBlendConstants(
        VkCommandBuffer commandBuffer,
        const float     blendConstants[4]
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetBlendConstants(commandBuffer, blendConstants);
    }

    void vkCmdSetDepthBounds(
        VkCommandBuffer commandBuffer,
        float           minDepthBounds,
        float           maxDepthBounds
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetDepthBounds(commandBuffer, minDepthBounds, maxDepthBounds);
    }

    void vkCmdSetStencilCompareMask(
        VkCommandBuffer    commandBuffer,
        VkStencilFaceFlags faceMask,
        uint32_t           compareMask
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetStencilCompareMask(commandBuffer, faceMask, compareMask);
    }

    void vkCmdSetStencilWriteMask(
        VkCommandBuffer    commandBuffer,
        VkStencilFaceFlags faceMask,
        uint32_t           writeMask
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetStencilWriteMask(commandBuffer, faceMask, writeMask);
    }

    void vkCmdSetStencilReference(
        VkCommandBuffer    commandBuffer,
        VkStencilFaceFlags faceMask,
        uint32_t           reference
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetStencilReference(commandBuffer, faceMask, reference);
    }

    void vkCmdBindDescriptorSets(
        VkCommandBuffer        commandBuffer,
        VkPipelineBindPoint    pipelineBindPoint,
        VkPipelineLayout       layout,
        uint32_t               firstSet,
        uint32_t               descriptorSetCount,
        const VkDescriptorSet* pDescriptorSets,
        uint32_t               dynamicOffsetCount,
        const uint32_t*        pDynamicOffsets
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
    }

    void vkCmdBindIndexBuffer(
        VkCommandBuffer commandBuffer,
        VkBuffer        buffer,
        VkDeviceSize    offset,
        VkIndexType     indexType
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
    }

    void vkCmdBindVertexBuffers(
        VkCommandBuffer     commandBuffer,
        uint32_t            firstBinding,
        uint32_t            bindingCount,
        const VkBuffer*     pBuffers,
        const VkDeviceSize* pOffsets
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
    }

    void vkCmdDraw(
        VkCommandBuffer commandBuffer,
        uint32_t        vertexCount,
        uint32_t        instanceCount,
        uint32_t        firstVertex,
        uint32_t        firstInstance
    )
    {
        PROF_CPU_DEEP();
        KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
        vkfunc::vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        pEnginePerformace->dwDrawCallCount++;
        pEnginePerformace->dwDrawFacesindics += vertexCount * instanceCount;
    }

    void vkCmdDrawIndexed(
        VkCommandBuffer commandBuffer,
        uint32_t        indexCount,
        uint32_t        instanceCount,
        uint32_t        firstIndex,
        int32_t         vertexOffset,
        uint32_t        firstInstance
    )
    {
        PROF_CPU_DEEP();
        KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
        vkfunc::vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        pEnginePerformace->dwDrawCallCount++;
        pEnginePerformace->dwDrawFacesindics += indexCount * instanceCount;
    }

    void vkCmdDrawIndirect(
        VkCommandBuffer commandBuffer,
        VkBuffer        buffer,
        VkDeviceSize    offset,
        uint32_t        drawCount,
        uint32_t        stride
    )
    {
        PROF_CPU_DEEP();
        KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
        vkfunc::vkCmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
        pEnginePerformace->dwDrawCallCount++;
    }

    void vkCmdDrawIndexedIndirect(
        VkCommandBuffer commandBuffer,
        VkBuffer        buffer,
        VkDeviceSize    offset,
        uint32_t        drawCount,
        uint32_t        stride
    )
    {
        PROF_CPU_DEEP();
        KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
        vkfunc::vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
        pEnginePerformace->dwDrawCallCount++;
    }

    void vkCmdDispatch(
        VkCommandBuffer commandBuffer,
        uint32_t        groupCountX,
        uint32_t        groupCountY,
        uint32_t        groupCountZ
    )
    {
        PROF_CPU_DEEP();
        KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
        vkfunc::vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
        pEnginePerformace->dwComputeCallCount++;
    }

    void vkCmdDispatchIndirect(
        VkCommandBuffer commandBuffer,
        VkBuffer        buffer,
        VkDeviceSize    offset
    )
    {
        PROF_CPU_DEEP();
        KEnginePerformance* pEnginePerformace = NSEngine::GetEnginePerformance();
        vkfunc::vkCmdDispatchIndirect(commandBuffer, buffer, offset);
        pEnginePerformace->dwComputeCallCount++;
    }

    void vkCmdCopyBuffer(
        VkCommandBuffer     commandBuffer,
        VkBuffer            srcBuffer,
        VkBuffer            dstBuffer,
        uint32_t            regionCount,
        const VkBufferCopy* pRegions
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
    }

    void vkCmdCopyImage(
        VkCommandBuffer    commandBuffer,
        VkImage            srcImage,
        VkImageLayout      srcImageLayout,
        VkImage            dstImage,
        VkImageLayout      dstImageLayout,
        uint32_t           regionCount,
        const VkImageCopy* pRegions
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
    }

    void vkCmdBlitImage(
        VkCommandBuffer    commandBuffer,
        VkImage            srcImage,
        VkImageLayout      srcImageLayout,
        VkImage            dstImage,
        VkImageLayout      dstImageLayout,
        uint32_t           regionCount,
        const VkImageBlit* pRegions,
        VkFilter           filter
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
    }

    void vkCmdCopyBufferToImage(
        VkCommandBuffer          commandBuffer,
        VkBuffer                 srcBuffer,
        VkImage                  dstImage,
        VkImageLayout            dstImageLayout,
        uint32_t                 regionCount,
        const VkBufferImageCopy* pRegions
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
    }

    void vkCmdCopyImageToBuffer(
        VkCommandBuffer          commandBuffer,
        VkImage                  srcImage,
        VkImageLayout            srcImageLayout,
        VkBuffer                 dstBuffer,
        uint32_t                 regionCount,
        const VkBufferImageCopy* pRegions
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
    }

    void vkCmdUpdateBuffer(
        VkCommandBuffer commandBuffer,
        VkBuffer        dstBuffer,
        VkDeviceSize    dstOffset,
        VkDeviceSize    dataSize,
        const void*     pData
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
    }

    void vkCmdFillBuffer(
        VkCommandBuffer commandBuffer,
        VkBuffer        dstBuffer,
        VkDeviceSize    dstOffset,
        VkDeviceSize    size,
        uint32_t        data
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
    }

    void vkCmdClearColorImage(
        VkCommandBuffer                commandBuffer,
        VkImage                        image,
        VkImageLayout                  imageLayout,
        const VkClearColorValue*       pColor,
        uint32_t                       rangeCount,
        const VkImageSubresourceRange* pRanges
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
    }

    void vkCmdClearDepthStencilImage(
        VkCommandBuffer                 commandBuffer,
        VkImage                         image,
        VkImageLayout                   imageLayout,
        const VkClearDepthStencilValue* pDepthStencil,
        uint32_t                        rangeCount,
        const VkImageSubresourceRange*  pRanges
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdClearDepthStencilImage(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
    }

    void vkCmdClearAttachments(
        VkCommandBuffer          commandBuffer,
        uint32_t                 attachmentCount,
        const VkClearAttachment* pAttachments,
        uint32_t                 rectCount,
        const VkClearRect*       pRects
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
    }

    void vkCmdResolveImage(
        VkCommandBuffer       commandBuffer,
        VkImage               srcImage,
        VkImageLayout         srcImageLayout,
        VkImage               dstImage,
        VkImageLayout         dstImageLayout,
        uint32_t              regionCount,
        const VkImageResolve* pRegions
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
    }

    void vkCmdSetEvent(
        VkCommandBuffer      commandBuffer,
        VkEvent              event,
        VkPipelineStageFlags stageMask
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdSetEvent(commandBuffer, event, stageMask);
    }

    void vkCmdResetEvent(
        VkCommandBuffer      commandBuffer,
        VkEvent              event,
        VkPipelineStageFlags stageMask
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdResetEvent(commandBuffer, event, stageMask);
    }

    void vkCmdWaitEvents(
        VkCommandBuffer              commandBuffer,
        uint32_t                     eventCount,
        const VkEvent*               pEvents,
        VkPipelineStageFlags         srcStageMask,
        VkPipelineStageFlags         dstStageMask,
        uint32_t                     memoryBarrierCount,
        const VkMemoryBarrier*       pMemoryBarriers,
        uint32_t                     bufferMemoryBarrierCount,
        const VkBufferMemoryBarrier* pBufferMemoryBarriers,
        uint32_t                     imageMemoryBarrierCount,
        const VkImageMemoryBarrier*  pImageMemoryBarriers
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdWaitEvents(commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
    }

    void vkCmdPipelineBarrier(
        VkCommandBuffer              commandBuffer,
        VkPipelineStageFlags         srcStageMask,
        VkPipelineStageFlags         dstStageMask,
        VkDependencyFlags            dependencyFlags,
        uint32_t                     memoryBarrierCount,
        const VkMemoryBarrier*       pMemoryBarriers,
        uint32_t                     bufferMemoryBarrierCount,
        const VkBufferMemoryBarrier* pBufferMemoryBarriers,
        uint32_t                     imageMemoryBarrierCount,
        const VkImageMemoryBarrier*  pImageMemoryBarriers
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
    }

    void vkCmdBeginQuery(
        VkCommandBuffer     commandBuffer,
        VkQueryPool         queryPool,
        uint32_t            query,
        VkQueryControlFlags flags
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdBeginQuery(commandBuffer, queryPool, query, flags);
    }

    void vkCmdEndQuery(
        VkCommandBuffer commandBuffer,
        VkQueryPool     queryPool,
        uint32_t        query
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdEndQuery(commandBuffer, queryPool, query);
    }

    void vkCmdResetQueryPool(
        VkCommandBuffer commandBuffer,
        VkQueryPool     queryPool,
        uint32_t        firstQuery,
        uint32_t        queryCount
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdResetQueryPool(commandBuffer, queryPool, firstQuery, queryCount);
    }

    void vkCmdWriteTimestamp(
        VkCommandBuffer         commandBuffer,
        VkPipelineStageFlagBits pipelineStage,
        VkQueryPool             queryPool,
        uint32_t                query
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdWriteTimestamp(commandBuffer, pipelineStage, queryPool, query);
    }

    void vkCmdCopyQueryPoolResults(
        VkCommandBuffer    commandBuffer,
        VkQueryPool        queryPool,
        uint32_t           firstQuery,
        uint32_t           queryCount,
        VkBuffer           dstBuffer,
        VkDeviceSize       dstOffset,
        VkDeviceSize       stride,
        VkQueryResultFlags flags
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdCopyQueryPoolResults(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
    }

    void vkCmdPushConstants(
        VkCommandBuffer    commandBuffer,
        VkPipelineLayout   layout,
        VkShaderStageFlags stageFlags,
        uint32_t           offset,
        uint32_t           size,
        const void*        pValues
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
    }

    bool _bBeginRenderPass = false;
    void vkCmdBeginRenderPass(
        VkCommandBuffer              commandBuffer,
        const VkRenderPassBeginInfo* pRenderPassBegin,
        VkSubpassContents            contents
    )
    {
        PROF_CPU_DEEP();
        ASSERT(!_bBeginRenderPass);
        _bBeginRenderPass = true;
        vkfunc::vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
    }

    void vkCmdNextSubpass(
        VkCommandBuffer   commandBuffer,
        VkSubpassContents contents
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdNextSubpass(commandBuffer, contents);
    }

    void vkCmdEndRenderPass(
        VkCommandBuffer commandBuffer
    )
    {
        PROF_CPU_DEEP();
        ASSERT(_bBeginRenderPass);
        _bBeginRenderPass = false;
        vkfunc::vkCmdEndRenderPass(commandBuffer);
    }

    void vkCmdExecuteCommands(
        VkCommandBuffer        commandBuffer,
        uint32_t               commandBufferCount,
        const VkCommandBuffer* pCommandBuffers
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdExecuteCommands(commandBuffer, commandBufferCount, pCommandBuffers);
    }

    void vkCmdPushDescriptorSetKHR(
        VkCommandBuffer             commandBuffer,
        VkPipelineBindPoint         pipelineBindPoint,
        VkPipelineLayout            layout,
        uint32_t                    set,
        uint32_t                    descriptorWriteCount,
        const VkWriteDescriptorSet* pDescriptorWrites
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
    }

    void vkCmdPushDescriptorSetWithTemplateKHR(
        VkCommandBuffer            commandBuffer,
        VkDescriptorUpdateTemplate descriptorUpdateTemplate,
        VkPipelineLayout           layout,
        uint32_t                   set,
        const void*                pData
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
    }

    VkResult vkCreateDescriptorUpdateTemplateKHR(
        VkDevice                                    device,
        const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
        const VkAllocationCallbacks*                pAllocator,
        VkDescriptorUpdateTemplate*                 pDescriptorUpdateTemplate
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateDescriptorUpdateTemplateKHR(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
    }

    void vkUpdateDescriptorSetWithTemplateKHR(
        VkDevice                   device,
        VkDescriptorSet            descriptorSet,
        VkDescriptorUpdateTemplate descriptorUpdateTemplate,
        const void*                pData
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkUpdateDescriptorSetWithTemplateKHR(device, descriptorSet, descriptorUpdateTemplate, pData);
    }

    void vkDestroyDescriptorUpdateTemplateKHR(
        VkDevice                     device,
        VkDescriptorUpdateTemplate   descriptorUpdateTemplate,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyDescriptorUpdateTemplateKHR(device, descriptorUpdateTemplate, pAllocator);
    }

    VkResult vkCreateDebugReportCallbackEXT(
        VkInstance                                instance,
        const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks*              pAllocator,
        VkDebugReportCallbackEXT*                 pCallback
    )
    {
        PROF_CPU_DEEP();
        return vkfunc::vkCreateDebugReportCallbackEXT(instance, pCreateInfo, pAllocator, pCallback);
    }

    void vkDebugReportMessageEXT(
        VkInstance                 instance,
        VkDebugReportFlagsEXT      flags,
        VkDebugReportObjectTypeEXT objectType,
        uint64_t                   object,
        size_t                     location,
        int32_t                    messageCode,
        const char*                pLayerPrefix,
        const char*                pMessage
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDebugReportMessageEXT(instance, flags, objectType, object, location, messageCode, pLayerPrefix, pMessage);
    }

    void vkDestroyDebugReportCallbackEXT(
        VkInstance                   instance,
        VkDebugReportCallbackEXT     callback,
        const VkAllocationCallbacks* pAllocator
    )
    {
        PROF_CPU_DEEP();
        vkfunc::vkDestroyDebugReportCallbackEXT(instance, callback, pAllocator);
    }

    VkResult vkSetDebugUtilsObjectNameEXT(
        VkDevice                             device,
        const VkDebugUtilsObjectNameInfoEXT* pInfo
    )
    {
        // #ifdef _WIN32
        if (vkfunc::vkSetDebugUtilsObjectNameEXT)
        {
            return vkfunc::vkSetDebugUtilsObjectNameEXT(device, pInfo);
        }
        // #endif
        return VK_SUCCESS;
    }

    void vkCmdBeginDebugUtilsLabelEXT(
        VkCommandBuffer             commandBuffer,
        const VkDebugUtilsLabelEXT* pInfo
    )
    {
        // #ifdef _WIN32
        if (vkfunc::vkCmdBeginDebugUtilsLabelEXT)
        {
            vkfunc::vkCmdBeginDebugUtilsLabelEXT(commandBuffer, pInfo);
        }
        // #endif
    }

    void vkCmdEndDebugUtilsLabelEXT(
        VkCommandBuffer commandBuffer
    )
    {
        // #ifdef _WIN32
        if (vkfunc::vkCmdEndDebugUtilsLabelEXT)
        {
            vkfunc::vkCmdEndDebugUtilsLabelEXT(commandBuffer);
        }
        // #endif
    }

    BOOL LoadVulkanLibrary()
    {
        return vkfunc::LoadVulkanLibrary();
    }

    VkInstance GetLoadedInstance()
    {
        return vkfunc::GetLoadedInstance();
    }

    VkDevice GetLoadedDevice()
    {
        return vkfunc::GetLoadedDevice();
    }

    void LoadVulkanInstanceFunctions(VkInstance instance)
    {
        vkfunc::LoadVulkanInstanceFunctions(instance);
    }

    void LoadVulkanDeviceFunctions(VkDevice device)
    {
        vkfunc::LoadVulkanDeviceFunctions(device);
    }

    void FreeVulkanLibrary()
    {
        vkfunc::FreeVulkanLibrary();
    }

    void GetDeviceConfig()
    {
        vkfunc::GetDeviceConfig();
    }

    int IsApiInited()
    {
        return vkfunc::IsApiInited();
    }

    void SetApiInited()
    {
        vkfunc::SetApiInited();
    }

    void SetGetInstanceProcAddr(PFN_vkGetInstanceProcAddr pGetInstanceProcAddr)
    {
        vkfunc::vkGetInstanceProcAddr = pGetInstanceProcAddr;
    }
} // namespace vks

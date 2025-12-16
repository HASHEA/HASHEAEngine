#pragma once

#include "vk.h"

typedef int BOOL;
struct VkWin32SurfaceCreateInfoKHR;
namespace vks
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkResult   vkCreateWin32SurfaceKHR(
		VkInstance                                  instance,
		const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSurfaceKHR* pSurface);
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
	VkResult  vkCreateAndroidSurfaceKHR(
		VkInstance                                  instance,
		const VkAndroidSurfaceCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSurfaceKHR* pSurface);
	VkResult vkGetAndroidHardwareBufferPropertiesANDROID(
		VkDevice                                    device,
		const struct AHardwareBuffer*               buffer,
		VkAndroidHardwareBufferPropertiesANDROID*   pProperties);

	VkResult vkCreateSamplerYcbcrConversion(
		VkDevice                                    device,
		const VkSamplerYcbcrConversionCreateInfo*   pCreateInfo,
		const VkAllocationCallbacks*                pAllocator,
		VkSamplerYcbcrConversion*                   pYcbcrConversion);

    void vkDestroySamplerYcbcrConversion(
        VkDevice                                    device,
        VkSamplerYcbcrConversion                    ycbcrConversion,
        const VkAllocationCallbacks*                pAllocator);
#endif

#ifdef VK_USE_PLATFORM_OHOS
    VkResult vkCreateSurfaceOHOS(
        VkInstance                                  instance, 
        const VkSurfaceCreateInfoOHOS*              pCreateInfo,
        const VkAllocationCallbacks*                pAllocator, 
        VkSurfaceKHR*                               pSurface);
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR	
	VkResult  vkCreateXlibSurfaceKHR(
		VkInstance                                  instance,
		const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSurfaceKHR* pSurface);
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR	
	VkResult  vkCreateXcbSurfaceKHR(
		VkInstance                                  instance,
		const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSurfaceKHR* pSurface);
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	VkResult vkCreateWaylandSurfaceKHR(
		VkInstance                                  instance,
		const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSurfaceKHR* pSurface);
#endif

//#ifdef VK_USE_PLATFORM_MACOS_MVK
//	VkResult  vkCreateMacOSSurfaceMVK(
//		VkInstance                                  instance,
//		const VkMacOSSurfaceCreateInfoMVK* pCreateInfo,
//		const VkAllocationCallbacks* pAllocator,
//		VkSurfaceKHR* pSurface);
//#endif

#ifdef USE_PLATFORM_NULLWS	
	VkResult vkGetPhysicalDeviceDisplayPropertiesKHR(
		VkPhysicalDevice                            physicalDevice,
		uint32_t* pPropertyCount,
		VkDisplayPropertiesKHR* pProperties);

	VkResult  vkGetDisplayModePropertiesKHR(
		VkPhysicalDevice                            physicalDevice,
		VkDisplayKHR                                display,
		uint32_t* pPropertyCount,
		VkDisplayModePropertiesKHR* pProperties);

	VkResult  vkCreateDisplayPlaneSurfaceKHR(
		VkInstance                                  instance,
		const VkDisplaySurfaceCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSurfaceKHR* pSurface);
#endif
	/************************** ray tracing ******************************************/
	void vkCreateAccelerationStructureKHR(
		VkDevice                                    device,
		const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkAccelerationStructureKHR* pAccelerationStructure);

	void vkDestroyAccelerationStructureKHR(
		VkDevice                                    device,
		VkAccelerationStructureKHR                  accelerationStructure,
		const VkAllocationCallbacks* pAllocator);

	void vkCmdBuildAccelerationStructuresKHR(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);

	void vkCmdBuildAccelerationStructuresIndirectKHR(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkDeviceAddress* pIndirectDeviceAddresses,
		const uint32_t* pIndirectStrides,
		const uint32_t* const* ppMaxPrimitiveCounts);

	VkResult vkBuildAccelerationStructuresKHR(
		VkDevice                                    device,
		VkDeferredOperationKHR                      deferredOperation,
		uint32_t                                    infoCount,
		const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
		const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);

	VkResult vkCopyAccelerationStructureKHR(
		VkDevice                                    device,
		VkDeferredOperationKHR                      deferredOperation,
		const VkCopyAccelerationStructureInfoKHR* pInfo);

	VkResult vkCopyAccelerationStructureToMemoryKHR(
		VkDevice                                    device,
		VkDeferredOperationKHR                      deferredOperation,
		const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo);

	VkResult vkCopyMemoryToAccelerationStructureKHR(
		VkDevice                                    device,
		VkDeferredOperationKHR                      deferredOperation,
		const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo);

	VkResult vkWriteAccelerationStructuresPropertiesKHR(
		VkDevice                                    device,
		uint32_t                                    accelerationStructureCount,
		const VkAccelerationStructureKHR* pAccelerationStructures,
		VkQueryType                                 queryType,
		size_t                                      dataSize,
		void* pData,
		size_t                                      stride);

	void vkCmdCopyAccelerationStructureKHR(
		VkCommandBuffer                             commandBuffer,
		const VkCopyAccelerationStructureInfoKHR* pInfo);

	void vkCmdCopyAccelerationStructureToMemoryKHR(
		VkCommandBuffer                             commandBuffer,
		const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo);

	void vkCmdCopyMemoryToAccelerationStructureKHR(
		VkCommandBuffer                             commandBuffer,
		const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo);

	VkDeviceAddress vkGetAccelerationStructureDeviceAddressKHR(
		VkDevice                                    device,
		const VkAccelerationStructureDeviceAddressInfoKHR* pInfo);

	void vkCmdWriteAccelerationStructuresPropertiesKHR(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    accelerationStructureCount,
		const VkAccelerationStructureKHR* pAccelerationStructures,
		VkQueryType                                 queryType,
		VkQueryPool                                 queryPool,
		uint32_t                                    firstQuery);

	void vkGetDeviceAccelerationStructureCompatibilityKHR(
		VkDevice                                    device,
		const VkAccelerationStructureVersionInfoKHR* pVersionInfo,
		VkAccelerationStructureCompatibilityKHR* pCompatibility);

	void vkGetAccelerationStructureBuildSizesKHR(
		VkDevice                                    device,
		VkAccelerationStructureBuildTypeKHR         buildType,
		const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
		const uint32_t* pMaxPrimitiveCounts,
		VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo);

	void vkCmdTraceRaysKHR(
		VkCommandBuffer                             commandBuffer,
		const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
		uint32_t                                    width,
		uint32_t                                    height,
		uint32_t                                    depth);

	VkResult vkCreateRayTracingPipelinesKHR(
		VkDevice                                    device,
		VkDeferredOperationKHR                      deferredOperation,
		VkPipelineCache                             pipelineCache,
		uint32_t                                    createInfoCount,
		const VkRayTracingPipelineCreateInfoKHR* pCreateInfos,
		const VkAllocationCallbacks* pAllocator,
		VkPipeline* pPipelines);

	VkResult vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(
		VkDevice                                    device,
		VkPipeline                                  pipeline,
		uint32_t                                    firstGroup,
		uint32_t                                    groupCount,
		size_t                                      dataSize,
		void* pData);

	void vkCmdTraceRaysIndirectKHR(
		VkCommandBuffer                             commandBuffer,
		const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
		const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
		VkDeviceAddress                             indirectDeviceAddress);

	VkDeviceSize vkGetRayTracingShaderGroupStackSizeKHR(
		VkDevice                                    device,
		VkPipeline                                  pipeline,
		uint32_t                                    group,
		VkShaderGroupShaderKHR                      groupShader);

	void vkCmdSetRayTracingPipelineStackSizeKHR(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    pipelineStackSize);


	void vkGetPhysicalDeviceProperties2(
		VkPhysicalDevice                            physicalDevice,
		VkPhysicalDeviceProperties2* pProperties);

	VkResult vkGetRayTracingShaderGroupHandlesKHR(
		VkDevice                                    device,
		VkPipeline                                  pipeline,
		uint32_t                                    firstGroup,
		uint32_t                                    groupCount,
		size_t                                      dataSize,
		void* pData);
	///////////////////////////////////////////////////////////////////////////////
	VkDeviceAddress vkGetBufferDeviceAddress(
		VkDevice                                    device,
		const VkBufferDeviceAddressInfo* pInfo);

    void vkGetDescriptorSetLayoutSupport(
        VkDevice    device,
        const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
        VkDescriptorSetLayoutSupport* pSupport);

	VkResult vkGetPhysicalDeviceSurfaceSupportKHR(
		VkPhysicalDevice                            physicalDevice,
		uint32_t                                    queueFamilyIndex,
		VkSurfaceKHR                                surface,
		VkBool32* pSupported);

	VkResult  vkGetPhysicalDeviceSurfacePresentModesKHR(
		VkPhysicalDevice                            physicalDevice,
		VkSurfaceKHR                                surface,
		uint32_t* pPresentModeCount,
		VkPresentModeKHR* pPresentModes);


	VkResult  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		VkPhysicalDevice                            physicalDevice,
		VkSurfaceKHR                                surface,
		VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);


	VkResult  vkGetPhysicalDeviceSurfaceFormatsKHR(
		VkPhysicalDevice                            physicalDevice,
		VkSurfaceKHR                                surface,
		uint32_t* pSurfaceFormatCount,
		VkSurfaceFormatKHR* pSurfaceFormats);


	VkResult  vkCreateSwapchainKHR(
		VkDevice                                    device,
		const VkSwapchainCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSwapchainKHR* pSwapchain);

	VkResult  vkGetSwapchainImagesKHR(
		VkDevice                                    device,
		VkSwapchainKHR                              swapchain,
		uint32_t* pSwapchainImageCount,
		VkImage* pSwapchainImages);

	VkResult  vkAcquireNextImageKHR(
		VkDevice                                    device,
		VkSwapchainKHR                              swapchain,
		uint64_t                                    timeout,
		VkSemaphore                                 semaphore,
		VkFence                                     fence,
		uint32_t* pImageIndex);

	VkResult  vkQueuePresentKHR(
		VkQueue                                     queue,
		const VkPresentInfoKHR* pPresentInfo);

	void  vkDestroySwapchainKHR(
		VkDevice                                    device,
		VkSwapchainKHR                              swapchain,
		const VkAllocationCallbacks* pAllocator);


	VkResult vkCreateInstance(
		const VkInstanceCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkInstance* pInstance);

	VkResult  vkEnumeratePhysicalDevices(
		VkInstance                                  instance,
		uint32_t* pPhysicalDeviceCount,
		VkPhysicalDevice* pPhysicalDevices);

	void  vkDestroyInstance(
		VkInstance                                  instance,
		const VkAllocationCallbacks* pAllocator);


	void vkDestroySurfaceKHR(
		VkInstance                                  instance,
		VkSurfaceKHR                                surface,
		const VkAllocationCallbacks* pAllocator);

	PFN_vkVoidFunction vkGetInstanceProcAddr(
		VkInstance                                  instance,
		const char* pName);

	PFN_vkVoidFunction  vkGetDeviceProcAddr(
		VkDevice                                    device,
		const char* pName);

	void vkGetPhysicalDeviceFeatures(
		VkPhysicalDevice                            physicalDevice,
		VkPhysicalDeviceFeatures* pFeatures);

	BOOL vkGetPhysicalDeviceFeatures2KHR(
		VkPhysicalDevice                            physicalDevice,
		VkPhysicalDeviceFeatures2* pFeatures);

	void  vkGetPhysicalDeviceFormatProperties(
		VkPhysicalDevice                            physicalDevice,
		VkFormat                                    format,
		VkFormatProperties* pFormatProperties);

	VkResult  vkGetPhysicalDeviceImageFormatProperties(
		VkPhysicalDevice                            physicalDevice,
		VkFormat                                    format,
		VkImageType                                 type,
		VkImageTiling                               tiling,
		VkImageUsageFlags                           usage,
		VkImageCreateFlags                          flags,
		VkImageFormatProperties* pImageFormatProperties);

	void vkGetPhysicalDeviceProperties(
		VkPhysicalDevice                            physicalDevice,
		VkPhysicalDeviceProperties* pProperties);

	void vkGetPhysicalDeviceProperties2KHR(
		VkPhysicalDevice                            physicalDevice,
		VkPhysicalDeviceProperties2* pProperties);

	void  vkGetPhysicalDeviceQueueFamilyProperties(
		VkPhysicalDevice                            physicalDevice,
		uint32_t* pQueueFamilyPropertyCount,
		VkQueueFamilyProperties* pQueueFamilyProperties);

	void vkGetPhysicalDeviceMemoryProperties(
		VkPhysicalDevice                            physicalDevice,
		VkPhysicalDeviceMemoryProperties* pMemoryProperties);

	void vkGetPhysicalDeviceMemoryProperties2KHR(
		VkPhysicalDevice                            physicalDevice,
		VkPhysicalDeviceMemoryProperties2* pMemoryProperties);

	VkResult vkCreateDevice(
		VkPhysicalDevice                            physicalDevice,
		const VkDeviceCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDevice* pDevice);

	void vkDestroyDevice(
		VkDevice                                    device,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkEnumerateInstanceExtensionProperties(
		const char* pLayerName,
		uint32_t* pPropertyCount,
		VkExtensionProperties* pProperties);

	VkResult  vkEnumerateDeviceExtensionProperties(
		VkPhysicalDevice                            physicalDevice,
		const char* pLayerName,
		uint32_t* pPropertyCount,
		VkExtensionProperties* pProperties);

	VkResult  vkEnumerateInstanceLayerProperties(
		uint32_t* pPropertyCount,
		VkLayerProperties* pProperties);

	VkResult vkEnumerateDeviceLayerProperties(
		VkPhysicalDevice                            physicalDevice,
		uint32_t* pPropertyCount,
		VkLayerProperties* pProperties);


	void vkGetDeviceQueue(
		VkDevice                                    device,
		uint32_t                                    queueFamilyIndex,
		uint32_t                                    queueIndex,
		VkQueue* pQueue);

	VkResult  vkQueueSubmit(
		VkQueue                                     queue,
		uint32_t                                    submitCount,
		const VkSubmitInfo* pSubmits,
		VkFence                                     fence);

	VkResult  vkQueueWaitIdle(
		VkQueue                                     queue);

	VkResult  vkDeviceWaitIdle(
		VkDevice                                    device);

	VkResult  vkAllocateMemory(
		VkDevice                                    device,
		const VkMemoryAllocateInfo* pAllocateInfo,
		const VkAllocationCallbacks* pAllocator,
		KVkDeviceMemory* pMemory);


	void  vkFreeMemory(
		VkDevice                                    device,
		KVkDeviceMemory                              memory,
		const VkAllocationCallbacks* pAllocator);


	VkResult vkMapMemory(
		VkDevice                                    device,
		KVkDeviceMemory                              pMemory,
		VkDeviceSize                                offset,
		VkDeviceSize                                size,
		VkMemoryMapFlags                            flags,
		void** ppData);

	void  vkUnmapMemory(
		VkDevice                                    device,
		KVkDeviceMemory                              pMemory);


	VkResult vkFlushMappedMemoryRanges(
		VkDevice                                    device,
		uint32_t                                    memoryRangeCount,
		const VkMappedMemoryRange* pMemoryRanges);

	VkResult  vkInvalidateMappedMemoryRanges(
		VkDevice                                    device,
		uint32_t                                    memoryRangeCount,
		const VkMappedMemoryRange* pMemoryRanges);


	void vkGetDeviceMemoryCommitment(
		VkDevice                                    device,
		KVkDeviceMemory                              memory,
		VkDeviceSize* pCommittedMemoryInBytes);

	VkResult vkBindBufferMemory(
		VkDevice                                    device,
		VkBuffer                                    buffer,
		KVkDeviceMemory                              memory,
		VkDeviceSize                                memoryOffset);

	VkResult  vkBindImageMemory(
		VkDevice                                    device,
		VkImage                                     image,
		KVkDeviceMemory                              memory,
		VkDeviceSize                                memoryOffset);

	void  vkGetBufferMemoryRequirements(
		VkDevice                                    device,
		VkBuffer                                    buffer,
		VkMemoryRequirements* pMemoryRequirements);

	void vkGetImageMemoryRequirements(
		VkDevice                                    device,
		VkImage                                     image,
		VkMemoryRequirements* pMemoryRequirements);

	void vkGetBufferMemoryRequirements2KHR(
		VkDevice                                    device,
		const VkBufferMemoryRequirementsInfo2* pInfo,
		VkMemoryRequirements2* pMemoryRequirements);

	void vkGetImageMemoryRequirements2KHR(
		VkDevice                                    device,
		const VkImageMemoryRequirementsInfo2* pInfo,
		VkMemoryRequirements2* pMemoryRequirements);

	void  vkGetImageSparseMemoryRequirements(
		VkDevice                                    device,
		VkImage                                     image,
		uint32_t* pSparseMemoryRequirementCount,
		VkSparseImageMemoryRequirements* pSparseMemoryRequirements);

	void  vkGetPhysicalDeviceSparseImageFormatProperties(
		VkPhysicalDevice                            physicalDevice,
		VkFormat                                    format,
		VkImageType                                 type,
		VkSampleCountFlagBits                       samples,
		VkImageUsageFlags                           usage,
		VkImageTiling                               tiling,
		uint32_t* pPropertyCount,
		VkSparseImageFormatProperties* pProperties);


	VkResult  vkQueueBindSparse(
		VkQueue                                     queue,
		uint32_t                                    bindInfoCount,
		const VkBindSparseInfo* pBindInfo,
		VkFence                                     fence);

	VkResult  vkCreateFence(
		VkDevice                                    device,
		const VkFenceCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkFence* pFence);

	void  vkDestroyFence(
		VkDevice                                    device,
		VkFence                                     fence,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkResetFences(
		VkDevice                                    device,
		uint32_t                                    fenceCount,
		const VkFence* pFences);


	VkResult  vkGetFenceStatus(
		VkDevice                                    device,
		VkFence                                     fence);

	VkResult vkWaitForFences(
		VkDevice                                    device,
		uint32_t                                    fenceCount,
		const VkFence* pFences,
		VkBool32                                    waitAll,
		uint64_t                                    timeout);


	VkResult  vkCreateSemaphore(
		VkDevice                                    device,
		const VkSemaphoreCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSemaphore* pSemaphore);


	void vkDestroySemaphore(
		VkDevice                                    device,
		VkSemaphore                                 semaphore,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkCreateEvent(
		VkDevice                                    device,
		const VkEventCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkEvent* pEvent);


	void vkDestroyEvent(
		VkDevice                                    device,
		VkEvent                                     event,
		const VkAllocationCallbacks* pAllocator);


	VkResult vkGetEventStatus(
		VkDevice                                    device,
		VkEvent                                     event);


	VkResult  vkSetEvent(
		VkDevice                                    device,
		VkEvent                                     event);

	VkResult  vkResetEvent(
		VkDevice                                    device,
		VkEvent                                     event);

	VkResult  vkCreateQueryPool(
		VkDevice                                    device,
		const VkQueryPoolCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkQueryPool* pQueryPool);

	void vkDestroyQueryPool(
		VkDevice                                    device,
		VkQueryPool                                 queryPool,
		const VkAllocationCallbacks* pAllocator);

	VkResult  vkGetQueryPoolResults(
		VkDevice                                    device,
		VkQueryPool                                 queryPool,
		uint32_t                                    firstQuery,
		uint32_t                                    queryCount,
		size_t                                      dataSize,
		void* pData,
		VkDeviceSize                                stride,
		VkQueryResultFlags                          flags);

	VkResult  vkCreateBuffer(
		VkDevice                                    device,
		const VkBufferCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkBuffer* pBuffer);

	void vkDestroyBuffer(
		VkDevice                                    device,
		VkBuffer                                    buffer,
		const VkAllocationCallbacks* pAllocator);

	VkResult  vkCreateBufferView(
		VkDevice                                    device,
		const VkBufferViewCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkBufferView* pView);


	void vkDestroyBufferView(
		VkDevice                                    device,
		VkBufferView                                bufferView,
		const VkAllocationCallbacks* pAllocator);


	VkResult  vkCreateImage(
		VkDevice                                    device,
		const VkImageCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkImage* pImage);

	void  vkDestroyImage(
		VkDevice                                    device,
		VkImage                                     image,
		const VkAllocationCallbacks* pAllocator);

	void vkGetImageSubresourceLayout(
		VkDevice                                    device,
		VkImage                                     image,
		const VkImageSubresource* pSubresource,
		VkSubresourceLayout* pLayout);

	VkResult  vkCreateImageView(
		VkDevice                                    device,
		const VkImageViewCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkImageView* pView);

	void vkDestroyImageView(
		VkDevice                                    device,
		VkImageView                                 imageView,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkCreateShaderModule(
		VkDevice                                    device,
		const VkShaderModuleCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkShaderModule* pShaderModule);

	void  vkDestroyShaderModule(
		VkDevice                                    device,
		VkShaderModule                              shaderModule,
		const VkAllocationCallbacks* pAllocator);

	VkResult  vkCreatePipelineCache(
		VkDevice                                    device,
		const VkPipelineCacheCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkPipelineCache* pPipelineCache);

	void  vkDestroyPipelineCache(
		VkDevice                                    device,
		VkPipelineCache                             pipelineCache,
		const VkAllocationCallbacks* pAllocator);

	VkResult  vkGetPipelineCacheData(
		VkDevice                                    device,
		VkPipelineCache                             pipelineCache,
		size_t* pDataSize,
		void* pData);

	VkResult  vkMergePipelineCaches(
		VkDevice                                    device,
		VkPipelineCache                             dstCache,
		uint32_t                                    srcCacheCount,
		const VkPipelineCache* pSrcCaches);


	VkResult  vkCreateGraphicsPipelines(
		VkDevice                                    device,
		VkPipelineCache                             pipelineCache,
		uint32_t                                    createInfoCount,
		const VkGraphicsPipelineCreateInfo* pCreateInfos,
		const VkAllocationCallbacks* pAllocator,
		VkPipeline* pPipelines);

	VkResult  vkCreateComputePipelines(
		VkDevice                                    device,
		VkPipelineCache                             pipelineCache,
		uint32_t                                    createInfoCount,
		const VkComputePipelineCreateInfo* pCreateInfos,
		const VkAllocationCallbacks* pAllocator,
		VkPipeline* pPipelines);

	void  vkDestroyPipeline(
		VkDevice                                    device,
		VkPipeline                                  pipeline,
		const VkAllocationCallbacks* pAllocator);


	VkResult  vkCreatePipelineLayout(
		VkDevice                                    device,
		const VkPipelineLayoutCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkPipelineLayout* pPipelineLayout);

	void  vkDestroyPipelineLayout(
		VkDevice                                    device,
		VkPipelineLayout                            pipelineLayout,
		const VkAllocationCallbacks* pAllocator);


	VkResult  vkCreateSampler(
		VkDevice                                    device,
		const VkSamplerCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSampler* pSampler);


	void vkDestroySampler(
		VkDevice                                    device,
		VkSampler                                   sampler,
		const VkAllocationCallbacks* pAllocator);

	void vkDestroySamplers(VkDevice	device);


	VkResult  vkCreateDescriptorSetLayout(
		VkDevice                                    device,
		const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDescriptorSetLayout* pSetLayout);


	void vkDestroyDescriptorSetLayout(
		VkDevice                                    device,
		VkDescriptorSetLayout                       descriptorSetLayout,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkCreateDescriptorPool(
		VkDevice                                    device,
		const VkDescriptorPoolCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDescriptorPool* pDescriptorPool);


	void  vkDestroyDescriptorPool(
		VkDevice                                    device,
		VkDescriptorPool                            descriptorPool,
		const VkAllocationCallbacks* pAllocator);


	VkResult  vkResetDescriptorPool(
		VkDevice                                    device,
		VkDescriptorPool                            descriptorPool,
		VkDescriptorPoolResetFlags                  flags);

	VkResult  vkAllocateDescriptorSets(
		VkDevice                                    device,
		const VkDescriptorSetAllocateInfo* pAllocateInfo,
		VkDescriptorSet* pDescriptorSets);

	VkResult vkFreeDescriptorSets(
		VkDevice                                    device,
		VkDescriptorPool                            descriptorPool,
		uint32_t                                    descriptorSetCount,
		const VkDescriptorSet* pDescriptorSets);


	void  vkUpdateDescriptorSets(
		VkDevice                                    device,
		uint32_t                                    descriptorWriteCount,
		const VkWriteDescriptorSet* pDescriptorWrites,
		uint32_t                                    descriptorCopyCount,
		const VkCopyDescriptorSet* pDescriptorCopies);


	VkResult vkCreateFramebuffer(
		VkDevice                                    device,
		const VkFramebufferCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkFramebuffer* pFramebuffer);

	void  vkDestroyFramebuffer(
		VkDevice                                    device,
		VkFramebuffer                               framebuffer,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkCreateRenderPass(
		VkDevice                                    device,
		const VkRenderPassCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkRenderPass* pRenderPass);

	void vkDestroyRenderPass(
		VkDevice                                    device,
		VkRenderPass                                renderPass,
		const VkAllocationCallbacks* pAllocator);

	void vkGetRenderAreaGranularity(
		VkDevice                                    device,
		VkRenderPass                                renderPass,
		VkExtent2D* pGranularity);

	VkResult  vkCreateCommandPool(
		VkDevice                                    device,
		const VkCommandPoolCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkCommandPool* pCommandPool);

	void vkDestroyCommandPool(
		VkDevice                                    device,
		VkCommandPool                               commandPool,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkResetCommandPool(
		VkDevice                                    device,
		VkCommandPool                               commandPool,
		VkCommandPoolResetFlags                     flags);

	void vkTrimCommandPool(
		VkDevice                device,
		VkCommandPool           commandPool,
		VkCommandPoolTrimFlags flags
	);

	VkResult vkAllocateCommandBuffers(
		VkDevice                                    device,
		const VkCommandBufferAllocateInfo* pAllocateInfo,
		VkCommandBuffer* pCommandBuffers);


	void vkFreeCommandBuffers(
		VkDevice                                    device,
		VkCommandPool                               commandPool,
		uint32_t                                    commandBufferCount,
		const VkCommandBuffer* pCommandBuffers);

	VkResult vkBeginCommandBuffer(
		VkCommandBuffer                             commandBuffer,
		const VkCommandBufferBeginInfo* pBeginInfo);

	VkResult vkEndCommandBuffer(
		VkCommandBuffer                             commandBuffer);

	VkResult vkResetCommandBuffer(
		VkCommandBuffer                             commandBuffer,
		VkCommandBufferResetFlags                   flags);

	void vkCmdBindPipeline(
		VkCommandBuffer                             commandBuffer,
		VkPipelineBindPoint                         pipelineBindPoint,
		VkPipeline                                  pipeline);

	void vkCmdSetViewport(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    firstViewport,
		uint32_t                                    viewportCount,
		const VkViewport* pViewports);

	void vkCmdSetScissor(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    firstScissor,
		uint32_t                                    scissorCount,
		const VkRect2D* pScissors);

	void vkCmdSetLineWidth(
		VkCommandBuffer                             commandBuffer,
		float                                       lineWidth);

	void vkCmdSetDepthBias(
		VkCommandBuffer                             commandBuffer,
		float                                       depthBiasConstantFactor,
		float                                       depthBiasClamp,
		float                                       depthBiasSlopeFactor);

	void vkCmdSetBlendConstants(
		VkCommandBuffer                             commandBuffer,
		const float                                 blendConstants[4]);



	void vkCmdSetDepthBounds(
		VkCommandBuffer                             commandBuffer,
		float                                       minDepthBounds,
		float                                       maxDepthBounds);

	void  vkCmdSetStencilCompareMask(
		VkCommandBuffer                             commandBuffer,
		VkStencilFaceFlags                          faceMask,
		uint32_t                                    compareMask);

	void  vkCmdSetStencilWriteMask(
		VkCommandBuffer                             commandBuffer,
		VkStencilFaceFlags                          faceMask,
		uint32_t                                    writeMask);

	void vkCmdSetStencilReference(
		VkCommandBuffer                             commandBuffer,
		VkStencilFaceFlags                          faceMask,
		uint32_t                                    reference);

	void  vkCmdBindDescriptorSets(
		VkCommandBuffer                             commandBuffer,
		VkPipelineBindPoint                         pipelineBindPoint,
		VkPipelineLayout                            layout,
		uint32_t                                    firstSet,
		uint32_t                                    descriptorSetCount,
		const VkDescriptorSet* pDescriptorSets,
		uint32_t                                    dynamicOffsetCount,
		const uint32_t* pDynamicOffsets);

	void vkCmdBindIndexBuffer(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    buffer,
		VkDeviceSize                                offset,
		VkIndexType                                 indexType);

	void  vkCmdBindVertexBuffers(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    firstBinding,
		uint32_t                                    bindingCount,
		const VkBuffer* pBuffers,
		const VkDeviceSize* pOffsets);

	void vkCmdDraw(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    vertexCount,
		uint32_t                                    instanceCount,
		uint32_t                                    firstVertex,
		uint32_t                                    firstInstance);

	void vkCmdDrawIndexed(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    indexCount,
		uint32_t                                    instanceCount,
		uint32_t                                    firstIndex,
		int32_t                                     vertexOffset,
		uint32_t                                    firstInstance);

	void vkCmdDrawIndirect(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    buffer,
		VkDeviceSize                                offset,
		uint32_t                                    drawCount,
		uint32_t                                    stride);


	void vkCmdDrawIndexedIndirect(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    buffer,
		VkDeviceSize                                offset,
		uint32_t                                    drawCount,
		uint32_t                                    stride);

	void vkCmdDispatch(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    groupCountX,
		uint32_t                                    groupCountY,
		uint32_t                                    groupCountZ);

	void vkCmdDispatchIndirect(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    buffer,
		VkDeviceSize                                offset);


	void vkCmdCopyBuffer(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    srcBuffer,
		VkBuffer                                    dstBuffer,
		uint32_t                                    regionCount,
		const VkBufferCopy* pRegions);


	void vkCmdCopyImage(
		VkCommandBuffer                             commandBuffer,
		VkImage                                     srcImage,
		VkImageLayout                               srcImageLayout,
		VkImage                                     dstImage,
		VkImageLayout                               dstImageLayout,
		uint32_t                                    regionCount,
		const VkImageCopy* pRegions);


	void  vkCmdBlitImage(
		VkCommandBuffer                             commandBuffer,
		VkImage                                     srcImage,
		VkImageLayout                               srcImageLayout,
		VkImage                                     dstImage,
		VkImageLayout                               dstImageLayout,
		uint32_t                                    regionCount,
		const VkImageBlit* pRegions,
		VkFilter                                    filter);

	void vkCmdCopyBufferToImage(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    srcBuffer,
		VkImage                                     dstImage,
		VkImageLayout                               dstImageLayout,
		uint32_t                                    regionCount,
		const VkBufferImageCopy* pRegions);


	void vkCmdCopyImageToBuffer(
		VkCommandBuffer                             commandBuffer,
		VkImage                                     srcImage,
		VkImageLayout                               srcImageLayout,
		VkBuffer                                    dstBuffer,
		uint32_t                                    regionCount,
		const VkBufferImageCopy* pRegions);

	void vkCmdUpdateBuffer(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    dstBuffer,
		VkDeviceSize                                dstOffset,
		VkDeviceSize                                dataSize,
		const void* pData);

	void  vkCmdFillBuffer(
		VkCommandBuffer                             commandBuffer,
		VkBuffer                                    dstBuffer,
		VkDeviceSize                                dstOffset,
		VkDeviceSize                                size,
		uint32_t                                    data);

	void vkCmdClearColorImage(
		VkCommandBuffer                             commandBuffer,
		VkImage                                     image,
		VkImageLayout                               imageLayout,
		const VkClearColorValue* pColor,
		uint32_t                                    rangeCount,
		const VkImageSubresourceRange* pRanges);

	void vkCmdClearDepthStencilImage(
		VkCommandBuffer                             commandBuffer,
		VkImage                                     image,
		VkImageLayout                               imageLayout,
		const VkClearDepthStencilValue* pDepthStencil,
		uint32_t                                    rangeCount,
		const VkImageSubresourceRange* pRanges);

	void vkCmdClearAttachments(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    attachmentCount,
		const VkClearAttachment* pAttachments,
		uint32_t                                    rectCount,
		const VkClearRect* pRects);

	void vkCmdResolveImage(
		VkCommandBuffer                             commandBuffer,
		VkImage                                     srcImage,
		VkImageLayout                               srcImageLayout,
		VkImage                                     dstImage,
		VkImageLayout                               dstImageLayout,
		uint32_t                                    regionCount,
		const VkImageResolve* pRegions);

	void vkCmdSetEvent(
		VkCommandBuffer                             commandBuffer,
		VkEvent                                     event,
		VkPipelineStageFlags                        stageMask);

	void vkCmdResetEvent(
		VkCommandBuffer                             commandBuffer,
		VkEvent                                     event,
		VkPipelineStageFlags                        stageMask);

	void  vkCmdWaitEvents(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    eventCount,
		const VkEvent* pEvents,
		VkPipelineStageFlags                        srcStageMask,
		VkPipelineStageFlags                        dstStageMask,
		uint32_t                                    memoryBarrierCount,
		const VkMemoryBarrier* pMemoryBarriers,
		uint32_t                                    bufferMemoryBarrierCount,
		const VkBufferMemoryBarrier* pBufferMemoryBarriers,
		uint32_t                                    imageMemoryBarrierCount,
		const VkImageMemoryBarrier* pImageMemoryBarriers);



	void  vkCmdPipelineBarrier(
		VkCommandBuffer                             commandBuffer,
		VkPipelineStageFlags                        srcStageMask,
		VkPipelineStageFlags                        dstStageMask,
		VkDependencyFlags                           dependencyFlags,
		uint32_t                                    memoryBarrierCount,
		const VkMemoryBarrier* pMemoryBarriers,
		uint32_t                                    bufferMemoryBarrierCount,
		const VkBufferMemoryBarrier* pBufferMemoryBarriers,
		uint32_t                                    imageMemoryBarrierCount,
		const VkImageMemoryBarrier* pImageMemoryBarriers);

	void vkCmdBeginQuery(
		VkCommandBuffer                             commandBuffer,
		VkQueryPool                                 queryPool,
		uint32_t                                    query,
		VkQueryControlFlags                         flags);

	void vkCmdEndQuery(
		VkCommandBuffer                             commandBuffer,
		VkQueryPool                                 queryPool,
		uint32_t                                    query);

	void  vkCmdResetQueryPool(
		VkCommandBuffer                             commandBuffer,
		VkQueryPool                                 queryPool,
		uint32_t                                    firstQuery,
		uint32_t                                    queryCount);

	void vkCmdWriteTimestamp(
		VkCommandBuffer                             commandBuffer,
		VkPipelineStageFlagBits                     pipelineStage,
		VkQueryPool                                 queryPool,
		uint32_t                                    query);

	void vkCmdCopyQueryPoolResults(
		VkCommandBuffer                             commandBuffer,
		VkQueryPool                                 queryPool,
		uint32_t                                    firstQuery,
		uint32_t                                    queryCount,
		VkBuffer                                    dstBuffer,
		VkDeviceSize                                dstOffset,
		VkDeviceSize                                stride,
		VkQueryResultFlags                          flags);

	void vkCmdPushConstants(
		VkCommandBuffer                             commandBuffer,
		VkPipelineLayout                            layout,
		VkShaderStageFlags                          stageFlags,
		uint32_t                                    offset,
		uint32_t                                    size,
		const void* pValues);


	void vkCmdBeginRenderPass(
		VkCommandBuffer                             commandBuffer,
		const VkRenderPassBeginInfo* pRenderPassBegin,
		VkSubpassContents                           contents);

	void vkCmdNextSubpass(
		VkCommandBuffer                             commandBuffer,
		VkSubpassContents                           contents);

	void vkCmdEndRenderPass(
		VkCommandBuffer                             commandBuffer);

	void  vkCmdExecuteCommands(
		VkCommandBuffer                             commandBuffer,
		uint32_t                                    commandBufferCount,
		const VkCommandBuffer* pCommandBuffers);

	void  vkCmdPushDescriptorSetKHR(
		VkCommandBuffer                             commandBuffer,
		VkPipelineBindPoint                         pipelineBindPoint,
		VkPipelineLayout                            layout,
		uint32_t                                    set,
		uint32_t                                    descriptorWriteCount,
		const VkWriteDescriptorSet* pDescriptorWrites);


	void vkCmdPushDescriptorSetWithTemplateKHR(
		VkCommandBuffer                             commandBuffer,
		VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
		VkPipelineLayout                            layout,
		uint32_t                                    set,
		const void* pData);

	VkResult vkCreateDescriptorUpdateTemplateKHR(
		VkDevice                                    device,
		const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate);

	void  vkUpdateDescriptorSetWithTemplateKHR(
		VkDevice                                    device,
		VkDescriptorSet                             descriptorSet,
		VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
		const void* pData);

	void  vkDestroyDescriptorUpdateTemplateKHR(
		VkDevice                                    device,
		VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkCreateDebugReportCallbackEXT(
		VkInstance                                  instance,
		const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDebugReportCallbackEXT* pCallback);



	void vkDebugReportMessageEXT(
		VkInstance                                  instance,
		VkDebugReportFlagsEXT                       flags,
		VkDebugReportObjectTypeEXT                  objectType,
		uint64_t                                    object,
		size_t                                      location,
		int32_t                                     messageCode,
		const char* pLayerPrefix,
		const char* pMessage);

	void vkDestroyDebugReportCallbackEXT(
		VkInstance                                  instance,
		VkDebugReportCallbackEXT                    callback,
		const VkAllocationCallbacks* pAllocator);

	VkResult vkSetDebugUtilsObjectNameEXT(
		VkDevice                                    device,
		const VkDebugUtilsObjectNameInfoEXT* pInfo
	);

	void vkCmdBeginDebugUtilsLabelEXT(
		VkCommandBuffer                             commandBuffer,
		const VkDebugUtilsLabelEXT* pInfo
	);

	void vkCmdEndDebugUtilsLabelEXT(
		VkCommandBuffer                             commandBuffer
	);

	BOOL LoadVulkanLibrary();

	VkInstance GetLoadedInstance();

	VkDevice GetLoadedDevice();

	void LoadVulkanInstanceFunctions(VkInstance instance);

	void LoadVulkanDeviceFunctions(VkDevice device);

	void FreeVulkanLibrary();

	void GetDeviceConfig();

	int IsApiInited();

	void SetApiInited();

	void SetGetInstanceProcAddr(PFN_vkGetInstanceProcAddr pGetInstanceProcAddr);
}

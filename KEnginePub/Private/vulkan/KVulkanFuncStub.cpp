#include "KVulkanFuncStub.h"
#include <string>
#include "Engine/KGLog.h"

#ifdef __ANDROID__
#include <android/log.h>
// android_app* androidApp;
ANativeActivity* android_native_activity = nullptr;
#elif defined(__OHOS__)
#include <hilog/log.h>
#endif

#ifndef _WIN32
#include <dlfcn.h>
#endif
#include "KEnginePub/Public/KEsDrv.h"
///////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"


#undef PVR_VULKAN_FUNCTION_POINTER_DECLARATION
#define PVR_VULKAN_FUNCTION_POINTER_DECLARATION(function_name) PFN_##function_name function_name;

#define PVR_STR(x)                                             #x
#define PVR_VULKAN_GET_INSTANCE_POINTER(instance, function_name)                                  \
    function_name = (PFN_##function_name)vkGetInstanceProcAddr(instance, PVR_STR(function_name)); \
    if (!function_name)                                                                           \
        KGLogPrintf(KGLOG_ERR, "Get instance Proc:%s Addr failed", #function_name);

#define PVR_VULKAN_GET_DEVICE_POINTER(device, function_name)                                  \
    function_name = (PFN_##function_name)vkGetDeviceProcAddr(device, PVR_STR(function_name)); \
    if (!function_name)                                                                       \
        KGLogPrintf(KGLOG_ERR, "Get device Proc:%s Addr failed", #function_name);

namespace vkfunc
{

    void* libVulkan = nullptr;

    int32_t screenDensity = 1;

#ifdef VK_USE_PLATFORM_WIN32_KHR
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateWin32SurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateAndroidSurfaceKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetAndroidHardwareBufferPropertiesANDROID);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateSamplerYcbcrConversion);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroySamplerYcbcrConversion);
#endif

#ifdef VK_USE_PLATFORM_OHOS
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateSurfaceOHOS);
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateXlibSurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateXcbSurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_MACOS_MVK
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateMacOSSurfaceMVK);
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateWaylandSurfaceKHR);
#endif

#ifdef USE_PLATFORM_NULLWS
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceDisplayPropertiesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetDisplayModePropertiesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateDisplayPlaneSurfaceKHR);
#endif

    /******************************* ray tracing ***********************************/
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateAccelerationStructureKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyAccelerationStructureKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBuildAccelerationStructuresKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetAccelerationStructureBuildSizesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetAccelerationStructureDeviceAddressKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdTraceRaysKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdTraceRaysIndirectKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdTraceRaysIndirect2KHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateRayTracingPipelinesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetRayTracingShaderGroupHandlesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdWriteAccelerationStructuresPropertiesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyAccelerationStructureKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBuildAccelerationStructuresIndirectKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkBuildAccelerationStructuresKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCopyAccelerationStructureKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCopyAccelerationStructureToMemoryKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCopyMemoryToAccelerationStructureKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkWriteAccelerationStructuresPropertiesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyAccelerationStructureToMemoryKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyMemoryToAccelerationStructureKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetDeviceAccelerationStructureCompatibilityKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceProperties2);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetRayTracingShaderGroupStackSizeKHR);
    /**********************************************************************************/
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetBufferDeviceAddressKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceSurfaceSupportKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceSurfacePresentModesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceSurfaceFormatsKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateSwapchainKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetSwapchainImagesKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkAcquireNextImageKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkQueuePresentKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroySwapchainKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateInstance);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkEnumeratePhysicalDevices);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyInstance);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroySurfaceKHR);

    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetInstanceProcAddr);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetDeviceProcAddr);

    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceFeatures);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceFeatures2KHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceFormatProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceImageFormatProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceProperties2KHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceQueueFamilyProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceMemoryProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetDescriptorSetLayoutSupport);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceMemoryProperties2KHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateDevice);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyDevice);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkEnumerateInstanceExtensionProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkEnumerateDeviceExtensionProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkEnumerateInstanceLayerProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkEnumerateDeviceLayerProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetDeviceQueue);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkQueueSubmit);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkQueueWaitIdle);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDeviceWaitIdle);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkAllocateMemory);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkFreeMemory);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkMapMemory);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkUnmapMemory);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkFlushMappedMemoryRanges);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkInvalidateMappedMemoryRanges);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetDeviceMemoryCommitment);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkBindBufferMemory);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkBindImageMemory);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetBufferMemoryRequirements);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetImageMemoryRequirements);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetBufferMemoryRequirements2KHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetImageMemoryRequirements2KHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetImageSparseMemoryRequirements);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPhysicalDeviceSparseImageFormatProperties);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkQueueBindSparse);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateFence);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyFence);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkResetFences);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetFenceStatus);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkWaitForFences);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateSemaphore);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroySemaphore);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateEvent);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyEvent);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetEventStatus);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkSetEvent);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkResetEvent);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateQueryPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyQueryPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetQueryPoolResults);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateBufferView);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyBufferView);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetImageSubresourceLayout);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateImageView);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyImageView);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateShaderModule);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyShaderModule);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreatePipelineCache);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyPipelineCache);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetPipelineCacheData);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkMergePipelineCaches);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateGraphicsPipelines);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateComputePipelines);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyPipeline);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreatePipelineLayout);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyPipelineLayout);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateSampler);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroySampler);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateDescriptorSetLayout);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyDescriptorSetLayout);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateDescriptorPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyDescriptorPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkResetDescriptorPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkAllocateDescriptorSets);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkFreeDescriptorSets);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkUpdateDescriptorSets);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateFramebuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyFramebuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateRenderPass);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyRenderPass);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkGetRenderAreaGranularity);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateCommandPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyCommandPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkResetCommandPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkTrimCommandPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkAllocateCommandBuffers);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkFreeCommandBuffers);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkBeginCommandBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkEndCommandBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkResetCommandBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBindPipeline);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetViewport);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetScissor);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetLineWidth);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetDepthBias);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetBlendConstants);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetDepthBounds);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetStencilCompareMask);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetStencilWriteMask);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetStencilReference);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBindDescriptorSets);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBindIndexBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBindVertexBuffers);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdDraw);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdDrawIndexed);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdDrawIndirect);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdDrawIndexedIndirect);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdDispatch);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdDispatchIndirect);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBlitImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyBufferToImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyImageToBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdUpdateBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdFillBuffer);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdClearColorImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdClearDepthStencilImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdClearAttachments);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdResolveImage);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdSetEvent);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdResetEvent);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdWaitEvents);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdPipelineBarrier);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBeginQuery);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdEndQuery);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdResetQueryPool);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdWriteTimestamp);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdCopyQueryPoolResults);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdPushConstants);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBeginRenderPass);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdNextSubpass);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdEndRenderPass);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdExecuteCommands);

    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdPushDescriptorSetKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdPushDescriptorSetWithTemplateKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateDescriptorUpdateTemplateKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkUpdateDescriptorSetWithTemplateKHR);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyDescriptorUpdateTemplateKHR);


    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCreateDebugReportCallbackEXT);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDebugReportMessageEXT);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkDestroyDebugReportCallbackEXT);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkSetDebugUtilsObjectNameEXT);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdBeginDebugUtilsLabelEXT);
    PVR_VULKAN_FUNCTION_POINTER_DECLARATION(vkCmdEndDebugUtilsLabelEXT);

    void* GetFunction(const char* functionName)
    {
        void* pFn = nullptr;
        if (libVulkan)
        {
#if _WIN32
            pFn = GetProcAddress((HMODULE)libVulkan, functionName);
            if (pFn == NULL)
            {
                KGLogPrintf(KGLOG_ERR, "Could not get function %s", functionName);
            }
            // #endif

            // #if __linux__ || __ANDROID__
#else
            pFn = dlsym(libVulkan, functionName);
            if (pFn == NULL)
            {
                KGLogPrintf(KGLOG_ERR, "Could not get function %s\n", functionName);
            }
#endif
            return pFn;
        }
        return NULL;
    }

    std::string get_self_dir()
    {
        static const char*  SELF_NAME     = "/libnative-lib.so";
        static const size_t SELF_NAME_LEN = strlen(SELF_NAME);
        char                linebuf[512]  = "";
        FILE*               fmap          = fopen("/proc/self/maps", "r");
        if (!fmap)
        {
            KGLogPrintf(KGLOG_ERR, "failed to open maps");
            goto Exit0;
        }
        // std::unique_ptr fmap_close{fmap, ::fclose};

        while (fgets(linebuf, sizeof(linebuf), fmap))
        {
            uintptr_t begin, end;
            char      perm[10], offset[20], dev[10], inode[20], path_mem[256], *path;
            int       nr = sscanf(linebuf, "%zx-%zx %s %s %s %s %s", &begin, &end, perm, offset, dev, inode, path_mem);
            if (nr == 6)
            {
                path = nullptr;
            }
            else
            {
                if (nr != 7)
                {
                    KGLogPrintf(KGLOG_ERR, "failed to parse map line: %s", linebuf);
                    goto Exit0;
                }
                path = path_mem;
            }
            if (path)
            {
                auto len          = strlen(path);
                auto last_dir_end = path + len - SELF_NAME_LEN;
                if (!strcmp(last_dir_end, SELF_NAME))
                {
                    last_dir_end[1] = 0;
                    return path;
                }
            }
        }
        KGLogPrintf(KGLOG_ERR, "can not find path of %s", SELF_NAME + 1);
    Exit0:
        if (fmap)
        {
            fclose(fmap);
        }
        return {};
    }


    bool LoadVulkanLibrary()
    {
        int x = 0;
#ifdef _WIN32
        KGLogPrintf(KGLOG_INFO, "[KEnginePub] Loading vulkan-1.dll...\n");
        libVulkan = LoadLibraryA("vulkan-1.dll");
        if (!libVulkan)
        {
            KGLogPrintf(KGLOG_ERR, "[KEnginePub] Could not load vulkan library ");
            return false;
        }
#endif

#if defined(__linux__) || defined(__ANDROID__)
        KGLogPrintf(KGLOG_INFO, "[KEnginePub] Loading libvulkan.so...\n");
        // Load vulkan library
        libVulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!libVulkan)
        {
            if (!libVulkan)
            {
                libVulkan = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            }

            if (!libVulkan)
            {
                KGLogPrintf(KGLOG_ERR, "[KEnginePub] Could not load vulkan library : %s!\n", dlerror());
                return false;
            }
        }
#endif

        // Load base function pointers
#if defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK)
        vkEnumerateInstanceExtensionProperties = ::vkEnumerateInstanceExtensionProperties;
        vkEnumerateInstanceLayerProperties     = ::vkEnumerateInstanceLayerProperties;
        vkCreateInstance                       = ::vkCreateInstance;
        vkGetInstanceProcAddr                  = ::vkGetInstanceProcAddr;
        vkGetDeviceProcAddr                    = ::vkGetDeviceProcAddr;
#else
        vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(GetFunction("vkEnumerateInstanceExtensionProperties"));
        vkEnumerateInstanceLayerProperties     = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(GetFunction("vkEnumerateInstanceLayerProperties"));
        vkCreateInstance                       = reinterpret_cast<PFN_vkCreateInstance>(GetFunction("vkCreateInstance"));
        vkGetInstanceProcAddr                  = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetFunction("vkGetInstanceProcAddr"));
        vkGetDeviceProcAddr                    = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetFunction("vkGetDeviceProcAddr"));
#endif

        if (vkEnumerateInstanceExtensionProperties &&
            vkEnumerateInstanceLayerProperties &&
            vkCreateInstance &&
            vkGetInstanceProcAddr &&
            vkGetDeviceProcAddr)
        {
            return true;
        }
        else
        {
            return false;
        }
    }


    VkInstance g_pLoadedInstance = nullptr;
    VkDevice   g_pLoadedDevice   = nullptr;

    VkInstance GetLoadedInstance()
    {
        return g_pLoadedInstance;
    }

    VkDevice GetLoadedDevice()
    {
        return g_pLoadedDevice;
    }

    bool bLoadedInstanceFunction = false;
    // Load instance based Vulkan function pointers
    void LoadVulkanInstanceFunctions(VkInstance instance)
    {
        if (bLoadedInstanceFunction)
        {
            return;
        }
        bLoadedInstanceFunction = true;
        g_pLoadedInstance       = instance;

#ifdef VK_USE_PLATFORM_WIN32_KHR
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateWin32SurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateAndroidSurfaceKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetAndroidHardwareBufferPropertiesANDROID);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateSamplerYcbcrConversion);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkDestroySamplerYcbcrConversion);
#endif

#ifdef VK_USE_PLATFORM_OHOS
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateSurfaceOHOS);
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateXlibSurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateXcbSurfaceKHR);
#endif


#ifdef VK_USE_PLATFORM_MACOS_MVK
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateMacOSSurfaceMVK);
#endif


#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateWaylandSurfaceKHR);
#endif

#ifdef USE_PLATFORM_NULLWS
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceDisplayPropertiesKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetDisplayModePropertiesKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateDisplayPlaneSurfaceKHR);
#endif

        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkEnumerateDeviceLayerProperties);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkEnumerateDeviceExtensionProperties);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceSurfaceFormatsKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkEnumeratePhysicalDevices);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceQueueFamilyProperties);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceFeatures);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceFeatures2KHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateDevice);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetDeviceProcAddr);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceMemoryProperties);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceMemoryProperties2KHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceSurfacePresentModesKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceFormatProperties);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceProperties);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceProperties2);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceProperties2KHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkDestroySurfaceKHR);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceImageFormatProperties);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkGetPhysicalDeviceSparseImageFormatProperties);
#ifndef _WIN32
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateDebugReportCallbackEXT);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkDebugReportMessageEXT);
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkDestroyDebugReportCallbackEXT);
#endif
        PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkDestroyInstance);

        // PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCmdPushDescriptorSetKHR);
        // PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCreateDescriptorUpdateTemplateKHR);
        // PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCmdPushDescriptorSetWithTemplateKHR);
        if (DrvOption::bForceEnableDebugUtileExtension)
        {
            PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkSetDebugUtilsObjectNameEXT);
            PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCmdBeginDebugUtilsLabelEXT);
            PVR_VULKAN_GET_INSTANCE_POINTER(instance, vkCmdEndDebugUtilsLabelEXT);
        }
    }


    bool bLoadedDeviceFunction = false;

    void LoadVulkanDeviceFunctions(VkDevice device)
    {
        if (bLoadedDeviceFunction)
        {
            return;
        }
        bLoadedDeviceFunction = true;
        g_pLoadedDevice       = device;

        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateSwapchainKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetSwapchainImagesKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkAcquireNextImageKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkQueuePresentKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroySwapchainKHR);

        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyDevice);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetDeviceQueue);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkQueueSubmit);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkQueueWaitIdle);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDeviceWaitIdle);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkAllocateMemory);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkFreeMemory);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkMapMemory);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkUnmapMemory);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkFlushMappedMemoryRanges);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkInvalidateMappedMemoryRanges);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetDeviceMemoryCommitment);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkBindBufferMemory);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkBindImageMemory);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetBufferMemoryRequirements);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetImageMemoryRequirements);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetBufferMemoryRequirements2KHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetImageMemoryRequirements2KHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetImageSparseMemoryRequirements);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkQueueBindSparse);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateFence);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyFence);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkResetFences);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetFenceStatus);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkWaitForFences);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateSemaphore);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroySemaphore);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateEvent);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyEvent);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetEventStatus);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkSetEvent);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkResetEvent);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateQueryPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyQueryPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetQueryPoolResults);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateBufferView);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyBufferView);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetImageSubresourceLayout);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateImageView);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyImageView);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateShaderModule);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyShaderModule);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreatePipelineCache);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyPipelineCache);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetPipelineCacheData);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkMergePipelineCaches);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateGraphicsPipelines);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateComputePipelines);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyPipeline);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreatePipelineLayout);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyPipelineLayout);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateSampler);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroySampler);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateDescriptorSetLayout);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyDescriptorSetLayout);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateDescriptorPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyDescriptorPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkResetDescriptorPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkAllocateDescriptorSets);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkFreeDescriptorSets);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkUpdateDescriptorSets);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateFramebuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyFramebuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateRenderPass);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyRenderPass);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetRenderAreaGranularity);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateCommandPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyCommandPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkResetCommandPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkTrimCommandPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkAllocateCommandBuffers);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkFreeCommandBuffers);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkBeginCommandBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkEndCommandBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkResetCommandBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBindPipeline);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetViewport);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetScissor);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetLineWidth);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetDepthBias);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetBlendConstants);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetDepthBounds);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetStencilCompareMask);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetStencilWriteMask);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetStencilReference);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBindDescriptorSets);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBindIndexBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBindVertexBuffers);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdDraw);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdDrawIndexed);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdDrawIndirect);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdDrawIndexedIndirect);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdDispatch);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdDispatchIndirect);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBlitImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyBufferToImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyImageToBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdUpdateBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdFillBuffer);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdClearColorImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdClearDepthStencilImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdClearAttachments);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdResolveImage);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdSetEvent);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdResetEvent);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdWaitEvents);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdPipelineBarrier);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBeginQuery);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdEndQuery);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdResetQueryPool);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdWriteTimestamp);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyQueryPoolResults);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdPushConstants);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBeginRenderPass);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdNextSubpass);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdEndRenderPass);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdExecuteCommands);
        // A-card dose not support
        // PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdPushDescriptorSetKHR);
        // PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdPushDescriptorSetWithTemplateKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateDescriptorUpdateTemplateKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkUpdateDescriptorSetWithTemplateKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyDescriptorUpdateTemplateKHR);
        // #ifdef _WIN32
        /*PVR_VULKAN_GET_DEVICE_POINTER(device, vkSetDebugUtilsObjectNameEXT);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBeginDebugUtilsLabelEXT);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdEndDebugUtilsLabelEXT);*/
        // #endif

        // if (!vkCmdPushDescriptorSetWithTemplateKHR)
        //{
        //	vkCmdPushDescriptorSetWithTemplateKHR = (PFN_vkCmdPushDescriptorSetWithTemplateKHR)vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSetWithTemplate");
        //	if (!vkCmdPushDescriptorSetWithTemplateKHR)
        //	{
        //		KGLogPrintf(KGLOG_ERR,"Get device Proc:%s Addr failed", "vkCmdPushDescriptorSetWithTemplate");
        //	}
        // }

        // if (!vkCreateDescriptorUpdateTemplateKHR)
        //{
        //	vkCreateDescriptorUpdateTemplateKHR = (PFN_vkCreateDescriptorUpdateTemplateKHR)vkGetDeviceProcAddr(device, "vkCreateDescriptorUpdateTemplate");
        //	if (!vkCreateDescriptorUpdateTemplateKHR)
        //	{
        //		KGLogPrintf(KGLOG_ERR,"Get device Proc:%s Addr failed", "vkCreateDescriptorUpdateTemplate");
        //	}
        // }
        /***************************** ray tracing ************************************/
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateAccelerationStructureKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkDestroyAccelerationStructureKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBuildAccelerationStructuresKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetAccelerationStructureBuildSizesKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetAccelerationStructureDeviceAddressKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdTraceRaysKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdTraceRaysIndirectKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdTraceRaysIndirect2KHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCreateRayTracingPipelinesKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetRayTracingShaderGroupHandlesKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdWriteAccelerationStructuresPropertiesKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyAccelerationStructureKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdBuildAccelerationStructuresIndirectKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkBuildAccelerationStructuresKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCopyAccelerationStructureKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCopyAccelerationStructureToMemoryKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCopyMemoryToAccelerationStructureKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkWriteAccelerationStructuresPropertiesKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyAccelerationStructureToMemoryKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkCmdCopyMemoryToAccelerationStructureKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetDeviceAccelerationStructureCompatibilityKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetRayTracingShaderGroupStackSizeKHR);
        /********************************************************************************/
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetBufferDeviceAddressKHR);
        PVR_VULKAN_GET_DEVICE_POINTER(device, vkGetDescriptorSetLayoutSupport);

    }

    void FreeVulkanLibrary()
    {
        if (libVulkan)
        {
#if defined(_WIN32)
            FreeLibrary((HMODULE)libVulkan);
#endif

#if defined(_LINUX) || defined(_ANDROID)
            dlclose(libVulkan);
#endif
        }
    }


    void GetDeviceConfig()
    {
#if defined(__ANDROID__)
        // Screen densityl
        if (android_native_activity)
        {
            AConfiguration* config = AConfiguration_new();
            AConfiguration_fromAssetManager(config, android_native_activity->assetManager);
            vkfunc::screenDensity = AConfiguration_getDensity(config);
            AConfiguration_delete(config);
        }
#endif
    }


    int bApiInited = false;
    int IsApiInited()
    {
        return bApiInited;
    }
    void SetApiInited()
    {
        bApiInited = true;
    }
} // namespace vkfunc

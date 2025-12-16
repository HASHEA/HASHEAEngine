#include "KVulkanDebug.h"
#include "KVulkanFunc.h"
#include "KVulkanCommandBuffer.h"
#include "Engine/KGLog.h"
#include <sstream>
#include "GFXVulkan.h"
#include "KBase/Public/KMemLeak.h"

namespace vks
{
    namespace debug
    {
        // for android
        // https://developer.android.com/ndk/guides/graphics/validation-layer
        // https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases
        PFN_vkCreateDebugUtilsMessengerEXT  CreateDebugUtilsMessageCallBack  = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT DestoryDebugUtilsMessageCallBack = VK_NULL_HANDLE;

        VkDebugUtilsMessengerEXT debugUtilsMessenger = VK_NULL_HANDLE;

        // for win32
        PFN_vkCreateDebugReportCallbackEXT  CreateDebugReportCallback  = VK_NULL_HANDLE;
        PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallback = VK_NULL_HANDLE;
        PFN_vkDebugReportMessageEXT         dbgBreakCallback           = VK_NULL_HANDLE;

        VkDebugReportCallbackEXT msgCallback = VK_NULL_HANDLE;

        // 老的接口，对应的是VK_EXT_DEBUG_REPORT_EXTENSION_NAME扩展
        VKAPI_ATTR VkBool32 VKAPI_CALL MessageCallback_Report(
            VkDebugReportFlagsEXT      flags,
            VkDebugReportObjectTypeEXT objType,
            uint64_t                   srcObject,
            size_t                     location,
            int32_t                    msgCode,
            const char*                pLayerPrefix,
            const char*                pMsg,
            void*                      pUserData
        )
        {
            if (pMsg)
            {
                if (strstr(pMsg, "-02699"))
                {
                    // https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#VUID-vkCmdDraw-None-02699
                    // 应该是shader真的不需使用某个binding里面的数据，就没有绑定，但shader却声明了这个binding的数据绑定，就会出现这个错误提示，忽略就好了,windows上的不会报错
                    return false;
                }
                else if (strstr(pMsg, "-01413"))
                {
                    // 联发科GPU 提示 fillModeNonSolid不支持，就不能设置成VK_POLYGON_MODE_POINT or VK_POLYGON_MODE_LINE 填充
                    //  华为鸿蒙系统也会报，但是在驱动层报，没有传上来
                    return false;
                }
            }

            // Select prefix depending on flags passed to the callback
            // Note that multiple flags may be set for a single validation message
            std::string prefix("");

            // Error that may result in undefined behaviour
            if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
            {
                prefix += "ERROR:";
            };
            // Warnings may hint at unexpected / non-spec API usage
            if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
            {
                prefix += "WARNING:";
            };
            // May indicate sub-optimal usage of the API
            if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
            {
                prefix += "PERFORMANCE:";
            };
            // Informal messages that may become handy during debugging
            if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
            {
                prefix += "INFO:";
            }
            // Diagnostic info from the Vulkan loader and layers
            // Usually not helpful in terms of API usage, but may help to debug layer and loader problems
            if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
            {
                prefix += "DEBUG:";
            }

            // Display message to default output (console/logcat)
            std::stringstream debugMessage;
            debugMessage << prefix << " [" << pLayerPrefix << "] Code " << msgCode << " : " << (pMsg ? pMsg : "");

            // #if defined(__ANDROID__)
            if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
            {
                KGLogPrintf(KGLOG_ERR, "%s", debugMessage.str().c_str());
                int x = 0;
            }
            else
            {
                KGLogPrintf(KGLOG_DEBUG, "%s", debugMessage.str().c_str());
            }
            // #else
            //           if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
            //               std::cerr << debugMessage.str() << "\n";
            //           }
            //           else {
            //               std::cout << debugMessage.str() << "\n";
            //           }
            // #endif

            fflush(stdout);

            // The return value of this callback controls wether the Vulkan call that caused
            // the validation message will be aborted or not
            // We return VK_FALSE as we DON'T want Vulkan calls that cause a validation message
            // (and return a VkResult) to abort
            // If you instead want to have calls abort, pass in VK_TRUE and the function will
            // return VK_ERROR_VALIDATION_FAILED_EXT
            return VK_FALSE;
        }

        // 新的接口，对应的是VK_EXT_DEBUG_UTILS_EXTENSION_NAME扩展
        VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessenger_Util(
            VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void*                                       userData
        )
        {
            const char  validation[]      = "Validation";
            const char  performance[]     = "Performance";
            const char  error[]           = "ERROR";
            const char  warning[]         = "WARNING";
            const char  unknownType[]     = "UNKNOWN_TYPE";
            const char  unknownSeverity[] = "UNKNOWN_SEVERITY";
            const char* typeString        = unknownType;
            const char* severityString    = unknownSeverity;
            const char* messageIdName     = callbackData->pMessageIdName;
            int32_t     messageIdNumber   = callbackData->messageIdNumber;
            const char* message           = callbackData->pMessage;

            if (messageIdName)
            {
                if (strstr(messageIdName, "-02699"))
                {
                    // https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#VUID-vkCmdDraw-None-02699
                    // 应该是shader真的不需使用某个binding里面的数据，就没有绑定，但shader却声明了这个binding的数据绑定，就会出现这个错误提示，忽略就好了,windows上的不会报错
                    return false;
                }
                else if (strstr(messageIdName, "-01413"))
                {
                    // 联发科GPU 提示 fillModeNonSolid不支持，就不能设置成VK_POLYGON_MODE_POINT or VK_POLYGON_MODE_LINE 填充
                    //  华为鸿蒙系统也会报，但是在驱动层报，没有传上来
                    return false;
                }
                else if (strstr(messageIdName, "-06341"))
                {
                    // 骁龙870会提示：Shader requires vertexPipelineStoresAndAtomics but is not enabled on the device The Vulkan spec states:
                    // If vertexPipelineStoresAndAtomics is not enabled, then all storage image, storage texel buffer,
                    // and storage buffer variables in the vertex, tessellation, and geometry stages must be decorated with the NonWritable decoration.
                    return false;
                }
            }

            if (message)
            {
                if (strstr(message, "VKDBGUTILWARN"))
                {
                    // The following warning was triggered: VKDBGUTILWARN003. Please refer to the Adreno Game Developer Guide for more information: https://developer.qualcomm.com/docs/adreno-gpu/developer-guide/index.html"
                    // 高通某些机器会报，实际上只是warning
                    return false;
                }
            }

            if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                if (message && strstr(message, "Expected Image to have the same type as Result Type Image") && strstr(message, " = OpSampledImage %"))
                {
                    // 怀疑是深度用了贴图数组采样器误报的错误
                    return false;
                }

                // win32 这里加断点
                //  priority = ANDROID_LOG_ERROR;
                KGLogPrintf(KGLOG_ERR, "%s", message);
                severityString = error;
            }
            else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                // priority = ANDROID_LOG_WARN;
                KGLogPrintf(KGLOG_WARNING, "%s", message);
                severityString = warning;
            }
            if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
            {
                KGLogPrintf(KGLOG_WARNING, "%s", message);
                typeString = validation;
            }
            else if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
            {
                KGLogPrintf(KGLOG_WARNING, "%s", message);
                typeString = performance;
            }

            //__android_log_print(priority,
            //  "AppName",
            //  "%s %s: [%s] -Code %i : %s",
            //  typeString,
            //  severityString,
            //  messageIdName,
            //  messageIdNumber,
            //  message);

            // if (priority == ANDROID_LOG_ERROR)
            //{
            //   int x = 0;
            // }

            // Returning false tells the layer not to stop when the event occurs, so
            // they see the same behavior with and without validation layers enabled.
            return VK_FALSE;
        }

        void SetupDebugging(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportCallbackEXT callBack, bool bSupportUtilsExtension)
        {
            // win32新的老的方式都能用，可以换着用，苹果，目前还是用老的方式
            // 用特性进行
            if (bSupportUtilsExtension)
            {
                CreateDebugUtilsMessageCallBack =
                    (PFN_vkCreateDebugUtilsMessengerEXT)vks::vkGetInstanceProcAddr(
                        instance, "vkCreateDebugUtilsMessengerEXT"
                    );
                DestoryDebugUtilsMessageCallBack =
                    (PFN_vkDestroyDebugUtilsMessengerEXT)vks::vkGetInstanceProcAddr(
                        instance, "vkDestroyDebugUtilsMessengerEXT"
                    );

                if (CreateDebugUtilsMessageCallBack)
                {
                    VkDebugUtilsMessengerCreateInfoEXT            messengerInfo;
                    constexpr VkDebugUtilsMessageSeverityFlagsEXT kSeveritiesToLog =
                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

                    constexpr VkDebugUtilsMessageTypeFlagsEXT kMessagesToLog =
                        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

                    messengerInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                    messengerInfo.pNext           = nullptr;
                    messengerInfo.flags           = 0;
                    messengerInfo.messageSeverity = kSeveritiesToLog;
                    messengerInfo.messageType     = kMessagesToLog;

                    // The DebugUtilsMessenger callback is explained in the following section.
                    messengerInfo.pfnUserCallback = &DebugUtilsMessenger_Util;
                    messengerInfo.pUserData       = nullptr; // Custom user data passed to callback

                    CreateDebugUtilsMessageCallBack(instance, &messengerInfo, nullptr, &debugUtilsMessenger);
                }
            }
            else
            {
                CreateDebugReportCallback  = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vks::vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
                DestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vks::vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
                dbgBreakCallback           = reinterpret_cast<PFN_vkDebugReportMessageEXT>(vks::vkGetInstanceProcAddr(instance, "vkDebugReportMessageEXT"));

                if (CreateDebugReportCallback)
                {
                    VkDebugReportCallbackCreateInfoEXT dbgCreateInfo = {};
                    dbgCreateInfo.sType                              = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
                    dbgCreateInfo.pfnCallback                        = (PFN_vkDebugReportCallbackEXT)MessageCallback_Report;
                    dbgCreateInfo.flags                              = flags;

                    VkResult err = CreateDebugReportCallback(
                        instance,
                        &dbgCreateInfo,
                        nullptr,
                        (callBack != VK_NULL_HANDLE) ? &callBack : &msgCallback
                    );
                    assert(!err);
                }
            }
        }

        void FreeDebugCallback(VkInstance instance)
        {
            if (DestroyDebugReportCallback && msgCallback)
            {
                DestroyDebugReportCallback(instance, msgCallback, nullptr);
            }

            if (DestoryDebugUtilsMessageCallBack && debugUtilsMessenger)
            {
                DestoryDebugUtilsMessageCallBack(instance, debugUtilsMessenger, nullptr);
            }
        }
    } // namespace debug

    namespace debugmarker
    {
        bool active = false;

        PFN_vkDebugMarkerSetObjectTagEXT  pfnDebugMarkerSetObjectTag  = VK_NULL_HANDLE;
        PFN_vkDebugMarkerSetObjectNameEXT pfnDebugMarkerSetObjectName = VK_NULL_HANDLE;
        PFN_vkCmdDebugMarkerBeginEXT      pfnCmdDebugMarkerBegin      = VK_NULL_HANDLE;
        PFN_vkCmdDebugMarkerEndEXT        pfnCmdDebugMarkerEnd        = VK_NULL_HANDLE;
        PFN_vkCmdDebugMarkerInsertEXT     pfnCmdDebugMarkerInsert     = VK_NULL_HANDLE;
        VkDevice                          pDevice                     = nullptr;
        void                              Setup(VkDevice device)
        {
            pfnDebugMarkerSetObjectTag  = reinterpret_cast<PFN_vkDebugMarkerSetObjectTagEXT>(vks::vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectTagEXT"));
            pfnDebugMarkerSetObjectName = reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(vks::vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT"));
            pfnCmdDebugMarkerBegin      = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(vks::vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT"));
            pfnCmdDebugMarkerEnd        = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(vks::vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT"));
            pfnCmdDebugMarkerInsert     = reinterpret_cast<PFN_vkCmdDebugMarkerInsertEXT>(vks::vkGetDeviceProcAddr(device, "vkCmdDebugMarkerInsertEXT"));

            // Set flag if at least one function pointer is present
            active = (pfnDebugMarkerSetObjectName != VK_NULL_HANDLE);
        }

        void SetObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char* name)
        {
            // Check for valid function pointer (may not be present if not running in a debugging application)
            if (pfnDebugMarkerSetObjectName)
            {
                VkDebugMarkerObjectNameInfoEXT nameInfo = {};
                nameInfo.sType                          = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
                nameInfo.objectType                     = objectType;
                nameInfo.object                         = object;
                nameInfo.pObjectName                    = name;
                pfnDebugMarkerSetObjectName(device, &nameInfo);
            }
        }

        void SetObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void* tag)
        {
            // Check for valid function pointer (may not be present if not running in a debugging application)
            if (pfnDebugMarkerSetObjectTag)
            {
                VkDebugMarkerObjectTagInfoEXT tagInfo = {};
                tagInfo.sType                         = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT;
                tagInfo.objectType                    = objectType;
                tagInfo.object                        = object;
                tagInfo.tagName                       = name;
                tagInfo.tagSize                       = tagSize;
                tagInfo.pTag                          = tag;
                pfnDebugMarkerSetObjectTag(device, &tagInfo);
            }
        }

        void _BeginRegion(VkCommandBuffer cmdbuffer, const char* pMarkerName, NSKMath::KVec4& color)
        {
            // Check for valid function pointer (may not be present if not running in a debugging application)
            if (pfnCmdDebugMarkerBegin)
            {
                VkDebugMarkerMarkerInfoEXT markerInfo = {};
                markerInfo.sType                      = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
                memcpy(markerInfo.color, &color, sizeof(float) * 4);
                markerInfo.pMarkerName = pMarkerName;
                pfnCmdDebugMarkerBegin(cmdbuffer, &markerInfo);
            }
        }

        void _Insert(VkCommandBuffer cmdbuffer, std::string markerName, NSKMath::KVec4& color)
        {
            // Check for valid function pointer (may not be present if not running in a debugging application)
            if (pfnCmdDebugMarkerInsert)
            {
                VkDebugMarkerMarkerInfoEXT markerInfo = {};
                markerInfo.sType                      = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
                memcpy(markerInfo.color, &color, sizeof(float) * 4);
                markerInfo.pMarkerName = markerName.c_str();
                pfnCmdDebugMarkerInsert(cmdbuffer, &markerInfo);
            }
        }

        void _EndRegion(VkCommandBuffer cmdBuffer)
        {
            // Check for valid function (may not be present if not running in a debugging application)
            if (pfnCmdDebugMarkerEnd)
            {
                pfnCmdDebugMarkerEnd(cmdBuffer);
            }
        }

        // Start a new debug marker region
        void BeginRegion(gfx::KVulkanCommandBuffer* cmdbuffer, const char* pMarkerName, NSKMath::KVec4& color)
        {
            gfx::KVulkanCommandBuffer* pCmd = (gfx::KVulkanCommandBuffer*)cmdbuffer;
            _BeginRegion(pCmd->GetCommandBuffer(), pMarkerName, color);
        }

        // Insert a new debug marker into the command buffer
        void Insert(gfx::KVulkanCommandBuffer* cmdbuffer, std::string markerName, NSKMath::KVec4& color)
        {
            gfx::KVulkanCommandBuffer* pCmd = (gfx::KVulkanCommandBuffer*)cmdbuffer;
            _Insert(pCmd->GetCommandBuffer(), markerName, color);
        }

        // End the current debug marker region
        void EndRegion(gfx::KVulkanCommandBuffer* cmdBuffer)
        {
            gfx::KVulkanCommandBuffer* pCmd = (gfx::KVulkanCommandBuffer*)cmdBuffer;
            _EndRegion(pCmd->GetCommandBuffer());
        }

        void SetCommandBufferName(VkDevice device, VkCommandBuffer cmdBuffer, const char* name)
        {
            SetObjectName(device, (uint64_t)cmdBuffer, VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, name);
        }

        void SetQueueName(VkDevice device, VkQueue queue, const char* name)
        {
            SetObjectName(device, (uint64_t)queue, VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, name);
        }

        void SetImageName(VkDevice device, VkImage image, const char* name)
        {
            SetObjectName(device, (uint64_t)image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, name);
        }

        void SetSamplerName(VkDevice device, VkSampler sampler, const char* name)
        {
            SetObjectName(device, (uint64_t)sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, name);
        }

        void SetBufferName(VkDevice device, VkBuffer buffer, const char* name)
        {
            SetObjectName(device, (uint64_t)buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, name);
        }

        void SetDeviceMemoryName(VkDevice device, VkDeviceMemory memory, const char* name)
        {
            SetObjectName(device, (uint64_t)memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, name);
        }

        void SetShaderModuleName(VkDevice device, VkShaderModule shaderModule, const char* name)
        {
            SetObjectName(device, (uint64_t)shaderModule, VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, name);
        }

        void SetPipelineName(VkDevice device, VkPipeline pipeline, const char* name)
        {
            SetObjectName(device, (uint64_t)pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, name);
        }

        void SetPipelineLayoutName(VkDevice device, VkPipelineLayout pipelineLayout, const char* name)
        {
            SetObjectName(device, (uint64_t)pipelineLayout, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT, name);
        }

        void SetRenderPassName(VkDevice device, VkRenderPass renderPass, const char* name)
        {
            SetObjectName(device, (uint64_t)renderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, name);
        }

        void SetFramebufferName(VkDevice device, VkFramebuffer framebuffer, const char* name)
        {
            SetObjectName(device, (uint64_t)framebuffer, VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, name);
        }

        void SetDescriptorSetLayoutName(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const char* name)
        {
            SetObjectName(device, (uint64_t)descriptorSetLayout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, name);
        }

        void SetDescriptorSetName(VkDevice device, VkDescriptorSet descriptorSet, const char* name)
        {
            SetObjectName(device, (uint64_t)descriptorSet, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, name);
        }

        void SetSemaphoreName(VkDevice device, VkSemaphore semaphore, const char* name)
        {
            SetObjectName(device, (uint64_t)semaphore, VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT, name);
        }

        void SetFenceName(VkDevice device, VkFence fence, const char* name)
        {
            SetObjectName(device, (uint64_t)fence, VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT, name);
        }

        void SetEventName(VkDevice device, VkEvent _event, const char* name)
        {
            SetObjectName(device, (uint64_t)_event, VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT, name);
        }
    }; // namespace debugmarker
} // namespace vks

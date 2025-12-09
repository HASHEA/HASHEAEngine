#pragma once
#include "Base/hlog.h"
#include "Base/hfile.h"
#include "Base/hcache.h"
#include "VulkanContext.h"
#include "VulkanCommandPool.h"
#include "VulkanCommandBuffer.h"
#include "VulkanFence.h"
#include "GpuProfiler.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanShader.h"
#include "VulkanStagingBuffer.h"
#include <vector>
namespace RHI
{
	constexpr const char* k_pipeline_cache_path = "product\\caches\\PipelineCaches\\AshVulkanPipelineCache.pipelineCacheVK";
	inline static auto check_layer_support(const std::vector<const char*>& rqLayers)->bool
	{
		std::vector<VkLayerProperties> layers;
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		layers.resize(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

		for (const char* layerName : rqLayers)
		{
			bool layerFound = false;
			for (const auto& layerProperties : layers)
			{
				if (strcmp(layerName, layerProperties.layerName) == 0)
				{
					layerFound = true;
					break;
				}
			}
			if (!layerFound)
			{
				return false;
			}
		}
		return true;
	}
	inline static auto check_extension_support(const std::vector<const char*>& rqExtensions) -> bool
	{
		std::vector<VkExtensionProperties> properties;
		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

		properties.resize(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, properties.data());

		bool extensionSupported = true;
		int size = rqExtensions.size();
		for (int i = 0; i < size; i++)
		{
			const char* extensionName = rqExtensions[i];
			bool        layerFound = false;

			for (const auto& layerProperties : properties)
			{
				if (strcmp(extensionName, layerProperties.extensionName) == 0)
				{
					layerFound = true;
					break;
				}
			}

			if (!layerFound)
			{
				extensionSupported = false;
				HLogError("Extension not supported {0}", extensionName);
			}
		}
		return extensionSupported  ? true : false;
	}
	static VkBool32 vk_debug_callbacks(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT types,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data) {
		bool triggerBreak = severity & (/*VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |*/ VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
		triggerBreak = triggerBreak && (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT);
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
		{
			HLogInfo("[Vulkan Validation] - VERBOSE : MessageID: {0} {1}\nMessage: {2}\n\n", callback_data->pMessageIdName, callback_data->messageIdNumber, callback_data->pMessage);
		}
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		{
			HLogInfo("[Vulkan Validation] - INFO : MessageID: {0} {1}\nMessage: {2}\n\n", callback_data->pMessageIdName, callback_data->messageIdNumber, callback_data->pMessage);

		}
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			HLogWarning("[Vulkan Validation] - WARNING : MessageID: {0} {1}\nMessage: {2}\n\n", callback_data->pMessageIdName, callback_data->messageIdNumber, callback_data->pMessage);

		}
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			HLogError("[Vulkan Validation] - ERROR : MessageID: {0} {1}\nMessage: {2}\n\n", callback_data->pMessageIdName, callback_data->messageIdNumber, callback_data->pMessage);

		}
		if (triggerBreak) {
			H_ASSERT(false);
		}
		return VK_FALSE;
	}

	auto VulkanContext::set_resource_name_internal(VkObjectType type, uint64_t handle, const char* name) -> void
	{
#ifdef VULKAN_DEBUG_REPORT
		VkDebugUtilsObjectNameInfoEXT name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		name_info.objectType = type;
		name_info.objectHandle = handle;
		name_info.pObjectName = name;
		vkSetDebugUtilsObjectNameEXT(vulkanDevice, &name_info);
#endif // VULKAN_DEBUG_REPORT
	}

	auto VulkanContext::_create_instance(const Array<const char*>& additionalExtensions) -> bool
	{
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pNext = nullptr;
		appInfo.pApplicationName = "Ash Engine";
		appInfo.applicationVersion = 1;
		appInfo.pEngineName = "Ash";
		appInfo.engineVersion = 1;
		appInfo.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
		std::vector<const char*> rqLayers{};
#ifdef VULKAN_DEBUG_REPORT
		rqLayers.push_back("VK_LAYER_KHRONOS_validation");
			//"VK_LAYER_AMD_switchable_graphics",
			//"VK_LAYER_NV_optimus",
			//"VK_LAYER_LUNARG_core_validation",
			//"VK_LAYER_LUNARG_image",
			//"VK_LAYER_LUNARG_parameter_validation",
			//"VK_LAYER_LUNARG_object_tracker"
#endif
		bool result = check_layer_support(rqLayers);
		if (!result)
		{
			HLogError("Not all required layers are supported!");
			return result;
		}
		std::vector<const char*> rqExtensions{};
#ifdef VK_USE_PLATFORM_WIN32_KHR
		rqExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
		rqExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		rqExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		rqExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
		rqExtensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		rqExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
		rqExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif (defined(VK_USE_PLATFORM_MIR_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR))
		rqExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		rqExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
		rqExtensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VULKAN_DEBUG_REPORT
		rqExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		rqExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif // VULKAN_DEBUG_REPORT
		//insert additional extensions
		for (uint32_t i = 0; i < additionalExtensions.size(); i++)
		{
			rqExtensions.push_back(additionalExtensions[i]);
		}
		result = check_extension_support(rqExtensions);
		if (!result)
		{
			HLogError("Not all required extensions are supported!");
			return result;
		}
		VkInstanceCreateInfo instanceCI{};
		instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instanceCI.pNext = nullptr;
		instanceCI.flags = 0;
		instanceCI.pApplicationInfo = &appInfo;
		auto lyorExCounts = rqLayers.size();
		instanceCI.enabledLayerCount = lyorExCounts;
		instanceCI.ppEnabledLayerNames = lyorExCounts == 0 ? nullptr : rqLayers.data();
		lyorExCounts = rqExtensions.size();
		instanceCI.enabledExtensionCount = lyorExCounts;
		instanceCI.ppEnabledExtensionNames = lyorExCounts == 0 ? nullptr : rqExtensions.data();
#ifdef VULKAN_DEBUG_REPORT
		VkDebugUtilsMessengerCreateInfoEXT debugUtilCI = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugUtilCI.pfnUserCallback = vk_debug_callbacks;
		debugUtilCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
		debugUtilCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
#ifdef VULKAN_SYNCHRONIZATION_VALIDATION
		const VkValidationFeatureEnableEXT featuresRequested[] = { VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT, VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT/*, VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT*/ };
		VkValidationFeaturesEXT features = {};
		features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
		features.pNext = &debugUtilCI;
		features.enabledValidationFeatureCount = _countof(featuresRequested);
		features.pEnabledValidationFeatures = featuresRequested;
		instanceCI.pNext = &features;
#else
		instanceCI.pNext = &debugUtilCI;
#endif // VULKAN_SYNCHRONIZATION_VALIDATION
#endif // VULKAN_DEBUG_REPORT
		VK_CHECK_RESULT(vkCreateInstance(&instanceCI, vulkanAllocationCallbacks, &vulkanInstance));
		//load instance apis
		volkLoadInstance(vulkanInstance);
		return true;
	}
	auto VulkanContext::_shutdown_instance() -> bool
	{
		if (vulkanInstance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(vulkanInstance, vulkanAllocationCallbacks);
			vulkanInstance = VK_NULL_HANDLE;
		}
		return true;
	}
#ifdef VULKAN_DEBUG_REPORT
	auto VulkanContext::_create_debug_util_messenger_ext() -> bool
	{
		// Create new debug utils callback
		VkDebugUtilsMessengerCreateInfoEXT debugUtilCI = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugUtilCI.pfnUserCallback = vk_debug_callbacks;
		debugUtilCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
		debugUtilCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;

		VK_CHECK_RESULT(vkCreateDebugUtilsMessengerEXT(vulkanInstance, &debugUtilCI, vulkanAllocationCallbacks, &vulkanDebugUtilMessenger));
		return true;
	}
	auto VulkanContext::_shutdown_debug_util_messenger_ext() -> bool
	{
		if (vulkanDebugUtilMessenger != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(vulkanInstance, vulkanDebugUtilMessenger, vulkanAllocationCallbacks);
			vulkanDebugUtilMessenger = VK_NULL_HANDLE;
		}
		return true;
	}
#endif

	auto VulkanContext::_select_and_prepare_physical_device() -> bool
	{
		uint32_t numPhysicalDevice = 0;
		VK_CHECK_RESULT(vkEnumeratePhysicalDevices(vulkanInstance, &numPhysicalDevice, NULL));
		H_ASSERTLOG(numPhysicalDevice > 0, "No GPU Found !");
		std::vector<VkPhysicalDevice> physicalDevices{};
		physicalDevices.resize(numPhysicalDevice);
		VK_CHECK_RESULT(vkEnumeratePhysicalDevices(vulkanInstance, &numPhysicalDevice, physicalDevices.data()));
		bool retCode = false;
		for (VkPhysicalDevice device : physicalDevices)
		{
			vkGetPhysicalDeviceProperties(device, &vulkanPhysicalDeviceProperties);
			if (vulkanPhysicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				vulkanPhysicalDevice = device;
				retCode = true;
				break;
			}
		}
		if (FAILED(retCode))
		{
			HLogError("No suitable GPU found!");
			return retCode;
		}
		HLogInfo("GPU used : {}", vulkanPhysicalDeviceProperties.deviceName);
		gpuTimestampFrequency = vulkanPhysicalDeviceProperties.limits.timestampPeriod / (1000*1000);
		//query memory props
		_query_device_memory_props();

		_filter_device_selectable_extension();
		//Query Properties Supported
		_query_supported_props();
		//Query Feature supported
		_query_supported_features();
		return retCode;
	}

	auto VulkanContext::_filter_device_selectable_extension() -> bool
	{
		uint32_t deviceExtensionCount = 0;
		vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, nullptr, &deviceExtensionCount, nullptr);
		H_ASSERT(deviceExtensionCount > 0);
		std::vector<VkExtensionProperties> extensions;
		extensions.resize(deviceExtensionCount);
		vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, nullptr, &deviceExtensionCount, extensions.data());
		for (size_t i = 0; i < deviceExtensionCount; i++) {

			if (!strcmp(extensions[i].extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::DynamicRendering);
				continue;
			}

			if (!strcmp(extensions[i].extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::TimelineSemaphore);
				continue;
			}

			if (!strcmp(extensions[i].extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::Synchronization2);
				continue;
			}

			if (!strcmp(extensions[i].extensionName, VK_NV_MESH_SHADER_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::MeshShaders);
				continue;
			}

			if (!strcmp(extensions[i].extensionName, VK_KHR_MULTIVIEW_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::Multiview);
				continue;
			}

			if (!strcmp(extensions[i].extensionName, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::FragmentShadingRate);
				continue;
			}

			if (!strcmp(extensions[i].extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::RayTracing);
				continue;
			}

			if (!strcmp(extensions[i].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
				featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::RayQuery);
				continue;
			}
		}
		return true;
	}

	auto VulkanContext::_query_device_memory_props() -> bool
	{
		{
			// Memory properties are used regularly for creating all kinds of buffers
			vkGetPhysicalDeviceMemoryProperties(vulkanPhysicalDevice, &vulkanPhysicalDeviceMemoryProperties);
			float fGB = 0.0f;
			bool  bLocalGpuMemory = false;
			for (uint32_t i = 0; i < vulkanPhysicalDeviceMemoryProperties.memoryHeapCount && i < VK_MAX_MEMORY_HEAPS; ++i)
			{
				if (vulkanPhysicalDeviceMemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				{
					fGB += (float)((double)vulkanPhysicalDeviceMemoryProperties.memoryHeaps[i].size / (double)(1024 * 1024 * 1024));
					bLocalGpuMemory = true;
				}
			}

			if (bLocalGpuMemory)
			{
				local_gpu_memory_gb = fGB;
			}

			HLogInfo("VM size:%.2f", fGB);

			for (uint32_t i = 0; i < vulkanPhysicalDeviceMemoryProperties.memoryTypeCount; i++)
			{
				if ((vulkanPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
				{
					HLogInfo("Device support VK_MEMORY_PROPERTY_HOST_COHERENT_BIT");
					break;
				}
			}

			for (uint32_t i = 0; i < vulkanPhysicalDeviceMemoryProperties.memoryTypeCount && i < VK_MAX_MEMORY_TYPES; ++i)
			{
				if (vulkanPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
				{
					HLogInfo("Device support VK_MEMORY_PROPERTY_HOST_CACHED_BIT");
					break;
				}
			}

			for (uint32_t i = 0; i < vulkanPhysicalDeviceMemoryProperties.memoryTypeCount && i < VK_MAX_MEMORY_TYPES; ++i)
			{
				if ((vulkanPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
					(vulkanPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
				{
					HLogInfo("Device Support VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT");
					featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::HostCoherentCached);
					break;
				}
			}
		}
		return true;
	}

	auto VulkanContext::_query_supported_props() -> bool
	{
		VkPhysicalDeviceSubgroupProperties subGroupProperties{};
		subGroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
		subGroupProperties.pNext = VK_NULL_HANDLE;
		VkPhysicalDeviceSubgroupProperties subgroupProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
		subgroupProperties.pNext = NULL;
		void* physicalDevicePropertiesNext = nullptr;
		physicalDevicePropertiesNext = &subgroupProperties;
		fragmentShadingRateProperties = VkPhysicalDeviceFragmentShadingRatePropertiesKHR{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR };
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::FragmentShadingRate))
		{
			fragmentShadingRateProperties.pNext = physicalDevicePropertiesNext;
			physicalDevicePropertiesNext = &fragmentShadingRateProperties;
		}

		rayTracingPipelineProperties = VkPhysicalDeviceRayTracingPipelinePropertiesKHR{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::RayTracing))
		{
			rayTracingPipelineProperties.pNext = physicalDevicePropertiesNext;
			physicalDevicePropertiesNext = &rayTracingPipelineProperties;
		}
		VkPhysicalDeviceProperties2 physicalDeviceProperties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		physicalDeviceProperties2.pNext = physicalDevicePropertiesNext;
		vkGetPhysicalDeviceProperties2(vulkanPhysicalDevice, &physicalDeviceProperties2);
		subgroupSize = subGroupProperties.subgroupSize;
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::FragmentShadingRate))
		{
			minFragmentShadingRateTexelSize = fragmentShadingRateProperties.minFragmentShadingRateAttachmentTexelSize;
		}
		uboAlignment = vulkanPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
		ssboAlignemnt = vulkanPhysicalDeviceProperties.limits.minStorageBufferOffsetAlignment;
		maxFramebufferLayers = vulkanPhysicalDeviceProperties.limits.maxFramebufferLayers;
		return true;
	}

	auto VulkanContext::_query_supported_features() -> bool
	{
		// Query bindless extension, called Descriptor Indexing (https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_EXT_descriptor_indexing.html)
		VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
		indexingFeatures.pNext = nullptr;
		VkPhysicalDeviceFeatures2 deviceFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		deviceFeatures.pNext = &indexingFeatures;

		accelerationStructureFeatures = VkPhysicalDeviceAccelerationStructureFeaturesKHR{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
		rayTracingPipelineFeatures = VkPhysicalDeviceRayTracingPipelineFeaturesKHR{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
		rayQueryFeatures = VkPhysicalDeviceRayQueryFeaturesKHR{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::RayTracing))
		{
			indexingFeatures.pNext = &accelerationStructureFeatures;
			accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;
			rayTracingPipelineFeatures.pNext = &rayQueryFeatures;
		}
		vkGetPhysicalDeviceFeatures2(vulkanPhysicalDevice, &deviceFeatures);
		// For the feature to be correctly working, we need both the possibility to partially bind a descriptor,
		// as some entries in the bindless array will be empty, and SpirV runtime descriptors.
		if (indexingFeatures.descriptorBindingPartiallyBound && indexingFeatures.runtimeDescriptorArray)
		{
			featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::Bindless);
		}
		
		return true;
	}

	auto VulkanContext::_create_device() -> bool
	{
		//Create logical device
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(vulkanPhysicalDevice,&queueFamilyCount,nullptr);
		H_ASSERT(queueFamilyCount > 0);
		std::vector<VkQueueFamilyProperties> queueFamilies;
		queueFamilies.resize(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(vulkanPhysicalDevice, &queueFamilyCount, queueFamilies.data());
		uint32_t mainQueueFamilyIndex = UINT32_MAX, transferQueueFamilyIndex = UINT32_MAX, computeQueueFamilyIndex = UINT32_MAX, presentQueueFamilyIndex = UINT32_MAX;
		uint32_t computeQueueIndex = UINT32_MAX;
		for (uint32_t i = 0; i < queueFamilyCount; i++)
		{
			VkQueueFamilyProperties qf = queueFamilies[i];
			if (qf.queueCount == 0)
			{
				continue;
			}
#ifdef ASH_DEBUG
			HLogInfo("Family {0}, flags {1} queue count {2}\n", i, qf.queueFlags, qf.queueCount);
#endif // ASH_DEBUG
			// Search for a main queue that should be able to do all work (graphics, compute and transfer)
			if ((qf.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT )) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT )/* && mainQueueFamilyIndex == UINT32_MAX*/)
			{
				mainQueueFamilyIndex = i;
				H_ASSERT(((qf.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) == VK_QUEUE_SPARSE_BINDING_BIT));
				if (qf.queueCount > 1)
				{
					computeQueueFamilyIndex = i;
					computeQueueIndex = 1;
				}
				continue;
			}

			// Search for another compute queue if graphics queue exposes only one queue
			if ((qf.queueFlags & VK_QUEUE_COMPUTE_BIT) && computeQueueIndex == UINT32_MAX) {
				computeQueueFamilyIndex = i;
				computeQueueIndex = 0;
			}

			// Search for transfer queue
			if ((qf.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0 && (qf.queueFlags & VK_QUEUE_TRANSFER_BIT)/* && transferQueueFamilyIndex == UINT32_MAX*/) {
				transferQueueFamilyIndex = i;
				continue;
			}
		}
		vulkanMainQueueFamily = mainQueueFamilyIndex;
		vulkanComputeQueueFamily = computeQueueFamilyIndex;
		vulkanTransferQueueFamily = transferQueueFamilyIndex;

		std::vector<const char*> deviceExtension;
		deviceExtension.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		deviceExtension.push_back(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering))
		{
			deviceExtension.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::TimelineSemaphore))
		{
			deviceExtension.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2))
		{
			deviceExtension.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::MeshShaders))
		{
			deviceExtension.push_back(VK_NV_MESH_SHADER_EXTENSION_NAME);
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Multiview))
		{
			deviceExtension.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::FragmentShadingRate))
		{
			deviceExtension.push_back(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
			deviceExtension.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
			deviceExtension.push_back(VK_KHR_MAINTENANCE2_EXTENSION_NAME);
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::RayTracing))
		{
			deviceExtension.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
			deviceExtension.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
			deviceExtension.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
			deviceExtension.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
			deviceExtension.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::RayQuery))
		{
			deviceExtension.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
		}

		const float queuePriority[] = {1.f,1.f};
		VkDeviceQueueCreateInfo queueInfo[3] = {};
		uint32_t queueCount = 0;
		VkDeviceQueueCreateInfo& mainQueueCI = queueInfo[queueCount++];
		mainQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		mainQueueCI.queueFamilyIndex = mainQueueFamilyIndex;
		mainQueueCI.queueCount = computeQueueFamilyIndex == mainQueueFamilyIndex ? 2 : 1;
		mainQueueCI.pQueuePriorities = queuePriority;

		if (computeQueueFamilyIndex != mainQueueFamilyIndex) {
			VkDeviceQueueCreateInfo& computeQueueCI = queueInfo[queueCount++];
			computeQueueCI.queueFamilyIndex = computeQueueFamilyIndex;
			computeQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			computeQueueCI.queueCount = 1;
			computeQueueCI.pQueuePriorities = queuePriority;
		}

		if (vulkanTransferQueueFamily < queueFamilyCount) {
			VkDeviceQueueCreateInfo& transferQueueCI = queueInfo[queueCount++];
			transferQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			transferQueueCI.queueFamilyIndex = transferQueueFamilyIndex;
			transferQueueCI.queueCount = 1;
			transferQueueCI.pQueuePriorities = queuePriority;
		}
		//enable all features
		VkPhysicalDeviceFeatures2 physicalFeatures2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		VkPhysicalDeviceVulkan11Features vulkan11Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		VkPhysicalDeviceVulkan12Features vulkan12Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		//VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		void* currentPNext = &vulkan11Features;
		vulkan12Features.pNext = currentPNext;
		currentPNext = &vulkan12Features;
		//vulkan13Features.pNext = currentPNext;
		//currentPNext = &vulkan13Features;
		VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering)) {
			dynamicRenderingFeatures.pNext = currentPNext;
			currentPNext = &dynamicRenderingFeatures;
		}

		VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2)) {
			synchronization2_features.pNext = currentPNext;
			currentPNext = &synchronization2_features;
		}

		VkPhysicalDeviceMeshShaderFeaturesNV mesh_shaders_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV };
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::MeshShaders)) {
			mesh_shaders_features.taskShader = true;
			mesh_shaders_features.meshShader = true;

			mesh_shaders_features.pNext = currentPNext;
			currentPNext = &mesh_shaders_features;
		}

		VkPhysicalDeviceFragmentShadingRateFeaturesKHR shading_rate_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::FragmentShadingRate)) {
			shading_rate_features.attachmentFragmentShadingRate = true;
			shading_rate_features.pipelineFragmentShadingRate = true;

			shading_rate_features.pNext = currentPNext;
			currentPNext = &shading_rate_features;
		}

		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::RayTracing)) {
			rayTracingPipelineFeatures.pNext = &accelerationStructureFeatures;
			accelerationStructureFeatures.pNext = currentPNext;

			currentPNext = &rayTracingPipelineFeatures;
		}

		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::RayQuery)) {
			rayQueryFeatures.pNext = currentPNext;
			currentPNext = &rayQueryFeatures;
		}

		physicalFeatures2.pNext = currentPNext;
		vkGetPhysicalDeviceFeatures2(vulkanPhysicalDevice, &physicalFeatures2);
		H_ASSERT(physicalFeatures2.features.sparseBinding);
		H_ASSERT(physicalFeatures2.features.sparseResidencyImage3D);
		H_ASSERT(physicalFeatures2.features.sparseResidencyImage2D);
		H_ASSERT(vulkan11Features.shaderDrawParameters == VK_TRUE);
		VkDeviceCreateInfo deviceCI{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		deviceCI.queueCreateInfoCount = queueCount;
		deviceCI.pQueueCreateInfos = queueInfo;
		deviceCI.enabledExtensionCount = deviceExtension.size();
		deviceCI.ppEnabledExtensionNames = deviceExtension.data();
		deviceCI.pNext = &physicalFeatures2;
		VK_CHECK_RESULT(vkCreateDevice(vulkanPhysicalDevice,&deviceCI,vulkanAllocationCallbacks,&vulkanDevice));
		volkLoadDevice(vulkanDevice);
		vkGetDeviceQueue(vulkanDevice, mainQueueFamilyIndex, 0, &vulkanMainQueue);
		vkGetDeviceQueue(vulkanDevice, mainQueueFamilyIndex, 0, &vulkanPresentQueue);
		vkGetDeviceQueue(vulkanDevice, computeQueueFamilyIndex, computeQueueIndex, &vulkanComputeQueue);
		if (vulkanTransferQueueFamily < queueFamilyCount) {
			vkGetDeviceQueue(vulkanDevice, transferQueueFamilyIndex, 0, &vulkanTransferQueue);
		}
		return true;
	}

	auto VulkanContext::_shutdown_device() -> bool
	{
		if (vulkanDevice != VK_NULL_HANDLE)
		{
			vkDestroyDevice(vulkanDevice, vulkanAllocationCallbacks);
			vulkanDevice = VK_NULL_HANDLE;
		}
		
		return true;
	}

	auto VulkanContext::_create_vulkan_memory_allocator() -> bool
	{
		//load vma funcs
		VmaAllocatorCreateInfo allocatorCI = {};
		allocatorCI.physicalDevice = vulkanPhysicalDevice;
		allocatorCI.device = vulkanDevice;
		allocatorCI.instance = vulkanInstance;

		VmaVulkanFunctions fn{};
		fn.vkAllocateMemory = vkAllocateMemory;
		fn.vkBindBufferMemory = vkBindBufferMemory;
		fn.vkBindImageMemory = vkBindImageMemory;
		fn.vkCmdCopyBuffer = vkCmdCopyBuffer;
		fn.vkCreateBuffer = vkCreateBuffer;
		fn.vkCreateImage = vkCreateImage;
		fn.vkDestroyBuffer = vkDestroyBuffer;
		fn.vkDestroyImage = vkDestroyImage;
		fn.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
		fn.vkFreeMemory = vkFreeMemory;
		fn.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
		fn.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
		fn.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
		fn.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
		fn.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
		fn.vkMapMemory = vkMapMemory;
		fn.vkUnmapMemory = vkUnmapMemory;
		fn.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
		fn.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR;
		fn.vkBindImageMemory2KHR = 0;
		fn.vkBindBufferMemory2KHR = 0;
		fn.vkGetPhysicalDeviceMemoryProperties2KHR = 0;
		fn.vkGetImageMemoryRequirements2KHR = 0;
		fn.vkGetBufferMemoryRequirements2KHR = 0;
		allocatorCI.pVulkanFunctions = &fn;
		VK_CHECK_RESULT(vmaCreateAllocator(&allocatorCI, &vmaAllocator));

		return true;
	}

	auto VulkanContext::_shutdown_vulkan_memory_allocator() -> bool
	{
		vmaDestroyAllocator(vmaAllocator);
		return true;
	}

	auto VulkanContext::_create_descriptor_pool(const GpuDescriptorPoolCreation& dspci) -> bool
	{
		VkDescriptorPoolSize poolSizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, dspci
			.samplers },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, dspci.combinedImageSamplers },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, dspci.sampledImage },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, dspci.storageImage },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, dspci.uniformTexelBuffers },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, dspci.storageTexelBuffers },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, dspci.uniformBuffer },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dspci.storageBuffer },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, dspci.uniformBufferDynamic },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, dspci.storageBufferDynamic },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, dspci.inputAttachments }
		};
		VkDescriptorPoolCreateInfo CI = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		CI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		CI.maxSets = 4096;
		CI.poolSizeCount = (uint32_t)ArraySize(poolSizes);
		CI.pPoolSizes = poolSizes;
		VK_CHECK_RESULT(vkCreateDescriptorPool(vulkanDevice,&CI,vulkanAllocationCallbacks,&vulkanDescriptorPool));
		//if enable bindless
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Bindless))
		{
			VkDescriptorPoolSize poolSizesBindless[] =
			{
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_max_bindless_resources },
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, k_max_bindless_resources },
			};

			// Update after bind is needed here, for each binding and in the descriptor set layout creation.
			CI.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
			CI.maxSets = k_max_bindless_resources * ArraySize(poolSizesBindless);
			CI.poolSizeCount = (uint32_t)ArraySize(poolSizesBindless);
			CI.pPoolSizes = poolSizesBindless;
			VK_CHECK_RESULT(vkCreateDescriptorPool(vulkanDevice, &CI, vulkanAllocationCallbacks, &vulkanBindlessDescriptorPool));
		}
		return true;
	}

	auto VulkanContext::_shutdown_descriptor_pool() -> bool
	{
		if (vulkanDescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(vulkanDevice, vulkanDescriptorPool, vulkanAllocationCallbacks);
			vulkanDescriptorPool = VK_NULL_HANDLE;
		}	
		//if enable bindless
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Bindless))
		{
			if (vulkanBindlessDescriptorPool != VK_NULL_HANDLE)
			{
				vkDestroyDescriptorPool(vulkanDevice, vulkanBindlessDescriptorPool, vulkanAllocationCallbacks);
				vulkanBindlessDescriptorPool = VK_NULL_HANDLE;
			}
		}
		return true;
	}

	auto VulkanContext::_create_frame_pool_and_data(uint16_t numThread, uint16_t numQueryTimes) -> bool
	{
		num_thread = numThread;
		const uint32_t num_pools = numThread * k_max_frames;
		framePools.init(nullptr/*default allocator*/, num_pools, num_pools);
		gpuTimeQueryManager = Ash_New<GPUTimeQueriesManager>();
		gpuTimeQueryManager->init(framePools.m_pData, nullptr/*default allocator*/, numQueryTimes, numThread, k_max_frames);
		commandBufferQueue.init(nullptr, k_command_buffer_queue_length);
		for (uint32_t i = 0; i < framePools.size(); i++)
		{
			FramePool& pool = framePools[i];
			pool.timeQueries = &gpuTimeQueryManager->query_trees[i];
			pool.cmdPool = Ash_New<VulkanCommandPool>(nullptr,vulkanDevice,vulkanMainQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
			VkQueryPoolCreateInfo timestampPoolCI = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
			timestampPoolCI.pNext = nullptr;
			timestampPoolCI.flags = 0;
			timestampPoolCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
			timestampPoolCI.queryCount = numQueryTimes;
			timestampPoolCI.pipelineStatistics = 0;
			VK_CHECK_RESULT(vkCreateQueryPool(vulkanDevice, &timestampPoolCI, vulkanAllocationCallbacks, &pool.vulkanTimestampQueryPool));
			VkQueryPoolCreateInfo pipelineStatisticPoolCI = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			pipelineStatisticPoolCI.pNext = nullptr;
			pipelineStatisticPoolCI.flags = 0;
			pipelineStatisticPoolCI.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
			pipelineStatisticPoolCI.queryCount = 7;
			pipelineStatisticPoolCI.pipelineStatistics = 0;
			pipelineStatisticPoolCI.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
				VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
				VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
				VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
				VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
				VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
				VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
			VK_CHECK_RESULT(vkCreateQueryPool(vulkanDevice, &pipelineStatisticPoolCI, vulkanAllocationCallbacks, &pool.vulkanPipelineStatsQueryPool));
		}
		frameDatas.init(nullptr, k_max_frames, k_max_frames);
		VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (uint32_t i = 0; i < frameDatas.size(); i++)
		{
			VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphore_info, vulkanAllocationCallbacks, &frameDatas[i].vulkanRenderCompleteSemaphore));
			VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphore_info, vulkanAllocationCallbacks, &frameDatas[i].vulkanRenderBeginSemaphore));
			if (!get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::TimelineSemaphore)) {
				frameDatas[i].vulkanCommandBufferExecutedFence = Ash_New<VulkanFence>();
			}
		}
		
		VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphore_info, vulkanAllocationCallbacks, &vulkanBindSemaphore));
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::TimelineSemaphore)) {
			VkSemaphoreTypeCreateInfo semaphore_type_info{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
			semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
			semaphore_info.pNext = &semaphore_type_info;

			VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphore_info, vulkanAllocationCallbacks, &vulkanGraphicsSemaphore));

			VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphore_info, vulkanAllocationCallbacks, &vulkanComputeSemaphore));
		}
		else {
			VK_CHECK_RESULT(vkCreateSemaphore(vulkanDevice, &semaphore_info, vulkanAllocationCallbacks, &vulkanComputeSemaphore));
			vulkanComputeFence = Ash_New<VulkanFence>();
		}
		vulkanImmediateFence = Ash_New<VulkanFence>();
		//allocate commandbuffers
		commandBufferRing = Ash_New<VulkanCommandBufferManager>();
		commandBufferRing->init(numThread);
		
		//create dynamic Buffer
		//TODO: make it dynamic alloc and dealloc
		//BufferCreation dc{};
		//dc.usage_flags = k_dynamic_buffer_mask;
		//dc.usage_flags = AshResourceUsageType::Immutable;
		//dc.name = "Dynamic Persistent Buffer";
		//dc.size = k_max_frames * 1024 * 1024 * 10;
		//dc.persistent = true;
		//global_dynamic_buffer = Ash_New_Shared<VulkanDynamicBuffer>(dc);
		//init sampler map
		samplerCache.init(nullptr, AshSamplerState::ASH_SAMPLER_STATE_MAX_ENUM, AshSamplerState::ASH_SAMPLER_STATE_MAX_ENUM);
		for (size_t i = 0; i < AshSamplerState::ASH_SAMPLER_STATE_MAX_ENUM; i++)
		{
			samplerCache[i] = create_sampler((AshSamplerState)i);
		}
		return true;

	}

	auto VulkanContext::create_sampler(const AshSamplerState& ss) -> std::shared_ptr<Sampler>
	{
		std::shared_ptr<VulkanSampler> ret;
		SamplerCreation ci{};

		switch (ss)
		{
		case ASH_SAMPLER_STATE_DEFAULT:
			ci.address_mode_u = ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			ci.address_mode_v = ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			ci.address_mode_w = ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			ci.border_color = ASH_BORDER_COLOR_INT_OPAQUE_WHITE;
			ci.minFilter = ASH_FILTER_LINEAR;
			ci.magFilter = ASH_FILTER_LINEAR;
			ci.mipFilter = ASH_FILTER_LINEAR;
			ci.name = "default sampler";
			ret = Ash_New_Shared<VulkanSampler>(ci);
			break;
		default:
			break;
		}
		return ret;
	}

	auto VulkanContext::vma_create_buffer(VkDeviceSize uBufferSize, VkBufferUsageFlags eBufferUsage, VmaMemoryUsage eMemUsage, VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation, void** ppData) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		VkBufferCreateInfo      sBufferCreateInfo = {};
		VmaAllocationCreateInfo vmaallocInfo = {};
		VmaAllocationInfo       vmaAllocationInfo = {};
		bool                    bMapped = (eMemUsage == VMA_MEMORY_USAGE_CPU_ONLY && ppData);
		H_ASSERT(!pVkBuffer);
		H_ASSERT(!pVMAllocation);
		sBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		sBufferCreateInfo.size = uBufferSize;
		sBufferCreateInfo.usage = eBufferUsage;
		vmaallocInfo.usage = eMemUsage;
		vmaallocInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
		// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
		if (bMapped)
		{
			vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		// allocate the buffer
		VkResult vkRetCode = vmaCreateBuffer(
			vmaAllocator,
			&sBufferCreateInfo,
			&vmaallocInfo,
			&pVkBuffer,
			&pVMAllocation,
			&vmaAllocationInfo
		);
		VK_CHECK_RESULT(vkRetCode);
		if (bMapped)
		{
			H_ASSERT(vmaAllocationInfo.pMappedData);
			*ppData = vmaAllocationInfo.pMappedData;
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_destroy_buffer(VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		vmaDestroyBuffer(vmaAllocator, pVkBuffer, pVMAllocation);
		pVkBuffer = nullptr;
		pVMAllocation = nullptr;
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_destroy_buffer_v(VkBuffer pVkBuffer, VmaAllocation pVMAllocation) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		vmaDestroyBuffer(vmaAllocator, pVkBuffer, pVMAllocation);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_create_image(const VkImageCreateInfo& sImgCreateInfo, VmaMemoryUsage eMemUsage, VkImage& pVkImage, VmaAllocation& pVMAllocation) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		VmaAllocationCreateInfo sVmaAllocCreateInfo = {};
		sVmaAllocCreateInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
		ASH_PROCESS_ERROR_EXIT(!pVkImage);
		ASH_PROCESS_ERROR_EXIT(!pVMAllocation);
		ASH_PROCESS_ERROR_EXIT("FATAL ERROR" && sImgCreateInfo.extent.width > 0 && sImgCreateInfo.extent.height > 0);
		ASH_PROCESS_ERROR_EXIT(eMemUsage > VmaMemoryUsage::VMA_MEMORY_USAGE_UNKNOWN && eMemUsage < VmaMemoryUsage::VMA_MEMORY_USAGE_MAX_ENUM);
		sVmaAllocCreateInfo.usage = eMemUsage;
		VkResult vkResult = vmaCreateImage(vmaAllocator, &sImgCreateInfo, &sVmaAllocCreateInfo, &pVkImage, &pVMAllocation, nullptr);
		VK_CHECK_RESULT(vkResult);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_destroy_image(VkImage pVkImage, VmaAllocation pVMAllocation) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR_EXIT(pVkImage);
		ASH_PROCESS_ERROR_EXIT(pVMAllocation);
		vmaDestroyImage(vmaAllocator, pVkImage, pVMAllocation);
		pVkImage = nullptr;
		pVMAllocation = nullptr;
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_map_memory(VmaAllocation pVMAllocation, void** ppData) const -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR_EXIT(pVMAllocation && ppData);
		VkResult vkRetCode = vmaMapMemory(vmaAllocator, pVMAllocation, ppData);
		VK_CHECK_RESULT(vkRetCode);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_unmap_memory(VmaAllocation pVMAllocation) const -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR_EXIT(pVMAllocation);
		vmaUnmapMemory(vmaAllocator, pVMAllocation);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_flush_allocation(VmaAllocation pVMAllocation, VkDeviceSize uOffset, VkDeviceSize uSize) const -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		VkResult vkRetCode = VK_INCOMPLETE;
		ASH_PROCESS_ERROR_EXIT(pVMAllocation);
		vkRetCode = vmaFlushAllocation(vmaAllocator, pVMAllocation, uOffset, uSize);
		ASH_PROCESS_ERROR_EXIT(vkRetCode == VK_SUCCESS);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::_shutdown_frame_pool_and_data() -> bool
	{
		samplerCache.shutdown();
		global_dynamic_buffer.reset();
		//flush deletion queue
		//delete all runtime resource here
		for (int i = 0; i < k_max_frames; i++)
		{
			delayed_deletion_queues[i].flush();
		}
		commandBufferRing->shutdown();
		Ash_Delete(nullptr,commandBufferRing);
		Ash_Delete(nullptr,vulkanImmediateFence);
		Ash_Delete(nullptr, vulkanComputeFence);
		if (vulkanComputeSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(vulkanDevice, vulkanComputeSemaphore, vulkanAllocationCallbacks);
			vulkanComputeSemaphore = VK_NULL_HANDLE;
		}
		if (vulkanGraphicsSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(vulkanDevice, vulkanGraphicsSemaphore, vulkanAllocationCallbacks);
			vulkanGraphicsSemaphore = VK_NULL_HANDLE;
		}
		if (vulkanBindSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(vulkanDevice, vulkanBindSemaphore, vulkanAllocationCallbacks);
			vulkanBindSemaphore = VK_NULL_HANDLE;
		}
		
		for (uint32_t i = 0; i < frameDatas.size(); i++)
		{
			vkDestroySemaphore(vulkanDevice, frameDatas[i].vulkanRenderBeginSemaphore, vulkanAllocationCallbacks);
			frameDatas[i].vulkanRenderBeginSemaphore = VK_NULL_HANDLE;
			vkDestroySemaphore(vulkanDevice, frameDatas[i].vulkanRenderCompleteSemaphore, vulkanAllocationCallbacks);
			frameDatas[i].vulkanRenderCompleteSemaphore = VK_NULL_HANDLE;
			if (!get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::TimelineSemaphore)) {
				Ash_Delete(nullptr, frameDatas[i].vulkanCommandBufferExecutedFence);
			}
		}
		frameDatas.shutdown();
		for (uint32_t i = 0; i < framePools.size(); i++)
		{
			FramePool& pool = framePools[i];
			Ash_Delete(nullptr,pool.cmdPool);
			pool.timeQueries = nullptr;
			if (pool.vulkanTimestampQueryPool != VK_NULL_HANDLE)
			{
				vkDestroyQueryPool(vulkanDevice, pool.vulkanTimestampQueryPool, vulkanAllocationCallbacks);
				pool.vulkanTimestampQueryPool = VK_NULL_HANDLE;
			}
			if (pool.vulkanPipelineStatsQueryPool != VK_NULL_HANDLE)
			{
				vkDestroyQueryPool(vulkanDevice, pool.vulkanPipelineStatsQueryPool, vulkanAllocationCallbacks);
				pool.vulkanPipelineStatsQueryPool = VK_NULL_HANDLE;
			}
		}
		commandBufferQueue.shutdown();
		gpuTimeQueryManager->shutdown();
		Ash_Delete(nullptr,gpuTimeQueryManager);
		framePools.shutdown();
		return true;
	}

	auto VulkanContext::_load_cache() -> bool
	{
		StringBuffer pathBuffer;
		pathBuffer.init(1024, nullptr);
		const char* pipelineCachePath = pathBuffer.append_get_f("%s", k_pipeline_cache_path);
		//load pipeline cache
		VkPipelineCacheCreateInfo pipeline_cache_create_info{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		if (file_exists(pipelineCachePath))
		{
			FileReadResult read_result = file_read_binary(pipelineCachePath, nullptr);
			VkPipelineCacheHeaderVersionOne* cache_header = (VkPipelineCacheHeaderVersionOne*)read_result.data;
			if (cache_header->deviceID == vulkanPhysicalDeviceProperties.deviceID &&
				cache_header->vendorID == vulkanPhysicalDeviceProperties.vendorID &&
				memcmp(cache_header->pipelineCacheUUID, vulkanPhysicalDeviceProperties.pipelineCacheUUID, VK_UUID_SIZE) == 0)
			{
				pipeline_cache_create_info.initialDataSize = read_result.size;
				pipeline_cache_create_info.pInitialData = read_result.data;
			}
			VK_CHECK_RESULT(vkCreatePipelineCache(vulkanDevice, &pipeline_cache_create_info, vulkanAllocationCallbacks, &vulkanPipelineCache));
			Ash_Free(nullptr,read_result.data);
		}
		else
		{
			VK_CHECK_RESULT(vkCreatePipelineCache(vulkanDevice, &pipeline_cache_create_info, vulkanAllocationCallbacks, &vulkanPipelineCache));
		}
		pathBuffer.shutdown();
		return true;
	}

	auto VulkanContext::_unload_cache() -> bool
	{
		StringBuffer pathBuffer;
		pathBuffer.init(1024, nullptr);
		
		size_t cache_data_size = 0;
		VK_CHECK_RESULT(vkGetPipelineCacheData(vulkanDevice, vulkanPipelineCache, &cache_data_size, nullptr));

		void* cache_data = Ash_Alloc(nullptr, cache_data_size,64);
		VK_CHECK_RESULT(vkGetPipelineCacheData(vulkanDevice, vulkanPipelineCache, &cache_data_size, cache_data));
		char* pipelineCacheDirectory = pathBuffer.append_get_f("%s", k_pipeline_cache_path);
		file_directory_from_path(pipelineCacheDirectory);
		if (!directory_exists(pipelineCacheDirectory))
		{
			directory_create(pipelineCacheDirectory);
		}
		file_write_binary(k_pipeline_cache_path, cache_data, cache_data_size);
		Ash_Free(nullptr, cache_data);
		vkDestroyPipelineCache(vulkanDevice, vulkanPipelineCache, vulkanAllocationCallbacks);
		pathBuffer.shutdown();
		return bool();
	}

	auto VulkanContext::_create_staging_buffer_pool() -> bool
	{
		vulkanStagingBufferPool = Ash_New<VulkanStagingBufferPool>();
		return vulkanStagingBufferPool->init();
	}

	auto VulkanContext::_shutdown_staging_buffer_pool() -> bool
	{
		if (vulkanStagingBufferPool)
		{
			vulkanStagingBufferPool->uninit();
			Ash_Delete(nullptr,vulkanStagingBufferPool);
		}
		return true;
	}

	auto VulkanContext::create_buffer_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView>
	{
		return std::shared_ptr<TextureView>();
	}

	auto VulkanContext::init(void* config) -> bool
	{	
		bool bRetCode = false;
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		GraphicsContextInitConfig vkConfig = *(GraphicsContextInitConfig*)config;
		H_ASSERT(&vkConfig);
		//load vulkan by volk;
		VK_CHECK_RESULT(volkInitialize());
		//create vkinstance
		bRetCode = _create_instance(vkConfig.addtionalExtensions);
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to create instance !");
#ifdef VULKAN_DEBUG_REPORT
		bRetCode = _create_debug_util_messenger_ext();
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to create DebugUtilMessengerExt !");
#endif
		bRetCode = _select_and_prepare_physical_device();
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to select Physical Device !");

		bRetCode = _create_device();
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to create Device !");

		bRetCode = _create_vulkan_memory_allocator();
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to create VMA Allocator !");

		bRetCode = _create_descriptor_pool(vkConfig.descriptorPoolCreation);
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to create DescriptorPool !");

		bRetCode = _create_frame_pool_and_data(vkConfig.num_threads, vkConfig.queryCount);
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to create FrameData !");

		bRetCode = _load_cache();
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to load cache !");

		bRetCode = _create_staging_buffer_pool();
		ASH_LOG_PROCESS_ERROR(bRetCode, "Fatal : Failed to create staging buffer pool !");

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanContext::shutdown() -> bool
	{
		//wait idle
		wait_idle();
		//unload cache
		_unload_cache();
		//shutdown framepool and datas
		_shutdown_frame_pool_and_data();
		//shutdown descriptor pool
		_shutdown_descriptor_pool();
		//shutdown vma
		_shutdown_vulkan_memory_allocator();
		//shutdown device
		_shutdown_device();
		//shutdown debugutilmsgr
		_shutdown_debug_util_messenger_ext();
		//shutdown instance
		_shutdown_instance();

		return true;
	}

	/********************************************************** RHI INTERFACE ******************************************************************************************************/

	auto VulkanContext::create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer>
	{
		return nullptr;// VulkanBuffer::create(ci);
	}

	auto VulkanContext::create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture>
	{
		return VulkanTexture::create(ci);
	}

	auto VulkanContext::create_texture_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView>
	{
		return VulkanTextureView::create(ci, parentTexture);
	}

	auto VulkanContext::create_shader(const ShaderCreation& ci) -> std::shared_ptr<Shader> 
	{
		uint64_t uHashCode = get_shader_hash(ci);
		std::shared_ptr<Shader> pRetShader = vulkanShaderPool.get(uHashCode);
		if (!pRetShader)
		{
			pRetShader = Ash_New_Shared<VulkanShader>(ci);
			vulkanShaderPool.emplace(uHashCode, pRetShader);
		}
		return pRetShader;
	}

	auto VulkanContext::get_sampler(const AshSamplerState& ss) -> std::shared_ptr<Sampler>
	{
		return samplerCache[ss];
	}

	auto VulkanContext::wait_idle() -> void 
	{
		VK_CHECK_RESULT(vkDeviceWaitIdle(vulkanDevice));
	}

	auto VulkanContext::get_command_buffer(uint32_t threadIndx) -> CommandBuffer*
	{
		auto current_frame = currentFrame != UINT32_MAX ? currentFrame : 0;
		return commandBufferRing->get_command_buffer(current_frame, threadIndx);
	}

	auto VulkanContext::get_secondary_command_buffer(uint32_t threadIndx) -> CommandBuffer*
	{
		auto current_frame = currentFrame != UINT32_MAX ? currentFrame : 0;
		return commandBufferRing->get_secondary_command_buffer(current_frame, threadIndx);
	}

	auto VulkanContext::submit(const SubmitInfo& info) -> void
	{
		//enqueue vkCommandBuffer
		for (size_t i = 0; i < info.cmdCount; i++)
		{
			H_ASSERTLOG(info.cmds[i].get_state() == AshCommandBufferState::ASH_Ended ||
				info.cmds[i].get_state() == AshCommandBufferState::ASH_Idle, "Fatal: The submitted command buffer must be in the one of the state of ended or idle!");
			commandBufferQueue.push_back(static_cast<VulkanCommandBuffer*>(&info.cmds[i]));
		}
	}

	auto VulkanContext::submit_immediately(const SubmitInfo& info) -> void
	{
		H_ASSERTLOG(info.cmdCount == 1, " immediately submit can only submit one command once!");
		H_ASSERTLOG(info.cmds->get_state() == AshCommandBufferState::ASH_Idle || info.cmds->get_state() == AshCommandBufferState::ASH_Ended, " comand buffer must be ended before submit!");
		//immediately submit and got result
		const VkCommandBuffer cmds[] = { (VkCommandBuffer)info.cmds->get_native_handle() };
		vulkanImmediateFence->reset();
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2)) {
			VkCommandBufferSubmitInfoKHR command_buffer_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR };
			command_buffer_info.commandBuffer = *cmds;

			VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
			submit_info.commandBufferInfoCount = 1;
			submit_info.pCommandBufferInfos = &command_buffer_info;

			VK_CHECK_RESULT(vkQueueSubmit2KHR(vulkanMainQueue, 1, &submit_info, vulkanImmediateFence->get_handle()));
		}
		else {
			VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit_info.waitSemaphoreCount = 0;
			submit_info.pWaitSemaphores = nullptr;
			submit_info.pWaitDstStageMask = nullptr;
			submit_info.commandBufferCount = info.cmdCount;
			submit_info.pCommandBuffers = cmds;
			submit_info.signalSemaphoreCount = 0;
			submit_info.pSignalSemaphores = nullptr;
			VK_CHECK_RESULT(vkQueueSubmit(vulkanMainQueue, 1, &submit_info, vulkanImmediateFence->get_handle()));
		}
		info.cmds->set_state(AshCommandBufferState::ASH_Submitted);
		vulkanImmediateFence->wait();
	}

	auto VulkanContext::begin_frame() -> void
	{
		if (currentFrame == UINT32_MAX)
		{
			currentFrame = 0;
			previousFrame = 0;
			absoluteFrame = 0;
		}
		else
		{
			previousFrame = currentFrame;
			currentFrame = (currentFrame + 1) % k_max_frames;
			absoluteFrame++;
		}
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::TimelineSemaphore))
		{
			if (absoluteFrame >= k_max_frames) {
				uint64_t graphics_timeline_value = absoluteFrame - (k_max_frames - 1);
				/*uint64_t compute_timeline_value = 0;*/
				uint64_t wait_values[]{ graphics_timeline_value/*, compute_timeline_value*/ };
				VkSemaphore semaphores[]{ vulkanGraphicsSemaphore};
				VkSemaphoreWaitInfo semaphore_wait_info{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
				semaphore_wait_info.semaphoreCount = 1;
				semaphore_wait_info.pSemaphores = semaphores;
				semaphore_wait_info.pValues = wait_values;
				vkWaitSemaphores(vulkanDevice, &semaphore_wait_info, ~0ull);
			}
		}
		else
		{
			get_frame_data_internal().vulkanCommandBufferExecutedFence->wait_and_reset();
		}
		//flush deletion queue
		delayed_deletion_queues[currentFrame].flush();
		commandBufferQueue.clear();
		//reset commandpool and commandbufferring
		commandBufferRing->reset(currentFrame);
		for (size_t i = 0; i < num_thread; i++)
		{
			FramePool& pool = framePools[currentFrame * num_thread + i];
			pool.cmdPool->reset();
		}
	}

	auto VulkanContext::end_frame() -> void
	{
		//submit all commands
		size_t count = commandBufferQueue.size();
		VkCommandBuffer cmds[k_command_buffer_queue_length] = {};
		if (count == 0)
		{
			cmds[0] = VK_NULL_HANDLE;
		}
		else
		{
			for (size_t i = 0; i < count; i++)
			{
				cmds[i] = (VkCommandBuffer)commandBufferQueue[i]->get_native_handle();
				commandBufferQueue[i]->set_state(AshCommandBufferState::ASH_Submitted);
			}
		}		
		if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::TimelineSemaphore))
		{
			bool wait_for_timeline_semaphore = absoluteFrame >= k_max_frames;
			if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2))
			{
				VkCommandBufferSubmitInfoKHR command_buffer_info[k_command_buffer_queue_length]{ };
				for (uint32_t c = 0; c < count; c++) {
					command_buffer_info[c].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
					command_buffer_info[c].commandBuffer = (VkCommandBuffer)commandBufferQueue[c]->get_native_handle();
				}
				Array<VkSemaphoreSubmitInfoKHR> wait_semaphores;
				wait_semaphores.init(nullptr, 4);
				wait_semaphores.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, get_frame_data_internal().vulkanRenderBeginSemaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 });
				if (wait_for_timeline_semaphore) {
					wait_semaphores.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkanGraphicsSemaphore, absoluteFrame - (k_max_frames - 1), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR , 0 });
				}
				VkSemaphoreSubmitInfoKHR signal_semaphores[]{
					{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, get_frame_data_internal().vulkanRenderCompleteSemaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 },
					{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkanGraphicsSemaphore, absoluteFrame + 1, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR , 0 }
				};
				VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
				submit_info.waitSemaphoreInfoCount = wait_semaphores.size();
				submit_info.pWaitSemaphoreInfos = wait_semaphores.m_pData;
				submit_info.commandBufferInfoCount = count;
				submit_info.pCommandBufferInfos = command_buffer_info;
				submit_info.signalSemaphoreInfoCount = 2;
				submit_info.pSignalSemaphoreInfos = signal_semaphores;
				VK_CHECK_RESULT(vkQueueSubmit2KHR(vulkanMainQueue, 1, &submit_info, VK_NULL_HANDLE));
				wait_semaphores.shutdown();
			}
			else
			{
				Array<VkSemaphore> wait_semaphores;
				wait_semaphores.init(nullptr, 4);
				Array<uint64_t> wait_values;
				wait_values.init(nullptr, 4);
				Array<VkPipelineStageFlags> wait_stages;
				wait_stages.init(nullptr, 4);
				wait_semaphores.push_back(get_frame_data_internal().vulkanRenderBeginSemaphore);
				wait_values.push_back(0);
				wait_stages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
				if (wait_for_timeline_semaphore) {
					wait_semaphores.push_back(vulkanGraphicsSemaphore);
					wait_values.push_back(absoluteFrame - (k_max_frames - 1));
					wait_stages.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
				}
				VkSemaphore signal_semaphores[] = { get_frame_data_internal().vulkanRenderCompleteSemaphore, vulkanGraphicsSemaphore };
				uint64_t signal_values[] = { 0, absoluteFrame + 1 };
				VkTimelineSemaphoreSubmitInfo semaphore_info{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
				semaphore_info.signalSemaphoreValueCount = 2;
				semaphore_info.pSignalSemaphoreValues = signal_values;
				semaphore_info.waitSemaphoreValueCount = wait_values.size();
				semaphore_info.pWaitSemaphoreValues = wait_values.m_pData;
				VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
				submit_info.waitSemaphoreCount = wait_semaphores.size();
				submit_info.pWaitSemaphores = wait_semaphores.m_pData;
				submit_info.pWaitDstStageMask = wait_stages.m_pData;
				submit_info.commandBufferCount = count;
				submit_info.pCommandBuffers = cmds;
				submit_info.signalSemaphoreCount = 2;
				submit_info.pSignalSemaphores = signal_semaphores;
				submit_info.pNext = &semaphore_info;
				VK_CHECK_RESULT(vkQueueSubmit(vulkanMainQueue, 1, &submit_info, VK_NULL_HANDLE));
				wait_semaphores.shutdown();
				wait_values.shutdown();
				wait_stages.shutdown();
			}	
		}
		else
		{
			if (get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2))
			{
				VkCommandBufferSubmitInfoKHR command_buffer_info[k_command_buffer_queue_length]{ };
				for (uint32_t c = 0; c < count; c++) {
					command_buffer_info[c].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
					command_buffer_info[c].commandBuffer = (VkCommandBuffer)commandBufferQueue[c]->get_native_handle();
				}
				Array<VkSemaphoreSubmitInfoKHR> wait_semaphores;
				wait_semaphores.init(nullptr, 4);
				wait_semaphores.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, get_frame_data_internal().vulkanRenderBeginSemaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 });
				VkSemaphoreSubmitInfoKHR signal_semaphores[]{
					{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, get_frame_data_internal().vulkanRenderCompleteSemaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 },
				};
				VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
				submit_info.waitSemaphoreInfoCount = wait_semaphores.size();
				submit_info.pWaitSemaphoreInfos = wait_semaphores.m_pData;
				submit_info.commandBufferInfoCount = count;
				submit_info.pCommandBufferInfos = command_buffer_info;
				submit_info.signalSemaphoreInfoCount = 1;
				submit_info.pSignalSemaphoreInfos = signal_semaphores;
				VK_CHECK_RESULT(vkQueueSubmit2KHR(vulkanMainQueue, 1, &submit_info, get_frame_data_internal().vulkanCommandBufferExecutedFence->get_handle()));
				wait_semaphores.shutdown();
			}
			else
			{
				VkPipelineStageFlags flag = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				vulkanImmediateFence->reset();
				VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
				submit_info.waitSemaphoreCount = 1;
				submit_info.pWaitSemaphores = &get_frame_data_internal().vulkanRenderBeginSemaphore;
				submit_info.pWaitDstStageMask = &flag;
				submit_info.commandBufferCount = count;
				submit_info.pCommandBuffers = cmds;
				submit_info.signalSemaphoreCount = 1;
				submit_info.pSignalSemaphores = &get_frame_data_internal().vulkanRenderCompleteSemaphore;
				VK_CHECK_RESULT(vkQueueSubmit(vulkanMainQueue, 1, &submit_info, get_frame_data_internal().vulkanCommandBufferExecutedFence->get_handle()));
			}		
		}	
	}

	/********************************************************** RHI INTERFACE ******************************************************************************************************/

	VulkanContext* VulkanContext::instance = nullptr;
};
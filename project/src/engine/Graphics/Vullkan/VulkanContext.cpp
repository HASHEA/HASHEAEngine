#pragma once
#include "Base/hlog.h"

#include "VulkanContext.h"
#include "VulkanCommandPool.h"
#include "GpuProfiler.h"
#include <vector>
namespace RHI
{
	static const uint32_t        k_bindless_texture_binding = 10;
	static const uint32_t        k_bindless_image_binding = 11;
	static const uint32_t        k_max_bindless_resources = 1024;
	static const uint32_t        k_max_frames = 2;

	static const char* s_requested_extensions[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	// Platform specific extension
#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
		VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
		VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#elif (defined(VK_USE_PLATFORM_MIR_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR))
		VK_KHR_DISPLAY_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_IOS_MVK)
		VK_MVK_IOS_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VULKAN_DEBUG_REPORT
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif // VULKAN_DEBUG_REPORT
	};
	static const char* s_requested_layers[] = {
#ifdef VULKAN_DEBUG_REPORT
	"VK_LAYER_KHRONOS_validation",
	//"VK_LAYER_AMD_switchable_graphics",
	//"VK_LAYER_NV_optimus",
	//"VK_LAYER_LUNARG_core_validation",
	//"VK_LAYER_LUNARG_image",
	//"VK_LAYER_LUNARG_parameter_validation",
	//"VK_LAYER_LUNARG_object_tracker"
#else
	"",
#endif // VULKAN_DEBUG_REPORT
	};
	inline auto CheckLayerSupport()->HS_Result
	{
		std::vector<VkLayerProperties>& layers{};
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		layers.resize(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

		for (const char* layerName : s_requested_layers)
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
				return HS_FAIL;
			}
		}
		return HS_OK;
	}
	inline auto CheckExtensionSupport() -> HS_Result
	{
		std::vector<VkExtensionProperties>& properties{};
		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

		properties.resize(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, properties.data());

		bool extensionSupported = true;
		int size = ArraySize(s_requested_extensions);
		for (int i = 0; i < size; i++)
		{
			const char* extensionName = s_requested_extensions[i];
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
		return extensionSupported  ? HS_OK : HS_FAIL;
	}
	static VkBool32 VKDebugCallbacks(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT types,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data) {
		bool triggerBreak = severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
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

	auto VulkanContext::_create_instance() -> HS_Result
	{
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pNext = nullptr;
		appInfo.pApplicationName = "Hashea Engine";
		appInfo.applicationVersion = 1;
		appInfo.pEngineName = "Hashea";
		appInfo.engineVersion = 1;
		appInfo.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
		HS_Result result = CheckLayerSupport();
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Not all required layers are supported!");
			return result;
		}
		result = CheckExtensionSupport();
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Not all required extensions are supported!");
			return result;
		}
		VkInstanceCreateInfo instanceCI{};
		instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instanceCI.pNext = nullptr;
		instanceCI.flags = 0;
		instanceCI.pApplicationInfo = &appInfo;
		auto lyorExCounts = ArraySize(s_requested_layers);
		instanceCI.enabledLayerCount = lyorExCounts;
		instanceCI.ppEnabledLayerNames = lyorExCounts == 0 ? nullptr : s_requested_layers;
		lyorExCounts = ArraySize(s_requested_extensions);
		instanceCI.enabledExtensionCount = lyorExCounts;
		instanceCI.ppEnabledExtensionNames = lyorExCounts == 0 ? nullptr : s_requested_extensions;
#ifdef VULKAN_DEBUG_REPORT
		VkDebugUtilsMessengerCreateInfoEXT debugUtilCI = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugUtilCI.pfnUserCallback = VKDebugCallbacks;
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
		return HS_OK;
	}
#ifdef VULKAN_DEBUG_REPORT
	auto VulkanContext::_create_debug_util_messenger_ext() -> HS_Result
	{
		// Create new debug utils callback
		VkDebugUtilsMessengerCreateInfoEXT debugUtilCI = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugUtilCI.pfnUserCallback = VKDebugCallbacks;
		debugUtilCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
		debugUtilCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;

		VK_CHECK_RESULT(vkCreateDebugUtilsMessengerEXT(vulkanInstance, &debugUtilCI, vulkanAllocationCallbacks, &vulkanDebugUtilMessenger));
		return HS_OK;
	}
#endif

	auto VulkanContext::_select_and_prepare_physical_device() -> HS_Result
	{
		uint32_t numPhysicalDevice = 0;
		VK_CHECK_RESULT(vkEnumeratePhysicalDevices(vulkanInstance, &numPhysicalDevice, NULL));
		H_ASSERTLOG(numPhysicalDevice > 0, "No GPU Found !");
		std::vector<VkPhysicalDevice> physicalDevices{};
		physicalDevices.resize(numPhysicalDevice);
		VK_CHECK_RESULT(vkEnumeratePhysicalDevices(vulkanInstance, &numPhysicalDevice, physicalDevices.data()));
		HS_Result retCode = HS_FAIL;
		for (VkPhysicalDevice device : physicalDevices)
		{
			vkGetPhysicalDeviceProperties(device, &vulkanPhysicalDeviceProperties);
			if (vulkanPhysicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				vulkanPhysicalDevice = device;
				retCode = HS_OK;
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
		_filter_device_selectable_extension();
		//Query Properties Supported
		_query_supported_props();
		//Query Feature supported
		_query_supported_features();
		return retCode;
	}

	auto VulkanContext::_filter_device_selectable_extension() -> HS_Result
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
		return HS_OK;
	}

	auto VulkanContext::_query_supported_props() -> HS_Result
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
		return HS_OK;
	}

	auto VulkanContext::_query_supported_features() -> HS_Result
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
		
		return HS_OK;
	}

	auto VulkanContext::_create_device() -> HS_Result
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
#ifdef HASHEA_DEBUG
			HLogInfo("Family {0}, flags {1} queue count {2}\n", i, qf.queueFlags, qf.queueCount);
#endif // HASHEA_DEBUG
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

		if (vulkanTransferQueueFamily < queueCount) {
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
		VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		void* currentPNext = &vulkan11Features;
		vulkan12Features.pNext = currentPNext;
		currentPNext = &vulkan12Features;
		vulkan13Features.pNext = currentPNext;
		currentPNext = &vulkan13Features;
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
		vkGetDeviceQueue(vulkanDevice, computeQueueFamilyIndex, computeQueueIndex, &vulkanComputeQueue);
		if (vulkanTransferQueueFamily < queueFamilyCount) {
			vkGetDeviceQueue(vulkanDevice, transferQueueFamilyIndex, 0, &vulkanTransferQueue);
		}
		return HS_OK;
	}

	auto VulkanContext::_create_vulkan_memory_allocator() -> HS_Result
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

		return HS_OK;
	}

	auto VulkanContext::_create_descriptor_pool(const GpuDescriptorPoolCreation& dspci) -> HS_Result
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
		return HS_OK;
	}

	auto VulkanContext::_create_frame_pool_and_data(uint16_t numThread, uint16_t numQueryTimes) -> HS_Result
	{
		
		const uint32_t num_pools = numThread * k_max_frames;
		framePools.init(nullptr/*default allocator*/, num_pools, num_pools);
		gpuTimeQueryManager = Hashea_New_Shared<GPUTimeQueriesManager>();
		gpuTimeQueryManager->init(framePools.m_pData, nullptr/*default allocator*/, numQueryTimes, numThread, k_max_frames);
		for (uint32_t i = 0; i < framePools.size(); i++)
		{
			FramePool& pool = framePools[i];
			pool.cmdPool = Hashea_New_Shared<VulkanCommandPool>(vulkanDevice,vulkanMainQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
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
		return HS_OK;
	}

	auto VulkanContext::init(void* config) -> HS_Result
	{	
		GraphicsContextInitConfig vkConfig = *(GraphicsContextInitConfig*)config;
		H_ASSERT(&vkConfig);
		//load vulkan by volk;
		VK_CHECK_RESULT(volkInitialize());
		//create vkinstance
		HS_Result result = _create_instance();
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Fatal : Failed to create instance !");
			return result;
		}
#ifdef VULKAN_DEBUG_REPORT
		result = _create_debug_util_messenger_ext();
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Fatal : Failed to create DebugUtilMessengerExt !");
			return result;
		}
#endif
		result = _select_and_prepare_physical_device();
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Fatal : Failed to select Physical Device !");
			return result;
		}
		result = _create_device();
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Fatal : Failed to create Device !");
			return result;
		}
		result = _create_vulkan_memory_allocator();
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Fatal : Failed to create VMA Allocator !");
			return result;
		}
		result = _create_descriptor_pool(vkConfig.descriptorPoolCreation);
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Fatal : Failed to create DescriptorPool !");
			return result;
		}
		
		result = _create_frame_pool_and_data(vkConfig.num_threads, vkConfig.queryCount);
		if (HS_CHECK_FAILED(result))
		{
			HLogError("Fatal : Failed to create FrameData !");
			return result;
		}


	}
	auto VulkanContext::shutdown() -> HS_Result
	{
	}

	VulkanContext* VulkanContext::instance = nullptr;
};
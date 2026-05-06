#pragma once
#include "Base/hlog.h"
#include "Base/hprofiler.h"
#include "Base/hfile.h"
#include "Base/hcache.h"
#include "Base/hmemory.h"
#include "VulkanContext.h"
#include "VulkanCommandPool.h"
#include "VulkanCommandBuffer.h"
#include "VulkanFence.h"
#include "GpuProfiler.h"
#include "VulkanBuffer.h"
#include "VulkanFramebuffer.h"
#include "VulkanRenderPass.h"
#include "VulkanRenderProgram.h"
#include "VulkanDescriptorSet.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanShader.h"
#include "VulkanShaderCompiler.h"
#include "VulkanStagingBuffer.h"
#include "VulkanGpuProfiler.h"
#include "Graphics/GpuProfilerRHI.h"
#if defined(ASH_HAS_DXC)
#include "Graphics/DXC/DXCHelper.h"
#endif
#include "Graphics/TextureUploadUtils.h"
#include "Base/hthreading.h"
#include <array>
#include <cstring>
#include <mutex>
#include <sstream>
#include <vector>

#if defined(ASH_WINDOWS)
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif
namespace RHI
{
	constexpr const char* k_pipeline_cache_path = "product\\caches\\PipelineCaches\\AshVulkanPipelineCache.pipelineCacheVK";
	namespace
	{
		static std::string append_vulkan_shader_macro(const char* shader_macro)
		{
			std::string combined_macro = shader_macro ? shader_macro : "";
			if (combined_macro.find("ASH_VULKAN") == std::string::npos)
			{
				if (!combined_macro.empty() && combined_macro.back() != ';')
				{
					combined_macro.push_back(';');
				}
				combined_macro += "ASH_VULKAN=1";
			}
			return combined_macro;
		}

		constexpr uint32_t k_vma_stack_frame_skip = 2;
		constexpr uint32_t k_vma_stack_frame_capture_count = 24;

		static auto get_vma_allocation_key(VmaAllocation allocation) -> uint64_t
		{
			return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(allocation));
		}

		static auto get_vulkan_object_type_name(VkObjectType type) -> const char*
		{
			switch (type)
			{
			case VK_OBJECT_TYPE_BUFFER:
				return "Buffer";
			case VK_OBJECT_TYPE_IMAGE:
				return "Image";
			default:
				return "Unknown";
			}
		}

		static auto get_symbol_mutex() -> std::mutex&
		{
			static std::mutex mutex{};
			return mutex;
		}

		static auto ensure_symbol_handler_initialized() -> bool
		{
#if defined(ASH_WINDOWS)
			static bool initialized = false;
			static bool attempted = false;
			std::lock_guard<std::mutex> lock(get_symbol_mutex());
			if (!attempted)
			{
				SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
				initialized = SymInitialize(GetCurrentProcess(), nullptr, TRUE) == TRUE;
				attempted = true;
			}
			return initialized;
#else
			return false;
#endif
		}

		static auto format_stack_frame(uint64_t address) -> std::string
		{
			std::ostringstream stream{};
			stream << "0x" << std::hex << std::uppercase << address;

#if defined(ASH_WINDOWS)
			if (!ensure_symbol_handler_initialized())
			{
				return stream.str();
			}

			std::lock_guard<std::mutex> lock(get_symbol_mutex());
			HANDLE process = GetCurrentProcess();
			DWORD64 displacement = 0;
			std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolStorage{};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage.data());
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MAX_SYM_NAME;

			if (SymFromAddr(process, address, &displacement, symbol) == TRUE)
			{
				stream << " " << symbol->Name;
				if (displacement != 0)
				{
					stream << " +0x" << std::hex << std::uppercase << displacement;
				}
			}

			IMAGEHLP_LINE64 lineInfo{};
			lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			DWORD lineDisplacement = 0;
			if (SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo) == TRUE)
			{
				stream << " [" << lineInfo.FileName << ":" << std::dec << lineInfo.LineNumber << "]";
			}

			return stream.str();
#else
			return stream.str();
#endif
		}
	}
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
		const VulkanValidationConfig* validationConfig = reinterpret_cast<const VulkanValidationConfig*>(user_data);
		bool triggerBreak = severity & (/*VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |*/ VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
		triggerBreak = triggerBreak && (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT);
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			HLogWarning("[Vulkan Validation] - WARNING : MessageID: {0} {1}\nMessage: {2}\n\n", callback_data->pMessageIdName, callback_data->messageIdNumber, callback_data->pMessage);

		}
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			HLogError("[Vulkan Validation] - ERROR : MessageID: {0} {1}\nMessage: {2}\n\n", callback_data->pMessageIdName, callback_data->messageIdNumber, callback_data->pMessage);

		}
		if (triggerBreak && validationConfig && validationConfig->breakOnValidationError) {
			H_ASSERT(false);
		}
		return VK_FALSE;
	}

	auto VulkanContext::set_resource_name_internal(VkObjectType type, uint64_t handle, const char* name) -> void
	{
#ifdef VULKAN_DEBUG_REPORT
		if (!debugUtilsEnabled || handle == 0 || !name || name[0] == '\0')
		{
			return;
		}
		VkDebugUtilsObjectNameInfoEXT name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		name_info.objectType = type;
		name_info.objectHandle = handle;
		name_info.pObjectName = name;
		vkSetDebugUtilsObjectNameEXT(vulkanDevice, &name_info);
#endif // VULKAN_DEBUG_REPORT
	}

	auto VulkanContext::_create_instance(const Array<const char*>& additionalExtensions) -> bool
	{
		debugUtilsEnabled = false;
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
		if (validationConfig.enableValidation)
		{
			rqLayers.push_back("VK_LAYER_KHRONOS_validation");
		}
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
		if (validationConfig.enableValidation)
		{
			rqExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
			rqExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			debugUtilsEnabled = true;
		}
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
		VkValidationFeaturesEXT features = {};
		VkValidationFeatureEnableEXT featuresRequested[2] = {};
		uint32_t featuresRequestedCount = 0;
		if (validationConfig.enableValidation)
		{
			debugUtilCI.pfnUserCallback = vk_debug_callbacks;
			debugUtilCI.pUserData = &validationConfig;
			debugUtilCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
			debugUtilCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
#ifdef VULKAN_SYNCHRONIZATION_VALIDATION
			if (validationConfig.enableGpuAssisted)
			{
				featuresRequested[featuresRequestedCount++] = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
			}
			if (validationConfig.enableSynchronizationValidation)
			{
				featuresRequested[featuresRequestedCount++] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
			}
			if (featuresRequestedCount > 0)
			{
				features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
				features.pNext = &debugUtilCI;
				features.enabledValidationFeatureCount = featuresRequestedCount;
				features.pEnabledValidationFeatures = featuresRequested;
				instanceCI.pNext = &features;
			}
			else
			{
				instanceCI.pNext = &debugUtilCI;
			}
#else
			instanceCI.pNext = &debugUtilCI;
#endif // VULKAN_SYNCHRONIZATION_VALIDATION
		}
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
		if (!validationConfig.enableValidation)
		{
			return true;
		}
		// Create new debug utils callback
		VkDebugUtilsMessengerCreateInfoEXT debugUtilCI = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugUtilCI.pfnUserCallback = vk_debug_callbacks;
		debugUtilCI.pUserData = &validationConfig;
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
		if (!retCode && !physicalDevices.empty())
		{
			vulkanPhysicalDevice = physicalDevices[0];
			vkGetPhysicalDeviceProperties(vulkanPhysicalDevice, &vulkanPhysicalDeviceProperties);
			retCode = true;
		}
		if (!retCode)
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

			for (uint32_t i = 0; i < vulkanPhysicalDeviceMemoryProperties.memoryTypeCount && i < VK_MAX_MEMORY_TYPES; ++i)
			{
				if ((vulkanPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
					(vulkanPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
				{
					featureSwitchFlags.set_bit(DeviceExtensionAndFeaturesFlags::HostCoherentCached);
					break;
				}
			}
		}
		return true;
	}

	auto VulkanContext::_query_supported_props() -> bool
	{
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
		subgroupSize = subgroupProperties.subgroupSize;
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
		if (mainQueueFamilyIndex == UINT32_MAX)
		{
			HLogError("Failed to find a graphics queue family.");
			return false;
		}
		if (computeQueueFamilyIndex == UINT32_MAX)
		{
			computeQueueFamilyIndex = mainQueueFamilyIndex;
			computeQueueIndex = 0;
		}
		if (transferQueueFamilyIndex == UINT32_MAX)
		{
			transferQueueFamilyIndex = mainQueueFamilyIndex;
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
		fn.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
		fn.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
		fn.vkBindImageMemory2KHR = vkBindImageMemory2;
		fn.vkBindBufferMemory2KHR = vkBindBufferMemory2;
		fn.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
		allocatorCI.pVulkanFunctions = &fn;
		VK_CHECK_RESULT(vmaCreateAllocator(&allocatorCI, &vmaAllocator));

		return true;
	}

	auto VulkanContext::_shutdown_vulkan_memory_allocator() -> bool
	{
		_dump_vma_leaks();
		vmaDestroyAllocator(vmaAllocator);
		vmaAllocator = VK_NULL_HANDLE;
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
			pool.cmdPool = Ash_New<VulkanCommandPool>(nullptr, vulkanDevice, vulkanMainQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, vulkanAllocationCallbacks);
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
			ci.address_mode_u = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
			ci.address_mode_v = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
			ci.address_mode_w = ASH_SAMPLER_ADDRESS_MODE_REPEAT;
			ci.border_color = ASH_BORDER_COLOR_INT_OPAQUE_WHITE;
			ci.minFilter = ASH_FILTER_LINEAR;
			ci.magFilter = ASH_FILTER_LINEAR;
			ci.mipFilter = ASH_FILTER_LINEAR;
			ci.name = "default sampler";
			ret = std::static_pointer_cast<VulkanSampler>(create_sampler(ci));
			break;
		default:
			break;
		}
		return ret;
	}

	auto VulkanContext::create_sampler(const SamplerCreation& ci) -> std::shared_ptr<Sampler>
	{
		return Ash_New_Shared<VulkanSampler>(ci);
	}

	auto VulkanContext::_track_vma_allocation(VmaAllocation allocation, VkObjectType objectType, uint64_t resourceHandle, uint64_t size, const char* debugName, const char* file, uint32_t line, const char* function) -> void
	{
#if ASH_ENABLE_VMA_LEAK_TRACKING
		if (!allocation)
		{
			return;
		}

		VmaTrackedAllocationInfo info{};
		info.objectType = objectType;
		info.resourceHandle = resourceHandle;
		info.allocationHandle = get_vma_allocation_key(allocation);
		info.size = size;
		info.debugName = debugName ? debugName : "";
		info.file = file ? file : "";
		info.function = function ? function : "";
		info.line = line;
		_capture_vma_allocation_stack(info);

		std::lock_guard<std::mutex> lock(vmaTrackedAllocationsMutex);
		vmaTrackedAllocations[info.allocationHandle] = std::move(info);
#else
		(void)allocation;
		(void)objectType;
		(void)resourceHandle;
		(void)size;
		(void)debugName;
		(void)file;
		(void)line;
		(void)function;
#endif
	}

	auto VulkanContext::_capture_vma_allocation_stack(VmaTrackedAllocationInfo& info) const -> void
	{
#if ASH_ENABLE_VMA_LEAK_STACKTRACE
		void* frames[k_vma_stack_frame_capture_count]{};
		const USHORT capturedFrameCount = CaptureStackBackTrace(k_vma_stack_frame_skip, k_vma_stack_frame_capture_count, frames, nullptr);
		info.stackFrames.reserve(capturedFrameCount);
		for (USHORT index = 0; index < capturedFrameCount; ++index)
		{
			info.stackFrames.push_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frames[index])));
		}
#else
		(void)info;
#endif
	}

	auto VulkanContext::_shutdown_shader_pool() -> bool
	{
		// Pooled shaders (VkShaderModule + descriptor layouts) must be destroyed while the device is still valid.
		// vulkanShaderPool holds the last strong refs after the app drops external shared_ptrs; without draining
		// the pool here, ~VulkanShader never runs and validation reports leaked shader modules at device destroy.
		vulkanShaderPool.clear();
		return true;
	}

	auto VulkanContext::_untrack_vma_allocation(VmaAllocation allocation, VkObjectType objectType, uint64_t resourceHandle, const char* file, uint32_t line, const char* function) -> void
	{
#if ASH_ENABLE_VMA_LEAK_TRACKING
		if (!allocation)
		{
			return;
		}

		const uint64_t allocationHandle = get_vma_allocation_key(allocation);
		std::lock_guard<std::mutex> lock(vmaTrackedAllocationsMutex);
		const auto iter = vmaTrackedAllocations.find(allocationHandle);
		if (iter == vmaTrackedAllocations.end())
		{
			HLogWarning(
				"VMA untrack missed allocation: type={}, allocation=0x{:X}, resource=0x{:X}, free_site={}:{} ({})",
				get_vulkan_object_type_name(objectType),
				allocationHandle,
				resourceHandle,
				file ? file : "<unknown>",
				line,
				function ? function : "<unknown>");
			return;
		}
		vmaTrackedAllocations.erase(iter);
#else
		(void)allocation;
		(void)objectType;
		(void)resourceHandle;
		(void)file;
		(void)line;
		(void)function;
#endif
	}

	auto VulkanContext::_dump_vma_leaks() const -> void
	{
#if ASH_ENABLE_VMA_LEAK_TRACKING
		std::lock_guard<std::mutex> lock(vmaTrackedAllocationsMutex);
		if (vmaTrackedAllocations.empty())
		{
			HLogInfo("VMA leak tracking: no live VMA allocations detected before allocator shutdown.");
			return;
		}

		HLogError("Detected {} live VMA allocations before allocator shutdown.", vmaTrackedAllocations.size());
		for (const auto& [allocationHandle, info] : vmaTrackedAllocations)
		{
			HLogError(
				"VMA leak: type={}, name='{}', allocation=0x{:X}, resource=0x{:X}, size={}, allocated_at={}:{} ({})",
				get_vulkan_object_type_name(info.objectType),
				info.debugName.empty() ? "<unnamed>" : info.debugName,
				allocationHandle,
				info.resourceHandle,
				info.size,
				info.file.empty() ? "<unknown>" : info.file,
				info.line,
				info.function.empty() ? "<unknown>" : info.function);
#if ASH_ENABLE_VMA_LEAK_STACKTRACE
			for (size_t frameIndex = 0; frameIndex < info.stackFrames.size(); ++frameIndex)
			{
				HLogError("  [{}] {}", frameIndex, format_stack_frame(info.stackFrames[frameIndex]));
			}
#endif
		}
#endif
	}

	auto VulkanContext::vma_create_buffer(VkDeviceSize uBufferSize, VkBufferUsageFlags eBufferUsage, VmaMemoryUsage eMemUsage, VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation, void** ppData, const char* debugName, const char* file, uint32_t line, const char* function) -> bool
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
#ifdef ASH_DEBUG
		if (pVMAllocation && debugName && debugName[0] != '\0')
		{
			vmaSetAllocationName(vmaAllocator, pVMAllocation, debugName);
		}
#endif
		_track_vma_allocation(pVMAllocation, VK_OBJECT_TYPE_BUFFER, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pVkBuffer)), static_cast<uint64_t>(uBufferSize), debugName, file, line, function);
		if (bMapped)
		{
			H_ASSERT(vmaAllocationInfo.pMappedData);
			*ppData = vmaAllocationInfo.pMappedData;
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_destroy_buffer(VkBuffer& pVkBuffer, VmaAllocation& pVMAllocation, const char* file, uint32_t line, const char* function) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		_untrack_vma_allocation(pVMAllocation, VK_OBJECT_TYPE_BUFFER, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pVkBuffer)), file, line, function);
		vmaDestroyBuffer(vmaAllocator, pVkBuffer, pVMAllocation);
		pVkBuffer = nullptr;
		pVMAllocation = nullptr;
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_destroy_buffer_v(VkBuffer pVkBuffer, VmaAllocation pVMAllocation, const char* file, uint32_t line, const char* function) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		_untrack_vma_allocation(pVMAllocation, VK_OBJECT_TYPE_BUFFER, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pVkBuffer)), file, line, function);
		vmaDestroyBuffer(vmaAllocator, pVkBuffer, pVMAllocation);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_create_image(const VkImageCreateInfo& sImgCreateInfo, VmaMemoryUsage eMemUsage, VkImage& pVkImage, VmaAllocation& pVMAllocation, const char* debugName, const char* file, uint32_t line, const char* function) -> bool
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
#ifdef ASH_DEBUG
		if (pVMAllocation && debugName && debugName[0] != '\0')
		{
			vmaSetAllocationName(vmaAllocator, pVMAllocation, debugName);
		}
#endif
		VmaAllocationInfo allocationInfo{};
		vmaGetAllocationInfo(vmaAllocator, pVMAllocation, &allocationInfo);
		_track_vma_allocation(pVMAllocation, VK_OBJECT_TYPE_IMAGE, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pVkImage)), static_cast<uint64_t>(allocationInfo.size), debugName, file, line, function);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	auto VulkanContext::vma_destroy_image(VkImage pVkImage, VmaAllocation pVMAllocation, const char* file, uint32_t line, const char* function) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR_EXIT(pVkImage);
		_untrack_vma_allocation(pVMAllocation, VK_OBJECT_TYPE_IMAGE, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pVkImage)), file, line, function);
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
		vulkanPipelineCache = VK_NULL_HANDLE;
		pathBuffer.shutdown();
		return true;
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

	auto VulkanContext::create_buffer_view(const BufferViewCreation& ci, std::shared_ptr<Buffer> parentBuffer) -> std::shared_ptr<BufferView>
	{
		return Ash_New_Shared<VulkanBufferView>(ci, parentBuffer);
	}

	auto VulkanContext::init(void* config) -> bool
	{	
		bool bRetCode = false;
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		H_ASSERT(config);
		const GraphicsContextInitConfig& vkConfig = *reinterpret_cast<const GraphicsContextInitConfig*>(config);
		validationConfig = vkConfig.vulkanValidation;
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

		// 安装 Tracy GPU profiler。需要在 device/queue 创建之后。
		// 失败不致命：内部会保留空 ctx，所有 zone 退化为 no-op。
		{
			auto* profiler = new VulkanGpuProfiler(
				vulkanInstance,
				vulkanPhysicalDevice,
				vulkanDevice,
				vulkanMainQueue,
				vulkanMainQueueFamily,
				"Vulkan");
			gpu_profiler_install(profiler);
		}

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanContext::shutdown() -> bool
	{
		//wait idle
		wait_idle();

		// 卸载 Tracy GPU profiler，必须在 device 销毁之前。
		if (auto* profiler = gpu_profiler_get())
		{
			gpu_profiler_install(nullptr);
			delete profiler;
		}

		//unload cache
		_unload_cache();
		//shutdown framepool and datas
		_shutdown_frame_pool_and_data();
		//shutdown staging pool
		_shutdown_staging_buffer_pool();
		//shutdown shader pool
		_shutdown_shader_pool();
		//release any layout-owned descriptor pools that may still be held by
		//live programs/layouts before we flush delayed deletion and destroy the device
		shutdown_vulkan_descriptor_set_layout_cache();
		//shutdown descriptor pool
		_shutdown_descriptor_pool();
		//flush deletion queue at the very end to make sure all resources are destroyed before device destroy, otherwise VUID-vkDestroyDevice-device-05137.
		for (auto& deletionQueue : delayed_deletion_queues)
		{
			deletionQueue.flush();
		}
		//shutdown vma
		_shutdown_vulkan_memory_allocator();
		//shutdown device
		_shutdown_device();
		//shutdown debugutilmsgr
#ifdef VULKAN_DEBUG_REPORT
		_shutdown_debug_util_messenger_ext();
#endif
		//shutdown instance
		_shutdown_instance();

		return true;
	}

auto VulkanContext::destroy() -> void
{
	VulkanContext* self = this;
	Ash_Delete(nullptr, self);
}

	/********************************************************** RHI INTERFACE ******************************************************************************************************/

	auto VulkanContext::create_buffer(const BufferCreation& ci) -> std::shared_ptr<Buffer>
	{
		auto buffer = Ash_New_Shared<VulkanBuffer>();
		if (!buffer->create(ci))
		{
			HLogError(
				"VulkanContext: failed to create buffer '{}' (size={}, usage=0x{:X}, access_type={}, force_static={}, has_initial_data={}).",
				ci.name ? ci.name : "UnnamedBuffer",
				ci.size,
				static_cast<uint32_t>(ci.usage_flags),
				static_cast<uint32_t>(ci.access_type),
				ci.force_static,
				ci.initial_data != nullptr);
			return nullptr;
		}
		return buffer;
	}

	auto VulkanContext::queue_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			HLogError(
				"VulkanContext: queue_buffer_upload rejected request (buffer={}, data={}, size={}, offset={}).",
				buffer ? (buffer->get_name() ? buffer->get_name() : "UnnamedBuffer") : "<null>",
				data != nullptr,
				size,
				offset);
			return false;
		}

		if (!AshEngine::is_in_render_thread() || !frameActive)
		{
			const bool queued = _enqueue_pending_buffer_upload(buffer, offset, size, data);
			if (!queued)
			{
				HLogError(
					"VulkanContext: failed to enqueue pending buffer upload for '{}' (offset={}, size={}, in_render_thread={}, frameActive={}).",
					buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
					offset,
					size,
					AshEngine::is_in_render_thread(),
					frameActive);
			}
			return queued;
		}

		const bool recorded = _record_buffer_upload(buffer, offset, size, data);
		if (!recorded)
		{
			HLogError(
				"VulkanContext: live buffer upload failed for '{}' (offset={}, size={}, uploadCommandsPending={}, uploadCommandQueued={}, currentUploadCommandBuffer={}, currentFrame={}).",
				buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
				offset,
				size,
				uploadCommandsPending,
				uploadCommandQueued,
				currentUploadCommandBuffer != nullptr,
				currentFrame);
		}
		return recorded;
	}

	auto VulkanContext::queue_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}

		const auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
		const TextureCreation& creation = vulkanTexture->get_desciption();
		const AshTextureFormatInfo& formatInfo = get_vk_texture_format_info(creation.format);
		if (creation.eSampleCount != ASH_SAMPLE_COUNT_1_BIT ||
			vulkanTexture->is_sparse() ||
			formatInfo.vkFormat == VK_FORMAT_UNDEFINED ||
			TextureFormat::has_depth_or_stencil(formatInfo.vkFormat))
		{
			return false;
		}

		if (!AshEngine::is_in_render_thread() || !frameActive)
		{
			return _enqueue_pending_texture_upload(texture, data);
		}

		return _record_texture_upload(texture, data);
	}

	auto VulkanContext::_enqueue_pending_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			return false;
		}

		PendingBufferUpload upload{};
		upload.buffer = buffer;
		upload.offset = offset;
		upload.data.resize(size);
		std::memcpy(upload.data.data(), data, size);
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			pendingBufferUploads.push_back(std::move(upload));
		}
		return true;
	}

	auto VulkanContext::_enqueue_pending_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}

		const TextureCreation& creation = texture->get_desciption();
		const AshTextureFormatInfo& formatInfo = get_vk_texture_format_info(creation.format);
		TextureUploadFormatInfo uploadFormatInfo{};
		uploadFormatInfo.bytesPerBlock = formatInfo.uBytesPerBlock;
		uploadFormatInfo.widthPerBlock = formatInfo.uWidthPerBlock;
		uploadFormatInfo.heightPerBlock = formatInfo.uHeightPerBlock;

		std::vector<TextureUploadSubresource> subresources{};
		uint64_t totalBytes = 0;
		if (!build_tightly_packed_texture_upload_layout(creation, uploadFormatInfo, subresources, totalBytes) || totalBytes == 0 || totalBytes > UINT32_MAX)
		{
			return false;
		}

		PendingTextureUpload upload{};
		upload.texture = texture;
		upload.data.resize(static_cast<size_t>(totalBytes));
		std::memcpy(upload.data.data(), data, static_cast<size_t>(totalBytes));
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			pendingTextureUploads.push_back(std::move(upload));
		}
		return true;
	}

	auto VulkanContext::_ensure_upload_command_buffer_recording() -> bool
	{
		if (!frameActive)
		{
			HLogError("VulkanContext: requested live upload command buffer while frameActive=false.");
			return false;
		}

		const bool needNewCmd =
			!uploadCommandsPending ||
			!currentUploadCommandBuffer ||
			currentUploadCommandBuffer->get_state() != AshCommandBufferState::ASH_Recording;
		if (needNewCmd)
		{
			currentUploadCommandBuffer = static_cast<VulkanCommandBuffer*>(get_command_buffer(0));
			if (!currentUploadCommandBuffer)
			{
				HLogError("VulkanContext: failed to acquire upload command buffer for frame {}.", currentFrame);
				return false;
			}
			currentUploadCommandBuffer->begin_record();
			uploadCommandsPending = true;
			uploadCommandQueued = false;
		}

		return currentUploadCommandBuffer != nullptr;
	}

	auto VulkanContext::_record_buffer_upload(const std::shared_ptr<Buffer>& buffer, uint32_t offset, uint32_t size, const void* data) -> bool
	{
		if (!buffer || !data || size == 0)
		{
			HLogError(
				"VulkanContext: _record_buffer_upload rejected request (buffer={}, data={}, size={}, offset={}).",
				buffer ? (buffer->get_name() ? buffer->get_name() : "UnnamedBuffer") : "<null>",
				data != nullptr,
				size,
				offset);
			return false;
		}
		if (!_ensure_upload_command_buffer_recording())
		{
			HLogError(
				"VulkanContext: failed to start live buffer upload for '{}' (offset={}, size={}).",
				buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
				offset,
				size);
			return false;
		}

		const bool updated = currentUploadCommandBuffer->cmd_update_sub_resource(buffer, offset, size, const_cast<void*>(data));
		if (!updated)
		{
			HLogError(
				"VulkanContext: cmd_update_sub_resource returned false for '{}' (offset={}, size={}, command_buffer_state={}).",
				buffer->get_name() ? buffer->get_name() : "UnnamedBuffer",
				offset,
				size,
				currentUploadCommandBuffer ? static_cast<uint32_t>(currentUploadCommandBuffer->get_state()) : UINT32_MAX);
		}
		return updated;
	}

	auto VulkanContext::_record_texture_upload(const std::shared_ptr<Texture>& texture, const void* data) -> bool
	{
		if (!texture || !data)
		{
			return false;
		}
		if (!_ensure_upload_command_buffer_recording())
		{
			return false;
		}

		return currentUploadCommandBuffer->cmd_update_texture_sub_resource(texture, data);
	}

	auto VulkanContext::_flush_pending_buffer_uploads() -> bool
	{
		std::vector<PendingBufferUpload> uploads{};
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			uploads.swap(pendingBufferUploads);
		}

		for (const PendingBufferUpload& upload : uploads)
		{
			if (!_record_buffer_upload(upload.buffer, upload.offset, static_cast<uint32_t>(upload.data.size()), upload.data.data()))
			{
				return false;
			}
		}

		return true;
	}

	auto VulkanContext::_flush_pending_texture_uploads() -> bool
	{
		std::vector<PendingTextureUpload> uploads{};
		{
			std::scoped_lock<std::mutex> lock(pendingUploadMutex);
			uploads.swap(pendingTextureUploads);
		}

		for (const PendingTextureUpload& upload : uploads)
		{
			if (!_record_texture_upload(upload.texture, upload.data.data()))
			{
				return false;
			}
		}

		return true;
	}

	auto VulkanContext::_finalize_upload_command_buffer() -> void
	{
		if (!uploadCommandsPending || !currentUploadCommandBuffer)
		{
			return;
		}
		if (currentUploadCommandBuffer->get_state() == AshCommandBufferState::ASH_Recording)
		{
			currentUploadCommandBuffer->end_record();
		}
	}

	auto VulkanContext::create_texture(const TextureCreation& ci) -> std::shared_ptr<Texture>
	{
		std::shared_ptr<Texture> texture = VulkanTexture::create(ci);
		if (!texture)
		{
			return nullptr;
		}
		if (ci.initial_data)
		{
			if (!queue_texture_upload(texture, ci.initial_data))
			{
				HLogError("VulkanContext: Failed to enqueue initial data upload for texture '{}'.", ci.name ? ci.name : "UnnamedTexture");
				return nullptr;
			}
		}
		return texture;
	}

	auto VulkanContext::create_texture_view(const TextureViewCreation& ci, std::shared_ptr<Texture> parentTexture) -> std::shared_ptr<TextureView>
	{
		return VulkanTextureView::create(ci, parentTexture);
	}

	auto VulkanContext::create_render_pass(const RenderPassCreation& ci) -> std::shared_ptr<RenderPass>
	{
		return VulkanRenderPass::create(ci);
	}

	auto VulkanContext::create_framebuffer(const FramebufferCreation& ci) -> std::shared_ptr<Framebuffer>
	{
		return VulkanFramebuffer::create(ci);
	}

	auto VulkanContext::create_graphics_render_program(const GraphicProgramCreateDesc& desc) -> std::unique_ptr<IGraphicsRenderProgram>
	{
		auto program = std::make_unique<VulkanGraphicsRenderProgram>();
		if (!program->create(desc))
		{
			return nullptr;
		}
		return program;
	}

	auto VulkanContext::create_compute_render_program(const ComputeProgramCreateDesc& desc) -> std::unique_ptr<IComputeRenderProgram>
	{
		auto program = std::make_unique<VulkanComputeRenderProgram>();
		if (!program->create(desc))
		{
			return nullptr;
		}
		return program;
	}

	auto VulkanContext::create_shader(const ShaderCreation& ci) -> std::shared_ptr<Shader> 
	{
		uint64_t uHashCode = get_shader_hash(ci);
		auto foundShader = vulkanShaderPool.find(uHashCode);
		if (foundShader != vulkanShaderPool.end())
		{
			return foundShader->second;
		}

		std::shared_ptr<Shader> pRetShader = nullptr;
		if (!pRetShader)
		{
			auto pVulkanShader = Ash_New_Shared<VulkanShader>();
			if (!pVulkanShader->init(ci))
			{
				return nullptr;
			}
#if defined(ASH_HAS_DXC)
			ShaderItem shader_item{};
			shader_item.sourceShaderPath = ci.pBaseShaderPath;
			shader_item.userShaderPath = ci.pUserShaderPath;
			shader_item.generatedBindingsPath = ci.pGeneratedBindingsPath;
			const std::string vulkan_shader_macro = append_vulkan_shader_macro(ci.pShaderMacro);
			shader_item.macroDefine = vulkan_shader_macro.c_str();
			shader_item.entryPoint = ci.pEntryPoint ? ci.pEntryPoint : "main";
			shader_item.stage = ci.type;

			ShaderFullTextResult shader_text_result{};
			ShaderFullTextResult* shader_text_result_ptr = &shader_text_result;
			AshDXCContext dxc_context{};
			if (!dxc_context.init())
			{
				return nullptr;
			}
			bool preprocess_ok = dxc_context.preprocess_shader_file_to_full_text(shader_item, &shader_text_result_ptr);
			dxc_context.uninit();
			if (!preprocess_ok)
			{
				if (!shader_text_result.errorMsg.empty())
				{
					HLogError("Shader preprocess diagnostics for {}:\n{}", ci.pBaseShaderPath ? ci.pBaseShaderPath : "<null>", shader_text_result.errorMsg);
				}
				HLogError("Failed to preprocess shader {}", ci.pBaseShaderPath ? ci.pBaseShaderPath : "<null>");
				return nullptr;
			}
			if (shader_text_result.resultShaderText.empty())
			{
				HLogError("Shader preprocess result is empty for {}", ci.pBaseShaderPath ? ci.pBaseShaderPath : "<null>");
				return nullptr;
			}
#else
			HLogError("Vulkan shader preprocessing requires DXC, but this build was compiled without ASH_HAS_DXC.");
			return nullptr;
#endif

			VulkanShaderCompiler shader_compiler{};
			if (!shader_compiler.init())
			{
				return nullptr;
			}
			bool compile_ok = shader_compiler.check_and_compile_shader(shader_item, shader_text_result.resultShaderText, pVulkanShader);
			shader_compiler.uninit();
			if (!compile_ok)
			{
				HLogError("Failed to compile shader {}", ci.pBaseShaderPath ? ci.pBaseShaderPath : "<null>");
				return nullptr;
			}

			pRetShader = pVulkanShader;
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
		const uint32_t extraUploadCommandCount = (uploadCommandsPending && !uploadCommandQueued) ? 1u : 0u;
		H_ASSERTLOG(commandBufferQueue.size() + info.cmdCount + extraUploadCommandCount <= k_command_buffer_queue_length, "Fatal: command count exceeds queue length.");
		if (uploadCommandsPending && !uploadCommandQueued)
		{
			_finalize_upload_command_buffer();
			H_ASSERTLOG(currentUploadCommandBuffer && currentUploadCommandBuffer->get_state() == AshCommandBufferState::ASH_Ended, "Fatal: upload command buffer must be ended before queue submit.");
			commandBufferQueue.push_back(currentUploadCommandBuffer);
			uploadCommandQueued = true;
		}
		//enqueue vkCommandBuffer
		for (size_t i = 0; i < info.cmdCount; i++)
		{
			H_ASSERTLOG(info.cmds[i].get_state() == AshCommandBufferState::ASH_Ended,
				"Fatal: The submitted command buffer must be ended before submit!");
			commandBufferQueue.push_back(static_cast<VulkanCommandBuffer*>(&info.cmds[i]));
		}
	}

	auto VulkanContext::submit_immediately(const SubmitInfo& info) -> void
	{
		H_ASSERTLOG(info.cmdCount == 1, " immediately submit can only submit one command once!");
		H_ASSERTLOG(info.cmds->get_state() == AshCommandBufferState::ASH_Ended, " comand buffer must be ended before submit!");
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
		ASH_PROFILE_SCOPE_NC("VulkanContext::begin_frame", AshEngine::Profile::Color::RHI);
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
				ASH_PROFILE_SCOPE_NC("VulkanContext::WaitFrameTimeline", AshEngine::Profile::Color::RHI);
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
			ASH_PROFILE_SCOPE_NC("VulkanContext::WaitFrameFence", AshEngine::Profile::Color::RHI);
			get_frame_data_internal().vulkanCommandBufferExecutedFence->wait_and_reset();
		}
		vulkanPresentCompleteSemaphore = get_frame_data_internal().vulkanRenderCompleteSemaphore;
		//flush deletion queue
		delayed_deletion_queues[currentFrame].flush();
		if (vulkanStagingBufferPool)
		{
			vulkanStagingBufferPool->frame_move();
		}
		commandBufferQueue.clear();
		//reset commandpool and commandbufferring
		commandBufferRing->reset(currentFrame);
		for (size_t i = 0; i < num_thread; i++)
		{
			FramePool& pool = framePools[currentFrame * num_thread + i];
			pool.cmdPool->reset();
		}
		currentUploadCommandBuffer = nullptr;
		uploadCommandsPending = false;
		uploadCommandQueued = false;
		frameActive = true;
		if (!_flush_pending_buffer_uploads())
		{
			HLogError("VulkanContext: Failed to flush pending buffer uploads for frame {}.", currentFrame);
		}
		if (!_flush_pending_texture_uploads())
		{
			HLogError("VulkanContext: Failed to flush pending texture uploads for frame {}.", currentFrame);
		}
	}

	auto VulkanContext::end_frame() -> void
	{
		ASH_PROFILE_SCOPE_NC("VulkanContext::end_frame", AshEngine::Profile::Color::RHI);
		// Tracy GPU collect 需要一个正在录制的 cmdbuf。复用 upload cmdbuf；
		// 没有就借机起一个，只为发 1 次 vkCmdResetQueryPool + 读 timestamp。
		if (auto* profiler = gpu_profiler_get())
		{
			if (_ensure_upload_command_buffer_recording() && currentUploadCommandBuffer)
			{
				profiler->collect(currentUploadCommandBuffer);
			}
		}

		if (uploadCommandsPending && !uploadCommandQueued)
		{
			_finalize_upload_command_buffer();
			if (currentUploadCommandBuffer && currentUploadCommandBuffer->get_state() == AshCommandBufferState::ASH_Ended)
			{
				commandBufferQueue.push_back(currentUploadCommandBuffer);
				uploadCommandQueued = true;
			}
		}
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
		const VkSemaphore presentCompleteSemaphore =
			vulkanPresentCompleteSemaphore != VK_NULL_HANDLE ?
			vulkanPresentCompleteSemaphore :
			get_frame_data_internal().vulkanRenderCompleteSemaphore;
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
				wait_semaphores.init(nullptr, 4, 0);
				wait_semaphores.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, get_frame_data_internal().vulkanRenderBeginSemaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 });
				if (wait_for_timeline_semaphore) {
					wait_semaphores.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkanGraphicsSemaphore, absoluteFrame - (k_max_frames - 1), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR , 0 });
				}
				VkSemaphoreSubmitInfoKHR signal_semaphores[]{
					{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, presentCompleteSemaphore, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0 },
					{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkanGraphicsSemaphore, absoluteFrame + 1, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR , 0 }
				};
				VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
				submit_info.waitSemaphoreInfoCount = wait_semaphores.size();
				submit_info.pWaitSemaphoreInfos = wait_semaphores.m_pData;
				submit_info.commandBufferInfoCount = count;
				submit_info.pCommandBufferInfos = command_buffer_info;
				submit_info.signalSemaphoreInfoCount = 2;
				submit_info.pSignalSemaphoreInfos = signal_semaphores;
				{
					ASH_PROFILE_SCOPE_NC("VulkanContext::QueueSubmit2", AshEngine::Profile::Color::RHI);
					ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(count));
					VK_CHECK_RESULT(vkQueueSubmit2KHR(vulkanMainQueue, 1, &submit_info, VK_NULL_HANDLE));
				}
				wait_semaphores.shutdown();
			}
			else
			{
				Array<VkSemaphore> wait_semaphores;
				wait_semaphores.init(nullptr, 4, 0);
				Array<uint64_t> wait_values;
				wait_values.init(nullptr, 4, 0);
				Array<VkPipelineStageFlags> wait_stages;
				wait_stages.init(nullptr, 4, 0);
				wait_semaphores.push_back(get_frame_data_internal().vulkanRenderBeginSemaphore);
				wait_values.push_back(0);
				wait_stages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
				if (wait_for_timeline_semaphore) {
					wait_semaphores.push_back(vulkanGraphicsSemaphore);
					wait_values.push_back(absoluteFrame - (k_max_frames - 1));
					wait_stages.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
				}
				VkSemaphore signal_semaphores[] = { presentCompleteSemaphore, vulkanGraphicsSemaphore };
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
				{
					ASH_PROFILE_SCOPE_NC("VulkanContext::QueueSubmit", AshEngine::Profile::Color::RHI);
					ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(count));
					VK_CHECK_RESULT(vkQueueSubmit(vulkanMainQueue, 1, &submit_info, VK_NULL_HANDLE));
				}
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
				wait_semaphores.init(nullptr, 4, 0);
				wait_semaphores.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, get_frame_data_internal().vulkanRenderBeginSemaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 });
				VkSemaphoreSubmitInfoKHR signal_semaphores[]{
					{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, presentCompleteSemaphore, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0 },
				};
				VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
				submit_info.waitSemaphoreInfoCount = wait_semaphores.size();
				submit_info.pWaitSemaphoreInfos = wait_semaphores.m_pData;
				submit_info.commandBufferInfoCount = count;
				submit_info.pCommandBufferInfos = command_buffer_info;
				submit_info.signalSemaphoreInfoCount = 1;
				submit_info.pSignalSemaphoreInfos = signal_semaphores;
				{
					ASH_PROFILE_SCOPE_NC("VulkanContext::QueueSubmit2", AshEngine::Profile::Color::RHI);
					ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(count));
					VK_CHECK_RESULT(vkQueueSubmit2KHR(vulkanMainQueue, 1, &submit_info, get_frame_data_internal().vulkanCommandBufferExecutedFence->get_handle()));
				}
				wait_semaphores.shutdown();
			}
			else
			{
				VkPipelineStageFlags flag = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				get_frame_data_internal().vulkanCommandBufferExecutedFence->reset();
				VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
				submit_info.waitSemaphoreCount = 1;
				submit_info.pWaitSemaphores = &get_frame_data_internal().vulkanRenderBeginSemaphore;
				submit_info.pWaitDstStageMask = &flag;
				submit_info.commandBufferCount = count;
				submit_info.pCommandBuffers = cmds;
				submit_info.signalSemaphoreCount = 1;
				submit_info.pSignalSemaphores = &presentCompleteSemaphore;
				{
					ASH_PROFILE_SCOPE_NC("VulkanContext::QueueSubmit", AshEngine::Profile::Color::RHI);
					ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(count));
					VK_CHECK_RESULT(vkQueueSubmit(vulkanMainQueue, 1, &submit_info, get_frame_data_internal().vulkanCommandBufferExecutedFence->get_handle()));
				}
			}		
		}
		frameActive = false;
		currentUploadCommandBuffer = nullptr;
		uploadCommandsPending = false;
		uploadCommandQueued = false;
	}

	/********************************************************** RHI INTERFACE ******************************************************************************************************/

	VulkanContext* VulkanContext::instance = nullptr;
};

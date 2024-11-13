#pragma once
#include "VulkanWrapper.h"
#include "VulkanContext.h"
#include "Base/hlog.h"
namespace RHI
{
	auto GetRequiredLayers() -> std::vector<const char*>&
	{
		return std::vector<const char*>();
	}

	auto GetRequiredExtensions() -> std::vector<const char*>&
	{
		return std::vector<const char*>();

	}

	auto CheckValidationLayerSupport(std::vector<VkLayerProperties>& i, std::vector<const char*>& in)  -> bool
	{
		return false;

	}
	auto CheckExtensionSupport(std::vector<VkExtensionProperties>& i, std::vector<const char*>& in) -> bool
	{
		return false;

	}
	auto VulkanInstance::Init(void* config) -> void
	{
		instanceLayerNames = GetRequiredLayers();
		instanceExtensionNames = GetRequiredExtensions();
#ifdef HASHEA_DEBUG
		if (!CheckValidationLayerSupport(instanceLayers, instanceLayerNames))
		{
			HLogError("[VULKAN] Validation layers requested, but not available!");
		}
#endif // HASHEA_DEBUG
		if (!CheckExtensionSupport(instanceExtensions, instanceExtensionNames))
		{
			HLogError("[VULKAN] Extensions requested are not available!");
		}

		VkApplicationInfo appInfo = {};
		appInfo.pApplicationName = "APP";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Hashea Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_1;

		VkInstanceCreateInfo createInfo = {};
		createInfo.pApplicationInfo = &appInfo;
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensionNames.size());
		createInfo.ppEnabledExtensionNames = instanceExtensionNames.data();
		createInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayerNames.size());
		createInfo.ppEnabledLayerNames = instanceLayerNames.data();

		VkValidationFeatureEnableEXT enabled[] = { VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT };
		VkValidationFeaturesEXT      features{ VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
		features.disabledValidationFeatureCount = 0;
		features.enabledValidationFeatureCount = 1;
		features.pDisabledValidationFeatures = nullptr;
		features.pEnabledValidationFeatures = enabled;
		features.pNext = createInfo.pNext;

		createInfo.pNext = &features;
		vkCreateInstance(&createInfo, nullptr, &instance);
		//VK_CHECK_RESULT();

		//load instance apis
		volkLoadInstance(instance);
	}
	auto VulkanInstance::Shutdown() -> void
	{
	}
	auto VulkanPhysicalDevice::Init(void* config) -> void
	{
	}
	auto VulkanPhysicalDevice::Shutdown() -> void
	{
	}
	auto VulkanDevice::Init(void* config) -> void
	{
	}
	auto VulkanDevice::Shutdown() -> void
	{
	}
	auto VulkanContext::Init(void* config) -> void
	{	
		////load vulkan
		if (auto const res = volkInitialize(); VK_SUCCESS != res)
		{
			HLogError("Error: unable to load Vulkan API\n");

			return;
		}
	}
	auto VulkanContext::Shutdown() -> void
	{
	}
};
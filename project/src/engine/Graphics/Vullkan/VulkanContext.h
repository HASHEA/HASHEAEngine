#pragma once
#include "Base/hcore.h"
#include <vector>
namespace RHI
{
	class VulkanInstance
	{
	public:
		VulkanInstance() {}
		~VulkanInstance() {}
		NO_COPYABLE(VulkanInstance);

		auto Init(void* config = nullptr) -> void;
		auto Shutdown() -> void;
	private:
		std::vector<const char*>          instanceLayerNames;
		std::vector<const char*>          instanceExtensionNames;
		std::vector<VkLayerProperties>     instanceLayers;
		std::vector<VkExtensionProperties> instanceExtensions;
		VkInstance instance = VK_NULL_HANDLE;
	};

	class VulkanPhysicalDevice
	{
	public:
		VulkanPhysicalDevice() {}
		~VulkanPhysicalDevice() {}

		auto Init(void* config = nullptr) -> void;
		auto Shutdown() -> void;

		NO_COPYABLE(VulkanPhysicalDevice);
	private:
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

	};

	class VulkanDevice
	{
	public:
		VulkanDevice() {}
		~VulkanDevice() {}

		auto Init(void* config = nullptr) -> void;
		auto Shutdown() -> void;

		NO_COPYABLE(VulkanDevice);
	private:

		VkDevice device = VK_NULL_HANDLE;
	};

	class VulkanContext
	{
	public:
		auto Init(void* config = nullptr) -> void;
		auto Shutdown() -> void;

		VulkanContext() {}
		~VulkanContext() {}
	};

};
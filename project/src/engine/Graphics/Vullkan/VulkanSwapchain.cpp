#pragma once
#include "VulkanSwapchain.h"
#include "VulkanContext.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
#include "Base/hassert.h"
#include <GLFW/glfw3.h> // must include after include vulkan because some key macro should be pre defined
#include <GLFW/glfw3native.h>
namespace RHI
{
	VulkanSwapChain::VulkanSwapChain(uint32_t width, uint32_t height)
		:width(width), height(height)
	{
	}

	VulkanSwapChain::~VulkanSwapChain()
	{
	}

	auto VulkanSwapChain::init(void* config) -> HS_Result 
	{
		_create_surface((GLFWwindow*)config);
		_create_swapchain();
		return HS_OK;
	}

	auto VulkanSwapChain::shutdown() -> HS_Result 
	{
		return HS_OK;
	}

	auto VulkanSwapChain::_create_surface(GLFWwindow* window) -> HS_Result
	{
		auto instance = VulkanContext::get_vulkan_instance();
		VK_CHECK_RESULT(glfwCreateWindowSurface(instance, window, nullptr, &surface));

		return HS_OK;
	}

	auto VulkanSwapChain::_create_swapchain() -> HS_Result
	{
		SwapChainSupportDetails swapChainSupport;
		swapChainSupport = _query_swapchain_support();
		_choose_swap_surface_format(swapChainSupport.formats);
		_choose_swap_present_mode(swapChainSupport.presentModes);
		_choose_swap_extent(swapChainSupport.capabilities);
		//uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
		//if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
		//	imageCount = swapChainSupport.capabilities.maxImageCount;
		//}

		H_ASSERTLOG(swapChainSupport.capabilities.maxImageCount >= MAX_SWAPCHAIN_BUFFERS && swapChainSupport.capabilities.minImageCount <= MAX_SWAPCHAIN_BUFFERS, "Unsupported Image Count:{}!", MAX_SWAPCHAIN_BUFFERS);

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = MAX_SWAPCHAIN_BUFFERS;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0; // Optional
		createInfo.pQueueFamilyIndices = nullptr; // Optional
		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		auto device = VulkanContext::get_vulkan_device();

		VK_CHECK_RESULT(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain));

		return HS_Result();
	}

	auto VulkanSwapChain::_query_swapchain_support() -> SwapChainSupportDetails
	{
		SwapChainSupportDetails swapChainSupport;

		auto device = VulkanContext::get_vulkan_physical_device();
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &swapChainSupport.capabilities);
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

		if (formatCount != 0) {
			swapChainSupport.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, swapChainSupport.formats.data());
		}

		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

		if (presentModeCount != 0) {
			swapChainSupport.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, swapChainSupport.presentModes.data());
		}
		return swapChainSupport;
	}

	auto VulkanSwapChain::_choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) -> HS_Result
	{
		for (const auto& availableFormat : availableFormats) {
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				surfaceFormat = availableFormat;
			}
		}
		surfaceFormat = availableFormats[0];
		return HS_OK;
	}

	auto VulkanSwapChain::_choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes) -> HS_Result
	{
		for (const auto& availablePresentMode : availablePresentModes) {
			if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
				presentMode = availablePresentMode;
			}
		}
		presentMode = VK_PRESENT_MODE_FIFO_KHR;
		return HS_OK;
	}

	auto VulkanSwapChain::_choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) -> HS_Result
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			extent = capabilities.currentExtent;
		}
		else {
			//int width, height;
			//auto window = (GLFWwindow*)Application::GetWindow()->get_native_interface();

			//glfwGetFramebufferSize(window, &width, &height);

			VkExtent2D actualExtent = {
				static_cast<uint32_t>(width),
				static_cast<uint32_t>(height)
			};

			actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

			extent = actualExtent;
		}
		return HS_OK;
	}

}
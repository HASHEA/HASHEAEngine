#pragma once
#include "VulkanSwapchain.h"
#include "VulkanContext.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
#include "Base/hassert.h"
#include <GLFW/glfw3.h> // must include after include vulkan because some key macro should be pre defined
#include <GLFW/glfw3native.h>
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanBuffer.h"
namespace RHI
{
	VulkanSwapchain::VulkanSwapchain()
	{
	}

	VulkanSwapchain::~VulkanSwapchain()
	{
	}

	auto VulkanSwapchain::init(void* _config) -> HS_Result 
	{
		SwapChainInitConfig config = *(SwapChainInitConfig*)_config;
		width = config.width;
		H_ASSERT(width > 0);
		height = config.height;
		H_ASSERT(height > 0);
		if (config.colorFormatCount > 0)
		{
			H_ASSERTLOG(config.pColorFormat, "pColorFormat is nullptr but colorFormatCount > 0!");
		}
		if (config.colorSpaceCount > 0)
		{
			H_ASSERTLOG(config.pColorSpace, "pColorSpace is nullptr but colorSpaceCount > 0!");
		}
		if (config.presentModeCount > 0)
		{
			H_ASSERTLOG(config.pPresentMode, "pPresentMode is nullptr but presentModeCount > 0!");
		}
		HLogTrace("Create swapchain ...");
		_create_surface((GLFWwindow*)config.window);
		_create_swapchain(config);
		//test texture creation
		TextureCreation tc{};
		tc.width = 1920;
		tc.height = 1080;
		tc.depth = 1;
		tc.array_layer_count = 1;
		tc.alias = nullptr;
		tc.flags = 0;
		tc.initial_data = nullptr;
		tc.mip_level_count = 1;
		tc.name = "test vk texture";
		tc.type = Ash_Texture2D;
		tc.format = ASH_FORMAT_R8G8B8A8_SRGB;
		auto testVKTexture = VulkanTexture::create(tc);
		SamplerCreation sc{};
		sc.name = "test sampler";
		auto testSampler = VulkanSampler::create(sc);
		VulkanContext::get()->destroy_rhi_resource_Immediately(testSampler);

		BufferCreation bc{};
		bc.type_flags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bc.initial_data = nullptr;
		bc.size = 1024;
		bc.usage = AshResourceUsageType::Immutable;
		bc.name = "test non dynamic buffer";
		auto testBuffer1 = VulkanBuffer::create(bc);
		bc.usage = AshResourceUsageType::Dynamic;
		bc.type_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		bc.name = "test dynamic buffer";
		auto testBuffer2 = VulkanBuffer::create(bc);
		return HS_OK;
	}

	auto VulkanSwapchain::shutdown() -> HS_Result 
	{
		_clean_swapchain();
		if (surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(VulkanContext::get_vulkan_instance(), surface, VulkanContext::get_vulkan_allocation_callbacks());
		}
		return HS_OK;
	}

	auto VulkanSwapchain::_create_surface(GLFWwindow* window) -> HS_Result
	{
		auto instance = VulkanContext::get_vulkan_instance();
		VK_CHECK_RESULT(glfwCreateWindowSurface(instance, window, VulkanContext::get_vulkan_allocation_callbacks(), &surface));
		return HS_OK;
	}

	auto VulkanSwapchain::_create_swapchain(SwapChainInitConfig& config) -> HS_Result
	{
		SwapChainSupportDetails swapChainSupport{};
		_query_swapchain_support(swapChainSupport);
		bool bFound = false;
		uint32_t i = 0;
		for (i = 0; i < config.colorFormatCount; i++)
		{
			for (const auto& availableFormat : swapChainSupport.formats) {
				if (availableFormat.format == ash_format_to_vk(config.pColorFormat[i])) {
					uint32_t j = 0;
					for (j = 0; j < config.colorSpaceCount; j++)
					{
						if (availableFormat.colorSpace == ash_color_space_to_vk(config.pColorSpace[j]))
						{
							surfaceFormat = availableFormat;
							bFound = true;
							break;
						}
					}
					if (bFound)
					{		
						break;
					}
				}
			}
			if (bFound)
			{
				break;
			}
		}
		if (!bFound)
		{
			surfaceFormat = swapChainSupport.formats[0];
			HLogWarning("none of the required formats is supported! use the default format at index 0 ! ");
		}
		bFound = false;
		i = 0;
		for (i = 0; i < config.presentModeCount; i++)
		{
			for (const auto& availablePresentMode : swapChainSupport.presentModes) {
				if (availablePresentMode == ash_present_mode_to_vk(config.pPresentMode[i])) {
					presentMode = availablePresentMode;
					bFound = true;
					break;
				}
			}
			if (bFound)
			{
				break;
			}
		}
		if (!bFound)
		{
			presentMode = VK_PRESENT_MODE_FIFO_KHR;
			HLogWarning("none of the required presentModes is supported! use the default presentmode : {} ! ", TYPE_TO_STRING(VK_PRESENT_MODE_FIFO_KHR));
		}

		_choose_swap_extent(swapChainSupport.capabilities);
		H_ASSERTLOG(swapChainSupport.capabilities.maxImageCount >= MAX_SWAPCHAIN_BUFFERS && swapChainSupport.capabilities.minImageCount <= MAX_SWAPCHAIN_BUFFERS, "Unsupported Image Count:{}!", MAX_SWAPCHAIN_BUFFERS);
		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = MAX_SWAPCHAIN_BUFFERS;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0; // Optional
		createInfo.pQueueFamilyIndices = nullptr; // Optional
		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;
		auto device = VulkanContext::get_vulkan_device();
		VK_CHECK_RESULT(vkCreateSwapchainKHR(device, &createInfo, VulkanContext::get_vulkan_allocation_callbacks(), &swapChain));

		std::vector<VkImage> vecVkImage;
		uint32_t imageCount;
		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
		vecVkImage.resize(imageCount);
		swapChainImages.init(nullptr, imageCount, 0);
		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, vecVkImage.data());
		for (size_t i = 0; i < imageCount; i++)
		{
			std::shared_ptr<VulkanTexture> texture = Ash_New_Shared<VulkanTexture>();
			texture->width = width;
			texture->height = height;
			texture->aliasTexture = nullptr;
			texture->format = vk_format_to_ash(surfaceFormat.format);
			texture->render_target = 1;
			texture->name = "swapchain buffer";
			texture->swapchain_texture = true;
			texture->vkImage = vecVkImage[i];
			swapChainImages.push_back(texture);
			texture->init();
		}
		return HS_OK;
	}

	auto VulkanSwapchain::_query_swapchain_support(SwapChainSupportDetails& swapChainSupport) -> void
	{
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
	}

	auto VulkanSwapchain::_choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) -> HS_Result
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			extent = capabilities.currentExtent;
		}
		else {
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

	auto VulkanSwapchain::_recreate_swapchain()
	{
		auto device = VulkanContext::get_vulkan_device();
		vkDeviceWaitIdle(device);

		//_create_swapchain();
	}

	auto VulkanSwapchain::_clean_swapchain() ->HS_Result
	{
		auto device = VulkanContext::get_vulkan_device();

		for (size_t i = 0; i < swapChainImages.size(); i++)
		{
			swapChainImages[i].reset();
		}
		swapChainImages.shutdown();
		if (swapChain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(device, swapChain, VulkanContext::get_vulkan_allocation_callbacks());
		}
		return HS_OK;
	}

	auto VulkanSwapchain::get_width() -> uint32_t
	{
		return 0;
	}

	auto VulkanSwapchain::get_height() -> uint32_t
	{
		return 0;
	}

	auto VulkanSwapchain::get_swapchain_buffer() -> std::shared_ptr<Texture>
	{
		return std::shared_ptr<Texture>();
	}

	auto VulkanSwapchain::get_swapchain_buffer(uint32_t index) -> std::shared_ptr<Texture>
	{
		return std::shared_ptr<Texture>();
	}

	auto VulkanSwapchain::resize_swapchain() -> void
	{
	}

	auto VulkanSwapchain::present() -> void
	{
	}

}
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
#include "VulkanCommandBuffer.h"
#include "VulkanRenderPass.h"
#include "VulkanFramebuffer.h"
namespace RHI
{
	VulkanSwapchain::VulkanSwapchain()
	{
	}

	VulkanSwapchain::~VulkanSwapchain()
	{
	}

	auto VulkanSwapchain::init(void* _config) -> bool 
	{
		auto config = *(SwapChainInitConfig*)_config;
		swapchainBufferCount = config.swapchainBufferCount;
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
		_init_swapchain(config);
		return true;
	}

	auto VulkanSwapchain::shutdown() -> bool 
	{
		_clean_swapchain(swapChain);
		if (surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(VulkanContext::get_vulkan_instance(), surface, VulkanContext::get_vulkan_allocation_callbacks());
		}
		return true;
	}

	auto VulkanSwapchain::_create_surface(GLFWwindow* window) -> bool
	{
		auto instance = VulkanContext::get_vulkan_instance();
		VK_CHECK_RESULT(glfwCreateWindowSurface(instance, window, VulkanContext::get_vulkan_allocation_callbacks(), &surface));
		return true;
	}

	auto VulkanSwapchain::_init_swapchain(SwapChainInitConfig& config) -> bool
	{
		SwapChainSupportDetails swapChainSupport{};
		_query_swapchain_support(swapChainSupport);
		preTransform = swapChainSupport.capabilities.currentTransform;
		bool bFound = false;
		uint32_t i = 0;
		for (i = 0; i < config.colorFormatCount; i++)
		{
			for (const auto& availableFormat : swapChainSupport.formats) {
				if (availableFormat.format == get_vk_texture_format_info(config.pColorFormat[i]).vkFormat) {
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
		H_ASSERTLOG(swapChainSupport.capabilities.maxImageCount >= swapchainBufferCount && swapChainSupport.capabilities.minImageCount <= swapchainBufferCount, "Unsupported Image Count:{}!", swapchainBufferCount);
		_recreate_swapchain();
		return true;
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

	auto VulkanSwapchain::_choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) -> bool
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			swapchainExtents = capabilities.currentExtent;
		}
		else {
			swapchainExtents.width = std::clamp(swapchainExtents.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			swapchainExtents.height = std::clamp(swapchainExtents.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}
		return true;
	}

	auto VulkanSwapchain::_recreate_swapchain() -> void
	{
		VkSurfaceCapabilitiesKHR surface_properties{};
		VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VulkanContext::get_vulkan_physical_device(), surface, &surface_properties));	
		if (surface_properties.currentExtent.width == swapchainExtents.width &&
			surface_properties.currentExtent.height == swapchainExtents.height)
		{
			return;
		}
		_choose_swap_extent(surface_properties);
		oldSwapChain = swapChain;
		swapChain = VK_NULL_HANDLE;
		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = swapchainBufferCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = swapchainExtents;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0; // Optional
		createInfo.pQueueFamilyIndices = nullptr; // Optional
		createInfo.preTransform = preTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = oldSwapChain;
		VK_CHECK_RESULT(vkCreateSwapchainKHR(VulkanContext::get_vulkan_device(), &createInfo, VulkanContext::get_vulkan_allocation_callbacks(), &swapChain));

		if (oldSwapChain != VK_NULL_HANDLE)
		{
			_clean_swapchain(oldSwapChain);
		}

		std::vector<VkImage> vecVkImage;
		uint32_t imageCount;
		vkGetSwapchainImagesKHR(VulkanContext::get_vulkan_device(), swapChain, &imageCount, nullptr);
		vecVkImage.resize(imageCount);
		swapChainImages.init(nullptr, imageCount, 0);
		swapChainFramebuffer.init(nullptr, imageCount, 0);
		vkGetSwapchainImagesKHR(VulkanContext::get_vulkan_device(), swapChain, &imageCount, vecVkImage.data());
		//create render pass
		RenderPassCreation rci{};
		rci.set_name("swapchain render pass").add_attachment(vk_format_to_ash(surfaceFormat.format), AshResourceState::Present, AshLoadOption::ASH_LOAD_CLEAR);
			/*set_depth_stencil_texture(AshFormat::ASH_FORMAT_D32_SFLOAT, AshResourceState::ASH_RESOURCE_STATE_DEPTH_STENCIL_WRITE).set_depth_stencil_operations(ASH_LOAD_CLEAR, ASH_LOAD_CLEAR);*/
		swapchainRenderPass = VulkanRenderPass::create(rci);
		//create swapchain buffer proxy
		
		
		for (size_t i = 0; i < imageCount; i++)
		{
			std::shared_ptr<VulkanTexture> texture = Ash_New_Shared<VulkanTexture>();
			texture->m_sCreation.width = swapchainExtents.width;
			texture->m_sCreation.height = swapchainExtents.height;
			texture->aliasTexture = nullptr;
			texture->m_sCreation.format = vk_format_to_ash(surfaceFormat.format);
			texture->m_sCreation.name = "swapchain buffer";
			texture->swapchain_texture = true;
			texture->vkImage = vecVkImage[i];
			swapChainImages.push_back(texture);
			texture->init();
			//create swapchain framebuffers
			FramebufferCreation fci{};
			fci.name = "swapchain framebuffer";
			fci.renderPass = swapchainRenderPass;
			fci.width = swapchainExtents.width;
			fci.height = swapchainExtents.height;
			fci.colorAttachments.init(nullptr, imageCount);
			fci.colorAttachments.push_back(texture);
			fci.layers = 1;
			swapChainFramebuffer.push_back(VulkanFramebuffer::create(fci));
			fci.colorAttachments.shutdown();
		}
	
	}

	auto VulkanSwapchain::_clean_swapchain(VkSwapchainKHR& _swapchain) ->bool
	{
		vkDeviceWaitIdle(VulkanContext::get_vulkan_device());
		swapChainFramebuffer.shutdown();
		swapChainImages.shutdown();
		swapchainRenderPass.reset();
		if (_swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(VulkanContext::get_vulkan_device(), _swapchain, VulkanContext::get_vulkan_allocation_callbacks());
			_swapchain = VK_NULL_HANDLE;
		}
		return true;
	}

	auto VulkanSwapchain::_aquire_next_image() -> void
	{
		if (swapchainBufferCount == 1 && acquireImageIndex != UINT32_MAX)
		{
			return;
		}
		VkResult result = vkAcquireNextImageKHR(VulkanContext::get_vulkan_device(), swapChain, UINT64_MAX, VulkanContext::get_frame_data().vulkanRenderBeginSemaphore, VK_NULL_HANDLE, &acquireImageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			HLogWarning("Acquire Image result : {0}", result == VK_ERROR_OUT_OF_DATE_KHR ? "Out of Date" : "SubOptimal");

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				_recreate_swapchain();
				result = vkAcquireNextImageKHR(VulkanContext::get_vulkan_device(), swapChain, UINT64_MAX, VulkanContext::get_frame_data().vulkanRenderBeginSemaphore, VK_NULL_HANDLE, &acquireImageIndex);
			}
			return;
		}
		VK_CHECK_RESULT(result);
	}

	auto VulkanSwapchain::get_swapchain_buffer_count() -> uint8_t
	{
		return swapchainBufferCount;
	}

	auto VulkanSwapchain::begin_frame() -> void
	{
		_aquire_next_image();
	}

	auto VulkanSwapchain::end_frame() -> void
	{
	}

	auto VulkanSwapchain::get_width() -> uint32_t
	{
		return swapchainExtents.width;
	}

	auto VulkanSwapchain::get_height() -> uint32_t
	{
		return swapchainExtents.height;
	}

	auto VulkanSwapchain::get_swapchain_buffer() -> std::shared_ptr<Texture>
	{
		uint32_t curFrame = acquireImageIndex;
		return swapChainImages[curFrame];
	}

	auto VulkanSwapchain::get_swapchain_buffer(uint32_t index) -> std::shared_ptr<Texture>
	{
		H_ASSERT(index < swapChainImages.size());
		return swapChainImages[index];
	}

	auto VulkanSwapchain::resize_swapchain(uint32_t i_width, uint32_t i_height) -> void
	{
		HLogInfo("resize swapchain to width  : {}, height : {}", i_width, i_height);
		if (i_width == 0 || i_height == 0)
		{
			return;
		}
		_recreate_swapchain();
	}

	auto VulkanSwapchain::present() -> void
	{
		VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain;
		presentInfo.pImageIndices = &acquireImageIndex;
		//TEST PERIOD
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &(VulkanContext::get_frame_data().vulkanRenderCompleteSemaphore);
		presentInfo.pResults = nullptr;
		//TODO: deal the image layout problem
		//if layout != present_src, do transition.
		auto swapchainBuffer = get_swapchain_buffer();
		{
			auto cb = VulkanContext::get()->get_command_buffer(0);
			cb->begin_record();
			cb->cmd_transition_resource_state({swapchainBuffer, AshResourceState::Present});
			cb->end_record();
			VulkanContext::get()->submit_immediately({ cb ,1});
		}

		VkResult result = vkQueuePresentKHR(VulkanContext::get_present_queue(), &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			_recreate_swapchain();
		}
	}

}
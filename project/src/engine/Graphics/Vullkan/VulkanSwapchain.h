#pragma once
#include "VulkanWrapper.h"
#include "VulkanHelper.hpp"

#include "Graphics/Swapchain.h"
using namespace AshEngine;

class GLFWwindow;
namespace RHI
{
	class VulkanSwapChain :public Swapchain
	{
		struct SwapChainSupportDetails {
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};
	public:
		VulkanSwapChain(uint32_t width, uint32_t height);
		~VulkanSwapChain();

		virtual auto init(void* config)->HS_Result override;
		virtual auto shutdown()->HS_Result override;

	private:
		auto _create_surface(GLFWwindow* window)->HS_Result;
		auto _create_swapchain()->HS_Result;
		auto _query_swapchain_support(SwapChainSupportDetails& swapChainSupport)->void;
		auto _choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats)->HS_Result;
		auto _choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes)->HS_Result;
		auto _choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities)->HS_Result;

	private:
		uint32_t width = 0;
		uint32_t height = 0;
		VkSurfaceKHR    surface = VK_NULL_HANDLE;
		VkSurfaceFormatKHR surfaceFormat;
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		VkExtent2D extent;
		VkSwapchainKHR swapChain;

	};
}

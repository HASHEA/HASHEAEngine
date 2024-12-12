#pragma once
#include "VulkanWrapper.h"
#include "VulkanHelper.hpp"
#include "Base/ds/harray.hpp"
#include "Graphics/Swapchain.h"
using namespace AshEngine;

class GLFWwindow;
namespace RHI
{
	class VulkanTexture;
	class VulkanSwapchain :public Swapchain
	{
		struct SwapChainSupportDetails {
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};
	public:
		VulkanSwapchain();
		~VulkanSwapchain();

		virtual auto init(void* config)->HS_Result override;
		virtual auto shutdown()->HS_Result override;

	private:
		auto _create_surface(GLFWwindow* window)->HS_Result;
		auto _create_swapchain(SwapChainInitConfig& config)->HS_Result;
		auto _query_swapchain_support(SwapChainSupportDetails& swapChainSupport)->void;
		auto _choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities)->HS_Result;
		auto _recreate_swapchain();
		auto _clean_swapchain() -> HS_Result;
	private:
		uint32_t width = 0;
		uint32_t height = 0;
		VkSurfaceKHR    surface = VK_NULL_HANDLE;
		VkSurfaceFormatKHR surfaceFormat{};
		VkPresentModeKHR presentMode{};
		VkExtent2D extent{};
		VkSwapchainKHR swapChain = VK_NULL_HANDLE;
		Array<std::shared_ptr<VulkanTexture>> swapChainImages;
	};
}

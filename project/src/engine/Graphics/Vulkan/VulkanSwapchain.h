#pragma once
#include "VulkanWrapper.h"
#include "VulkanHelper.hpp"
#include "Base/ds/harray.hpp"
#include "Graphics/Swapchain.h"
#include <deque>
using namespace AshEngine;

class GLFWwindow;
namespace RHI
{
	class Texture;
	class VulkanTexture;
	class VulkanRenderPass;
	class VulkanFramebuffer;
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

		virtual auto init(void* config)->bool override;
		virtual auto shutdown()->bool override;
		virtual auto destroy() -> void override;
	public:
		auto resize_swapchain(uint32_t width, uint32_t height) -> void override;
		auto present() -> void override;
		auto get_swapchain_buffer() -> std::shared_ptr<Texture> override;
		auto get_swapchain_buffer(uint32_t index) -> std::shared_ptr<Texture> override;
		auto get_format() -> AshFormat override { return vk_format_to_ash(surfaceFormat.format); }
		auto get_width() -> uint32_t override;
		auto get_height() -> uint32_t override;
		auto begin_frame() -> void override;
		auto end_frame() -> void override;
		auto get_swapchain_buffer_count() -> uint8_t override;
		static auto should_recreate_for_surface_extent(
			bool hasSwapchain,
			bool forceRecreate,
			const VkExtent2D& currentSurfaceExtent,
			const VkExtent2D& activeSwapchainExtent) -> bool;
	public:
		//vulkan only
		auto get_vk_suface()
		{
			return surface;
		}
	private:
		auto _create_surface(GLFWwindow* window)->bool;
		auto _init_swapchain(SwapChainInitConfig& config)->bool;
		auto _query_swapchain_support(SwapChainSupportDetails& swapChainSupport)->void;
		auto _choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities)->bool;
		auto _recreate_swapchain(bool forceRecreate = false) -> void;
		auto _clean_swapchain(VkSwapchainKHR& _swapchain) -> bool;
		auto _aquire_next_image() -> void;
	private:
		uint8_t swapchainBufferCount = 0;
		VkSurfaceKHR    surface = VK_NULL_HANDLE;
		VkSurfaceFormatKHR surfaceFormat{};
		VkPresentModeKHR presentMode{};
		VkExtent2D swapchainExtents{};
		VkSurfaceTransformFlagBitsKHR    preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		VkSwapchainKHR swapChain = VK_NULL_HANDLE;
		VkSwapchainKHR oldSwapChain = VK_NULL_HANDLE;
		Array<std::shared_ptr<VulkanTexture>> swapChainImages;
		Array<std::shared_ptr<VulkanFramebuffer>> swapChainFramebuffer;
		Array<VkSemaphore> swapChainRenderCompleteSemaphores;
		uint32_t acquireImageIndex = UINT32_MAX;
		std::shared_ptr<VulkanRenderPass> swapchainRenderPass = nullptr;

		

	};
}

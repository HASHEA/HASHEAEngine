#pragma once
#include "VulkanWrapper.h"
#include "VulkanHelper.hpp"

#include "Graphics/Swapchain.h"
using namespace AshEngine;

namespace RHI
{
	class VulkanSwapChain :public Swapchain
	{
	public:
		VulkanSwapChain(uint32_t width, uint32_t height);
		~VulkanSwapChain();

		virtual auto init(void* config)->HS_Result override;
		virtual auto shutdown()->HS_Result override;

	private:
		auto _create_surface()->HS_Result;
	private:
		uint32_t width = 0;
		uint32_t height = 0;
		VkSurfaceKHR    surface = VK_NULL_HANDLE;

	};
}

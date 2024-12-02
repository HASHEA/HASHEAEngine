#pragma once
#include "VulkanSwapchain.h"
#include "VulkanContext.h"
#include "Base/window/Window.h"
#include "Function/Application.h"
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
		return HS_OK;
	}

	auto VulkanSwapChain::shutdown() -> HS_Result 
	{
		return HS_OK;
	}

	auto VulkanSwapChain::_create_surface() -> HS_Result
	{
		auto instance = VulkanContext::get_vulkan_instance();
		auto window = (GLFWwindow*)Application::GetWindow()->get_native_interface();
		VK_CHECK_RESULT(glfwCreateWindowSurface(instance, window, nullptr, &surface));

		return HS_OK;
	}

}
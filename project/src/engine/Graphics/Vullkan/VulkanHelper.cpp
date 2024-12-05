#pragma once
#include "VulkanHelper.hpp"
namespace RHI
{
	const char* vulkan_error_string(VkResult errorCode)
	{
		switch (errorCode)
		{
#define STR(r)   \
	case VK_##r: \
		return #r
			STR(NOT_READY);
			STR(TIMEOUT);
			STR(EVENT_SET);
			STR(EVENT_RESET);
			STR(INCOMPLETE);
			STR(ERROR_OUT_OF_HOST_MEMORY);
			STR(ERROR_OUT_OF_DEVICE_MEMORY);
			STR(ERROR_INITIALIZATION_FAILED);
			STR(ERROR_DEVICE_LOST);
			STR(ERROR_MEMORY_MAP_FAILED);
			STR(ERROR_LAYER_NOT_PRESENT);
			STR(ERROR_EXTENSION_NOT_PRESENT);
			STR(ERROR_FEATURE_NOT_PRESENT);
			STR(ERROR_INCOMPATIBLE_DRIVER);
			STR(ERROR_TOO_MANY_OBJECTS);
			STR(ERROR_FORMAT_NOT_SUPPORTED);
			STR(ERROR_SURFACE_LOST_KHR);
			STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
			STR(SUBOPTIMAL_KHR);
			STR(ERROR_OUT_OF_DATE_KHR);
			STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
			STR(ERROR_VALIDATION_FAILED_EXT);
			STR(ERROR_INVALID_SHADER_NV);
#undef STR
		default:
			return "UNKNOWN_ERROR";
		}
	}

	auto ash_format_to_vk(const AshFormat& format) -> VkFormat
	{
		VkFormat vkFormat = VkFormat::VK_FORMAT_UNDEFINED;
		switch (format)
		{
		case ASH_FORMAT_B8G8R8A8_SRGB:
			vkFormat = VkFormat::VK_FORMAT_B8G8R8A8_SRGB;
			break;
		}
		return vkFormat;
	}

	auto ash_color_space_to_vk(const AshColorSpace& colorSpace) -> VkColorSpaceKHR
	{
		VkColorSpaceKHR vkColorSpace = VkColorSpaceKHR::VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		switch (colorSpace)
		{
		case ASH_COLOR_SPACE_SRGB_NONLINEAR_KHR:
			vkColorSpace = VkColorSpaceKHR::VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			break;
		}
		return vkColorSpace;
	}

	auto ash_present_mode_to_vk(const AshPresentMode& presentMode) -> VkPresentModeKHR
	{
		VkPresentModeKHR vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_IMMEDIATE_KHR;
		switch (presentMode)
		{
		case ASH_PRESENT_MODE_MAILBOX_KHR:
			vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_MAILBOX_KHR;
			break;
		case ASH_PRESENT_MODE_IMMEDIATE_KHR:
			vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_IMMEDIATE_KHR;
			break;
		case ASH_PRESENT_MODE_FIFO_KHR:
			vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_FIFO_KHR;
			break;
		}
		return vkPresentMode;
	}
};
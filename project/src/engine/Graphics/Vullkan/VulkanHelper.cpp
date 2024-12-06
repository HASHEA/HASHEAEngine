#pragma once
#include "Graphics/Texture.h"
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

	//
// Helper methods for texture formats
//
	namespace TextureFormat {

		inline bool                     is_depth_stencil(VkFormat value) {
			return value >= VK_FORMAT_D16_UNORM_S8_UINT && value < VK_FORMAT_BC1_RGB_UNORM_BLOCK;
		}
		inline bool                     is_depth_only(VkFormat value) {
			return value >= VK_FORMAT_D16_UNORM && value < VK_FORMAT_S8_UINT;
		}
		inline bool                     is_stencil_only(VkFormat value) {
			return value == VK_FORMAT_S8_UINT;
		}

		inline bool                     has_depth(VkFormat value) {
			return is_depth_only(value) || is_depth_stencil(value);
		}
		inline bool                     has_stencil(VkFormat value) {
			return value >= VK_FORMAT_S8_UINT && value <= VK_FORMAT_D32_SFLOAT_S8_UINT;
		}
		inline bool                     has_depth_or_stencil(VkFormat value) {
			return value >= VK_FORMAT_D16_UNORM && value <= VK_FORMAT_D32_SFLOAT_S8_UINT;
		}

	} // namespace TextureFormat

	auto ash_image_type_to_vk(const AshImageType& type) -> VkImageType
	{
		VkImageType vktype = VkImageType::VK_IMAGE_TYPE_1D;
		switch (type)
		{
		case Ash_Texture1D:
			vktype = VkImageType::VK_IMAGE_TYPE_1D;
			break;
		case Ash_Texture2D:
			vktype = VkImageType::VK_IMAGE_TYPE_2D;
			break;
		case Ash_Texture3D:
			vktype = VkImageType::VK_IMAGE_TYPE_3D;
			break;
		case Ash_TextureCube:
			vktype = VkImageType::VK_IMAGE_TYPE_2D;
			break;
		case Ash_Texture_1D_Array:
			vktype = VkImageType::VK_IMAGE_TYPE_1D;
			break;
		case Ash_Texture_2D_Array:
			vktype = VkImageType::VK_IMAGE_TYPE_2D;
			break;
		case Ash_Texture_Cube_Array:
			vktype = VkImageType::VK_IMAGE_TYPE_2D;
			break;
		}

		return vktype;
	}

	auto ash_format_to_vk(const AshFormat& format) -> VkFormat
	{
		VkFormat vkFormat = VkFormat::VK_FORMAT_UNDEFINED;
		switch (format)
		{
		case ASH_FORMAT_UNDEFINED:
			vkFormat = VkFormat::VK_FORMAT_UNDEFINED;
			break;
		case ASH_FORMAT_R8_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R8_UNORM;
			break;
		case ASH_FORMAT_R8_UINT:
			vkFormat = VkFormat::VK_FORMAT_R8_UINT;
			break;
		case ASH_FORMAT_R8_SRGB:
			vkFormat = VkFormat::VK_FORMAT_R8_SRGB;
			break;
		case ASH_FORMAT_R8G8_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R8G8_UNORM;
			break;
		case ASH_FORMAT_R8G8_UINT:
			vkFormat = VkFormat::VK_FORMAT_R8G8_UINT;
			break;
		case ASH_FORMAT_R8G8_SRGB:
			vkFormat = VkFormat::VK_FORMAT_R8G8_SRGB;
			break;
		case ASH_FORMAT_R8G8B8_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R8G8B8_UNORM;
			break;
		case ASH_FORMAT_R8G8B8_UINT:
			vkFormat = VkFormat::VK_FORMAT_R8G8B8_UINT;
			break;
		case ASH_FORMAT_R8G8B8_SRGB:
			vkFormat = VkFormat::VK_FORMAT_R8G8B8_SRGB;
			break;
		case ASH_FORMAT_B8G8R8_UNORM:
			vkFormat = VkFormat::VK_FORMAT_B8G8R8_UNORM;
			break;
		case ASH_FORMAT_B8G8R8_UINT:
			vkFormat = VkFormat::VK_FORMAT_B8G8R8_UINT;
			break;
		case ASH_FORMAT_B8G8R8_SRGB:
			vkFormat = VkFormat::VK_FORMAT_B8G8R8_SRGB;
			break;
		case ASH_FORMAT_R8G8B8A8_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
			break;
		case ASH_FORMAT_R8G8B8A8_UINT:
			vkFormat = VkFormat::VK_FORMAT_R8G8B8A8_UINT;
			break;
		case ASH_FORMAT_R8G8B8A8_SRGB:
			vkFormat = VkFormat::VK_FORMAT_R8G8B8A8_SRGB;
			break;
		case ASH_FORMAT_B8G8R8A8_UNORM:
			vkFormat = VkFormat::VK_FORMAT_B8G8R8A8_UNORM;
			break;
		case ASH_FORMAT_B8G8R8A8_UINT:
			vkFormat = VkFormat::VK_FORMAT_B8G8R8A8_UINT;
			break;
		case ASH_FORMAT_B8G8R8A8_SRGB:
			vkFormat = VkFormat::VK_FORMAT_B8G8R8A8_SRGB;
			break;
		case ASH_FORMAT_R16_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R16_UNORM;
			break;
		case ASH_FORMAT_R16_UINT:
			vkFormat = VkFormat::VK_FORMAT_R16_UINT;
			break;
		case ASH_FORMAT_R16_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R16_SFLOAT;
			break;
		case ASH_FORMAT_R16G16_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R16G16_UNORM;
			break;
		case ASH_FORMAT_R16G16_UINT:
			vkFormat = VkFormat::VK_FORMAT_R16G16_UINT;
			break;
		case ASH_FORMAT_R16G16_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R16G16_SFLOAT;
			break;
		case ASH_FORMAT_R16G16B16_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R16G16B16_UNORM;
			break;
		case ASH_FORMAT_R16G16B16_UINT:
			vkFormat = VkFormat::VK_FORMAT_R16G16B16_UINT;
			break;
		case ASH_FORMAT_R16G16B16_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R16G16B16_SFLOAT;
			break;
		case ASH_FORMAT_R16G16B16A16_UNORM:
			vkFormat = VkFormat::VK_FORMAT_R16G16B16A16_UNORM;
			break;
		case ASH_FORMAT_R16G16B16A16_UINT:
			vkFormat = VkFormat::VK_FORMAT_R16G16B16A16_UINT;
			break;
		case ASH_FORMAT_R16G16B16A16_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;
			break;
		case ASH_FORMAT_R32_UINT:
			vkFormat = VkFormat::VK_FORMAT_R32_UINT;
			break;
		case ASH_FORMAT_R32_SINT:
			vkFormat = VkFormat::VK_FORMAT_R32_SINT;
			break;
		case ASH_FORMAT_R32_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R32_SFLOAT;
			break;
		case ASH_FORMAT_R32G32_UINT:
			vkFormat = VkFormat::VK_FORMAT_R32G32_UINT;
			break;
		case ASH_FORMAT_R32G32_SINT:
			vkFormat = VkFormat::VK_FORMAT_R32G32_SINT;
			break;
		case ASH_FORMAT_R32G32_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R32G32_SFLOAT;
			break;
		case ASH_FORMAT_R32G32B32_UINT:
			vkFormat = VkFormat::VK_FORMAT_R32G32B32_UINT;
			break;
		case ASH_FORMAT_R32G32B32_SINT:
			vkFormat = VkFormat::VK_FORMAT_R32G32B32_SINT;
			break;
		case ASH_FORMAT_R32G32B32_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
			break;
		case ASH_FORMAT_R32G32B32A32_UINT:
			vkFormat = VkFormat::VK_FORMAT_R32G32B32A32_UINT;
			break;
		case ASH_FORMAT_R32G32B32A32_SINT:
			vkFormat = VkFormat::VK_FORMAT_R32G32B32A32_SINT;
			break;
		case ASH_FORMAT_R32G32B32A32_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
			break;
		case ASH_FORMAT_R64_UINT:
			vkFormat = VkFormat::VK_FORMAT_R64_UINT;
			break;
		case ASH_FORMAT_R64_SINT:
			vkFormat = VkFormat::VK_FORMAT_R64_SINT;
			break;
		case ASH_FORMAT_R64_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R64_SFLOAT;
			break;
		case ASH_FORMAT_R64G64_UINT:
			vkFormat = VkFormat::VK_FORMAT_R64G64_UINT;
			break;
		case ASH_FORMAT_R64G64_SINT:
			vkFormat = VkFormat::VK_FORMAT_R64G64_SINT;
			break;
		case ASH_FORMAT_R64G64_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R64G64_SFLOAT;
			break;
		case ASH_FORMAT_R64G64B64_UINT:
			vkFormat = VkFormat::VK_FORMAT_R64G64B64_UINT;
			break;
		case ASH_FORMAT_R64G64B64_SINT:
			vkFormat = VkFormat::VK_FORMAT_R64G64B64_SINT;
			break;
		case ASH_FORMAT_R64G64B64_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R64G64B64_SFLOAT;
			break;
		case ASH_FORMAT_R64G64B64A64_UINT:
			vkFormat = VkFormat::VK_FORMAT_R64G64B64A64_UINT;
			break;
		case ASH_FORMAT_R64G64B64A64_SINT:
			vkFormat = VkFormat::VK_FORMAT_R64G64B64A64_SINT;
			break;
		case ASH_FORMAT_R64G64B64A64_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_R64G64B64A64_SFLOAT;
			break;
		case ASH_FORMAT_D16_UNORM:
			vkFormat = VkFormat::VK_FORMAT_D16_UNORM;
			break;
		case ASH_FORMAT_D32_SFLOAT:
			vkFormat = VkFormat::VK_FORMAT_D32_SFLOAT;
			break;
		case ASH_FORMAT_S8_UINT:
			vkFormat = VkFormat::VK_FORMAT_S8_UINT;
			break;
		case ASH_FORMAT_D16_UNORM_S8_UINT:
			vkFormat = VkFormat::VK_FORMAT_D16_UNORM_S8_UINT;
			break;
		case ASH_FORMAT_D24_UNORM_S8_UINT:
			vkFormat = VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case ASH_FORMAT_D32_SFLOAT_S8_UINT:
			vkFormat = VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT;
			break;
		default:
			vkFormat = VkFormat::VK_FORMAT_UNDEFINED;
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
		case ASH_PRESENT_MODE_FIFO_RELAXED_KHR:
			vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_FIFO_KHR;
			break;
		case ASH_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:
			vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
			break;
		case ASH_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR:
			vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
			break;
		default:
			vkPresentMode = VkPresentModeKHR::VK_PRESENT_MODE_IMMEDIATE_KHR;
			break;
		}
		return vkPresentMode;
	}

	auto get_image_usage_vulkan(const TextureCreation& creation) -> VkImageUsageFlags
	{
		const bool is_render_target = (creation.flags & AshTextureFlags::RenderTarget_mask) == AshTextureFlags::RenderTarget_mask;
		const bool is_compute_used = (creation.flags & AshTextureFlags::Compute_mask) == AshTextureFlags::Compute_mask;
		const bool is_shading_rate_texture = (creation.flags & AshTextureFlags::ShadingRate_mask) == AshTextureFlags::ShadingRate_mask;
		// Default to always readable from shader.
		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		usage |= is_compute_used ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
		usage |= is_shading_rate_texture ? VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR : 0;
		if (TextureFormat::has_depth_or_stencil(ash_format_to_vk(creation.format))) {
			// Depth/Stencil textures are normally textures you render into.
			usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // TODO

		}
		else {
			usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // TODO
			usage |= is_render_target ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0;
		}
		return usage;
	}
};
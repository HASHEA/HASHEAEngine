#pragma once
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "VulkanWrapper.h"
#include "Graphics/RHICommon.h"
#include "Graphics/RHIResource.h"
#include "Graphics/Texture.h"
namespace RHI
{

	inline auto vulkan_error_string(VkResult errorCode) -> const char*
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
	enum DeviceExtensionAndFeaturesFlags
	{
		DynamicRendering,
		TimelineSemaphore,
		Synchronization2,
		MeshShaders,
		Multiview,
		FragmentShadingRate,
		RayTracing,
		RayQuery,
		Bindless,
	};
#define VK_CHECK_RESULT(f)                                                                                                        \
	{                                                                                                                             \
		VkResult res = (f);                                                                                                       \
		if (res != VK_SUCCESS)                                                                                                    \
		{                                                                                                                         \
			HLogError("Fatal : VK CALL ERROR : VkResult is \" {0} \" in {1} at line {2}", vulkan_error_string(res), __FILE__, __LINE__); \
			H_ASSERT(res == VK_SUCCESS);                                                                                            \
		}                                                                                                                         \
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
	inline auto ash_image_type_to_vk(const AshImageType& type) -> VkImageType
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
		default:
			vktype = VkImageType::VK_IMAGE_TYPE_2D;
			break;
		}

		return vktype;
	}
	inline auto ash_format_to_vk(const AshFormat& format) -> VkFormat
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

	inline auto vk_format_to_ash(const VkFormat& format) -> AshFormat
	{
		AshFormat ashFormat = AshFormat::ASH_FORMAT_UNDEFINED;
		switch (format)
		{
		case VK_FORMAT_UNDEFINED:
			ashFormat = AshFormat::ASH_FORMAT_UNDEFINED;
			break;
		case VK_FORMAT_R8_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R8_UNORM;
			break;
		case VK_FORMAT_R8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R8_UINT;
			break;
		case VK_FORMAT_R8_SRGB:
			ashFormat = AshFormat::ASH_FORMAT_R8_SRGB;
			break;
		case VK_FORMAT_R8G8_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R8G8_UNORM;
			break;
		case VK_FORMAT_R8G8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R8G8_UINT;
			break;
		case VK_FORMAT_R8G8_SRGB:
			ashFormat = AshFormat::ASH_FORMAT_R8G8_SRGB;
			break;
		case VK_FORMAT_R8G8B8_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R8G8B8_UNORM;
			break;
		case VK_FORMAT_R8G8B8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R8G8B8_UINT;
			break;
		case VK_FORMAT_R8G8B8_SRGB:
			ashFormat = AshFormat::ASH_FORMAT_R8G8B8_SRGB;
			break;
		case VK_FORMAT_B8G8R8_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_B8G8R8_UNORM;
			break;
		case VK_FORMAT_B8G8R8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_B8G8R8_UINT;
			break;
		case VK_FORMAT_B8G8R8_SRGB:
			ashFormat = AshFormat::ASH_FORMAT_B8G8R8_SRGB;
			break;
		case VK_FORMAT_R8G8B8A8_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R8G8B8A8_UNORM;
			break;
		case VK_FORMAT_R8G8B8A8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R8G8B8A8_UINT;
			break;
		case VK_FORMAT_R8G8B8A8_SRGB:
			ashFormat = AshFormat::ASH_FORMAT_R8G8B8A8_SRGB;
			break;
		case VK_FORMAT_B8G8R8A8_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_B8G8R8A8_UNORM;
			break;
		case VK_FORMAT_B8G8R8A8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_B8G8R8A8_UINT;
			break;
		case VK_FORMAT_B8G8R8A8_SRGB:
			ashFormat = AshFormat::ASH_FORMAT_B8G8R8A8_SRGB;
			break;
		case VK_FORMAT_R16_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R16_UNORM;
			break;
		case VK_FORMAT_R16_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R16_UINT;
			break;
		case VK_FORMAT_R16_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R16_SFLOAT;
			break;
		case VK_FORMAT_R16G16_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R16G16_UNORM;
			break;
		case VK_FORMAT_R16G16_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R16G16_UINT;
			break;
		case VK_FORMAT_R16G16_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R16G16_SFLOAT;
			break;
		case VK_FORMAT_R16G16B16_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R16G16B16_UNORM;
			break;
		case VK_FORMAT_R16G16B16_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R16G16B16_UINT;
			break;
		case VK_FORMAT_R16G16B16_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R16G16B16_SFLOAT;
			break;
		case VK_FORMAT_R16G16B16A16_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_R16G16B16A16_UNORM;
			break;
		case VK_FORMAT_R16G16B16A16_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R16G16B16A16_UINT;
			break;
		case VK_FORMAT_R16G16B16A16_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R16G16B16A16_SFLOAT;
			break;
		case VK_FORMAT_R32_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R32_UINT;
			break;
		case VK_FORMAT_R32_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R32_SINT;
			break;
		case VK_FORMAT_R32_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R32_SFLOAT;
			break;
		case VK_FORMAT_R32G32_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32_UINT;
			break;
		case VK_FORMAT_R32G32_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32_SINT;
			break;
		case VK_FORMAT_R32G32_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32_SFLOAT;
			break;
		case VK_FORMAT_R32G32B32_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32B32_UINT;
			break;
		case VK_FORMAT_R32G32B32_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32B32_SINT;
			break;
		case VK_FORMAT_R32G32B32_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32B32_SFLOAT;
			break;
		case VK_FORMAT_R32G32B32A32_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32B32A32_UINT;
			break;
		case VK_FORMAT_R32G32B32A32_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32B32A32_SINT;
			break;
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R32G32B32A32_SFLOAT;
			break;
		case VK_FORMAT_R64_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R64_UINT;
			break;
		case VK_FORMAT_R64_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R64_SINT;
			break;
		case VK_FORMAT_R64_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R64_SFLOAT;
			break;
		case VK_FORMAT_R64G64_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64_UINT;
			break;
		case VK_FORMAT_R64G64_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64_SINT;
			break;
		case VK_FORMAT_R64G64_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64_SFLOAT;
			break;
		case VK_FORMAT_R64G64B64_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64B64_UINT;
			break;
		case VK_FORMAT_R64G64B64_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64B64_SINT;
			break;
		case VK_FORMAT_R64G64B64_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64B64_SFLOAT;
			break;
		case VK_FORMAT_R64G64B64A64_UINT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64B64A64_UINT;
			break;
		case VK_FORMAT_R64G64B64A64_SINT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64B64A64_SINT;
			break;
		case VK_FORMAT_R64G64B64A64_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_R64G64B64A64_SFLOAT;
			break;
		case VK_FORMAT_D16_UNORM:
			ashFormat = AshFormat::ASH_FORMAT_D16_UNORM;
			break;
		case VK_FORMAT_D32_SFLOAT:
			ashFormat = AshFormat::ASH_FORMAT_D32_SFLOAT;
			break;
		case VK_FORMAT_S8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_S8_UINT;
			break;
		case VK_FORMAT_D16_UNORM_S8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_D16_UNORM_S8_UINT;
			break;
		case VK_FORMAT_D24_UNORM_S8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_D24_UNORM_S8_UINT;
			break;
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			ashFormat = AshFormat::ASH_FORMAT_D32_SFLOAT_S8_UINT;
			break;
		default:
			ashFormat = AshFormat::ASH_FORMAT_UNDEFINED;
			break;
		}
		return ashFormat;
	}

	inline auto ash_color_space_to_vk(const AshColorSpace& colorSpace) -> VkColorSpaceKHR
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
	inline auto ash_present_mode_to_vk(const AshPresentMode& presentMode) -> VkPresentModeKHR
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
	inline auto get_image_usage_vulkan(const TextureCreation& creation) -> VkImageUsageFlags
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
	inline auto ash_image_view_type_to_vk(const AshImageViewType& type) -> VkImageViewType
	{
		VkImageViewType vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D;
		switch (type)
		{
		case ASH_IMAGE_VIEW_TYPE_1D:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D;
			break;
		case ASH_IMAGE_VIEW_TYPE_2D:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
			break;
		case ASH_IMAGE_VIEW_TYPE_3D:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_3D;
			break;
		case ASH_IMAGE_VIEW_TYPE_CUBE:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE;
			break;
		case ASH_IMAGE_VIEW_TYPE_1D_ARRAY:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			break;
		case ASH_IMAGE_VIEW_TYPE_2D_ARRAY:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		case ASH_IMAGE_VIEW_TYPE_CUBE_ARRAY:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			break;
		default:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
			break;
		}
		return vktype;
	}
	inline auto ash_image_type_to_image_view_type(const AshImageType& type) -> AshImageViewType
	{
		AshImageViewType oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_1D;
		switch (type)
		{
		case Ash_Texture1D:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_1D;
			break;
		case Ash_Texture2D:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_2D;
			break;
		case Ash_Texture3D:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_3D;
			break;
		case Ash_TextureCube:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_CUBE;
			break;
		case Ash_Texture_1D_Array:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_1D_ARRAY;
			break;
		case Ash_Texture_2D_Array:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		case Ash_Texture_Cube_Array:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			break;
		default:
			oType = AshImageViewType::ASH_IMAGE_VIEW_TYPE_2D;
			break;
		}
		return oType;
	}
	inline auto ash_sampler_address_mode_to_vk(const AshSamplerAddressMode& mode) -> VkSamplerAddressMode
	{
		VkSamplerAddressMode vktype = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
		switch (mode)
		{
		case ASH_SAMPLER_ADDRESS_MODE_REPEAT:
			vktype = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
		case ASH_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
			vktype = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			break;
		case ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
			vktype = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		case ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
			vktype = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			break;
		case ASH_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
			vktype = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
			break;
		default:
			vktype = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
		}
		return vktype;
	}
	inline auto ash_border_color_to_vk(const AshBorderColor& col) -> VkBorderColor
	{
		VkBorderColor ret = VkBorderColor::VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		switch (col)
		{
		case ASH_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
			ret = VkBorderColor::VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			break;
		case ASH_BORDER_COLOR_INT_TRANSPARENT_BLACK:
			ret = VkBorderColor::VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
			break;
		case ASH_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
			ret = VkBorderColor::VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			break;
		case ASH_BORDER_COLOR_INT_OPAQUE_BLACK:
			ret = VkBorderColor::VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			break;
		case ASH_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
			ret = VkBorderColor::VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			break;
		case ASH_BORDER_COLOR_INT_OPAQUE_WHITE:
			ret = VkBorderColor::VK_BORDER_COLOR_INT_OPAQUE_WHITE;
			break;
		case ASH_BORDER_COLOR_FLOAT_CUSTOM_EXT:
			ret = VkBorderColor::VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
			break;
		case ASH_BORDER_COLOR_INT_CUSTOM_EXT:
			ret = VkBorderColor::VK_BORDER_COLOR_INT_CUSTOM_EXT;
			break;
		default:
			ret = VkBorderColor::VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			break;
		}
		return ret;
	}
	inline auto ash_filter_to_min_mag_vk(const AshFilter& filter) -> VkFilter
	{
		VkFilter vktype = VkFilter::VK_FILTER_NEAREST;
		switch (filter)
		{
		case ASH_FILTER_NEAREST:
			vktype = VkFilter::VK_FILTER_NEAREST;
			break;
		case ASH_FILTER_LINEAR:
			vktype = VkFilter::VK_FILTER_LINEAR;
			break;
		case ASH_FILTER_CUBIC_EXT:
			vktype = VkFilter::VK_FILTER_CUBIC_EXT;
			break;
		default:
			vktype = VkFilter::VK_FILTER_NEAREST;
			break;
		}
		return vktype;
	}
	inline auto ash_filter_to_mip_vk(const AshFilter& filter) -> VkSamplerMipmapMode
	{
		VkSamplerMipmapMode vktype = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
		switch (filter)
		{
		case ASH_FILTER_NEAREST:
			vktype = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		case ASH_FILTER_LINEAR:
			vktype = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR;
			break;
		case ASH_FILTER_CUBIC_EXT:
			HLogWarning("set invalid filter type: {0} to samplermipmap mode, use default type: {1} instead !", TYPE_TO_STRING(ASH_FILTER_CUBIC_EXT), TYPE_TO_STRING(ASH_FILTER_NEAREST));
		default:
			vktype = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		}
		return vktype;
	}
	inline auto ash_compare_option_to_vk(const AshCompareOp& op) -> VkCompareOp
	{
		VkCompareOp ret = VkCompareOp::VK_COMPARE_OP_NEVER;
		switch (op)
		{
		case ASH_COMPARE_OP_NEVER:
			ret = VkCompareOp::VK_COMPARE_OP_NEVER;
			break;
		case ASH_COMPARE_OP_LESS:
			ret = VkCompareOp::VK_COMPARE_OP_LESS;
			break;
		case ASH_COMPARE_OP_EQUAL:
			ret = VkCompareOp::VK_COMPARE_OP_EQUAL;
			break;
		case ASH_COMPARE_OP_LESS_OR_EQUAL:
			ret = VkCompareOp::VK_COMPARE_OP_LESS_OR_EQUAL;
			break;
		case ASH_COMPARE_OP_GREATER:
			ret = VkCompareOp::VK_COMPARE_OP_GREATER;
			break;
		case ASH_COMPARE_OP_NOT_EQUAL:
			ret = VkCompareOp::VK_COMPARE_OP_NOT_EQUAL;
			break;
		case ASH_COMPARE_OP_GREATER_OR_EQUAL:
			ret = VkCompareOp::VK_COMPARE_OP_GREATER_OR_EQUAL;
			break;
		case ASH_COMPARE_OP_ALWAYS:
			ret = VkCompareOp::VK_COMPARE_OP_ALWAYS;
			break;
		default:
			ret = VkCompareOp::VK_COMPARE_OP_NEVER;
			break;
		}
		return ret;
	}
	inline auto ash_sampler_reduction_mode_to_vk(const AshSamplerReductionMode& mode) -> VkSamplerReductionMode
	{
		VkSamplerReductionMode vktype = VkSamplerReductionMode::VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
		switch (mode)
		{
		case ASH_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE:
			vktype = VkSamplerReductionMode::VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
			break;
		case ASH_SAMPLER_REDUCTION_MODE_MIN:
			vktype = VkSamplerReductionMode::VK_SAMPLER_REDUCTION_MODE_MIN;
			break;
		case ASH_SAMPLER_REDUCTION_MODE_MAX:
			vktype = VkSamplerReductionMode::VK_SAMPLER_REDUCTION_MODE_MAX;
			break;
		default:
			vktype = VkSamplerReductionMode::VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
			break;
		}
		return vktype;
	}

	inline auto ash_resource_state_to_vk_image_layout(const AshResourceState& state) -> VkImageLayout
	{
		VkImageLayout layout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		switch (state)
		{
		case AshResourceState::ASH_RESOURCE_STATE_UNDEFINED:
			layout = VK_IMAGE_LAYOUT_UNDEFINED;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_GENERAL:
			layout = VK_IMAGE_LAYOUT_GENERAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_RENDER_TARGET:
			layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_DEPTH_STENCIL_WRITE:
			layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_DEPTH_STENCIL_READ:
			layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_SHADER_RESOURCE:
			layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_COPY_SOURCE:
			layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_COPY_DEST:
			layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_PREINITIALIZED:
			layout = VK_IMAGE_LAYOUT_PREINITIALIZED;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_UNORDERED_ACCESS:
			layout = VK_IMAGE_LAYOUT_GENERAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_DEPTH_WRITE:
			layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_DEPTH_READ:
			layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_STENCIL_WRITE:
			layout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_STENCIL_READ:
			layout = VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_PRESENT:
			layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			break;
		case AshResourceState::ASH_RESOURCE_STATE_FRAGMENT_SHADING_RATE_ATTACHMENT:
			layout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
			break;
		default:
			HLogWarning("unsupported state, trans to undefined !");
			break;
		}
		return layout;
	}

	inline auto ash_is_valid_transition(const AshResourceState& src, const AshResourceState& dst) -> bool
	{
		if (src == dst)
		{
			return false;
		}
		if (dst == AshResourceState::ASH_RESOURCE_STATE_UNDEFINED || dst == AshResourceState::ASH_RESOURCE_STATE_PREINITIALIZED)
		{
			return false;
		}
		//TODO: add other rules to avoid useless transition
		return true;
	}

	inline auto vk_layout_to_access_mask(const VkImageLayout& layout) -> VkPipelineStageFlags
	{
		VkPipelineStageFlags accessMask = 0;
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			break;

		case VK_IMAGE_LAYOUT_GENERAL:
			accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			accessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			accessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			accessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
			accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
			accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
			accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
			accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			accessMask = VK_ACCESS_MEMORY_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
			accessMask = VK_ACCESS_SHADER_WRITE_BIT;
			break;
		default:
			HLogWarning("Unexpected image layout");
			break;
		}

		return accessMask;
	}
	inline auto util_determine_pipeline_stage_flags(VkAccessFlags access_flags, AshQueueType::Enum queue_type) -> VkPipelineStageFlags {
		VkPipelineStageFlags flags = 0;

		switch (queue_type) {
		case AshQueueType::Graphics:
		{
			if ((access_flags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

			if ((access_flags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0) {
				flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
				flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

				// TODO(marco): check RT extension is present/enabled
				flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
			}

			if ((access_flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0)
				flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

			if ((access_flags & (VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR)) != 0)
				flags |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;

			if ((access_flags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

			if ((access_flags & VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR) != 0)
				flags = VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

			if ((access_flags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

			break;
		}
		case AshQueueType::Compute:
		{
			if ((access_flags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0 ||
				(access_flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0 ||
				(access_flags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0 ||
				(access_flags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
				return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

			if ((access_flags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0)
				flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			break;
		}
		case AshQueueType::CopyTransfer:
		case AshQueueType::Ignored: 
			return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		default: break;
		}

		// Compatible with both compute and graphics queues
		if ((access_flags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT) != 0)
			flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

		if ((access_flags & (VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)) != 0)
			flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;

		if ((access_flags & (VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT)) != 0)
			flags |= VK_PIPELINE_STAGE_HOST_BIT;

		if (flags == 0)
			flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		return flags;
	}

	inline auto  ash_load_operation_to_vk(const AshLoadOption& loadOP) -> VkAttachmentLoadOp
	{
		VkAttachmentLoadOp retOP = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		switch (loadOP)
		{
		case ASH_LOAD_DONT_CARE :
			retOP = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			break;
		case ASH_LOAD_LOAD:
			retOP = VK_ATTACHMENT_LOAD_OP_LOAD;
			break;
		case ASH_LOAD_CLEAR:
			retOP = VK_ATTACHMENT_LOAD_OP_CLEAR;
			break;
		default:
			break;
		}
		return retOP;
	}

	inline auto ash_color_value_to_vk(const AshColorValue& color) -> VkClearColorValue
	{
		VkClearColorValue clearColor = {};
		for (int i = 0; i < 4; ++i) {
			switch (color.v_type)
			{
			case RHI::AshColorValue::T_float32:
				clearColor.float32[i] = color.float32[i];
				break;
			case RHI::AshColorValue::T_int32:
				clearColor.int32[i] = color.int32[i];
				break;
			case RHI::AshColorValue::T_uint32:
				clearColor.uint32[i] = color.uint32[i];
				break;
			default:
				break;
			}
		}
		return clearColor;
	}
	inline auto ash_depth_stencil_value_to_vk(const AshDepthStencilValue& color) -> VkClearDepthStencilValue
	{
		VkClearDepthStencilValue clearColor = { color .depth,color .stencil};
		return clearColor;
	}
};
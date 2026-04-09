#pragma once
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "VulkanWrapper.h"
#include "Graphics/RHICommon.h"
#include "Graphics/RHIResource.h"
#include "Graphics/Texture.h"
namespace RHI
{
	struct AshTextureFormatInfo
	{
		AshFormat		  format;
		uint32_t          uBytesPerBlock : 15;
		uint32_t          uWidthPerBlock : 8;
		uint32_t          uHeightPerBlock : 8;
		uint32_t          uHasAlpha : 1;
		VkFormat          vkFormat;
		VkFormat          vkFormatSRGB;
	};
	extern const AshTextureFormatInfo g_ashTextureFormatInfo[];
	const AshTextureFormatInfo& get_vk_texture_format_info(AshFormat eFormat);
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
		HostCoherentCached,
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

	inline auto vk_format_to_ash(const VkFormat& format) -> AshFormat
	{
		for (uint32_t i = 0; i < ASH_FORMAT_COUNT; ++i)
		{
			const AshTextureFormatInfo& formatInfo = g_ashTextureFormatInfo[i];
			if (formatInfo.vkFormat == format)
			{
				return formatInfo.format;
			}
		}
		HLogWarning("Unsupported VkFormat {} in vk_format_to_ash, fallback to ASH_FORMAT_R8G8B8A8_UNORM.", static_cast<int32_t>(format));
		return ASH_FORMAT_R8G8B8A8_UNORM;
	}
	inline VkFormat get_texture_format_from_target_format(AshFormat srcfmt, bool& bColorAttach, bool& bDepth, bool& bStencil, uint32_t& bytesStride)
	{
		VkFormat fmt = VK_FORMAT_UNDEFINED;
		bColorAttach = false;
		bDepth = false;
		bStencil = false;
		bytesStride = 4;
		switch (srcfmt)
		{
		case ASH_FORMAT_R8G8B8A8_UNORM:
		{
			fmt = VK_FORMAT_R8G8B8A8_UNORM;
			bytesStride = 4;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_R8G8B8A8_SNORM:
		{
			fmt = VK_FORMAT_R8G8B8A8_SNORM;
			bytesStride = 4;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_R8G8B8A8_SRGB:
		{
			fmt = VK_FORMAT_R8G8B8A8_SRGB;
			bytesStride = 4;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_B8G8R8_UNORM:
		{
			fmt = VK_FORMAT_B8G8R8_UNORM;
			bytesStride = 3;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_B5G6R5_UNORM_PACK16:
		{
			fmt = VK_FORMAT_B5G6R5_UNORM_PACK16;
			bytesStride = 2;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_B8G8R8A8_UNORM:
		{
			fmt = VK_FORMAT_B8G8R8A8_UNORM;
			bytesStride = 4;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_B8G8R8A8_SRGB:
		{
			fmt = VK_FORMAT_B8G8R8A8_SRGB;
			bytesStride = 4;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_R8_UNORM:
		{
			fmt = VK_FORMAT_R8_UNORM;
			bytesStride = 1;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_R8G8_UNORM:
		{
			fmt = VK_FORMAT_R8G8_UNORM;
			bytesStride = 2;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_R16G16_UINT:
		{
			fmt = VK_FORMAT_R16G16_UINT;
			bytesStride = 4;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_R16G16B16A16_UNORM:
		{
			fmt = VK_FORMAT_R16G16B16A16_UNORM;
			bytesStride = 8;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
		}
		break;
		case ASH_FORMAT_R16G16B16A16_SFLOAT:
		{
			bytesStride = 8;
			fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_R32G32B32A32_SFLOAT:
		{
			bytesStride = 16;
			fmt = VK_FORMAT_R32G32B32A32_SFLOAT;
			bColorAttach = TRUE;
		}
		break;
		case ASH_FORMAT_BC7_SRGB_UNORM:
		{
			bytesStride = 16;
			fmt = VK_FORMAT_BC7_SRGB_BLOCK;
			bColorAttach = TRUE;
		}
		break;
		case ASH_FORMAT_R32G32_UINT:
			bytesStride = 8;
			fmt = VK_FORMAT_R32G32_UINT;
			bColorAttach = TRUE;
			break;
		case ASH_FORMAT_R32G32B32A32_UINT:
			bytesStride = 16;
			fmt = VK_FORMAT_R32G32B32A32_UINT;
			bColorAttach = TRUE;
			break;
		case ASH_FORMAT_A2R10G10B10_UNORM_PACK32:
		{
			bytesStride = 8;
			fmt = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_B10G11R11_UFLOAT_PACK32:
		{
			bytesStride = 8;
			fmt = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_R16G16_SFLOAT:
		{
			bytesStride = 8;
			fmt = VK_FORMAT_R16G16_SFLOAT;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_R16_SFLOAT:
		{
			bytesStride = 2;
			fmt = VK_FORMAT_R16_SFLOAT;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_R16_UINT:
		{
			bytesStride = 2;
			fmt = VK_FORMAT_R16_UINT;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_R32_SINT:
		{
			bytesStride = 4;
			fmt = VK_FORMAT_R32_SINT;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_R32_UINT:
		{
			bytesStride = 4;
			fmt = VK_FORMAT_R32_UINT;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_R32_FLOAT:
		{
			bytesStride = 4;
			fmt = VK_FORMAT_R32_SFLOAT;
			bColorAttach = true;
		}
		break;
		case ASH_FORMAT_D24_UNORM_S8_UINT:
		{
			bytesStride = 4;
			bDepth = true;
			bStencil = true;
			fmt = VK_FORMAT_D24_UNORM_S8_UINT;
		}
		break;
		case ASH_FORMAT_D16_UNORM:
		{
			bytesStride = 2;
			bDepth = true;
			fmt = VK_FORMAT_D16_UNORM;
		}
		break;
		case ASH_FORMAT_D32_SFLOAT:
		{
			bytesStride = 4;
			bDepth = true;
			fmt = VK_FORMAT_D32_SFLOAT;
		}
		break;
		case ASH_FORMAT_D32_SFLOAT_S8_UINT:
		{
			bytesStride = 4;
			bDepth = true;
			bStencil = true;
			fmt = VK_FORMAT_D32_SFLOAT_S8_UINT;
		}
		break;
		case ASH_FORMAT_R16G16_UNORM:
			bytesStride = 4;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
			fmt = VK_FORMAT_R16G16_UNORM;
			break;
		case ASH_FORMAT_R16_UNORM:
			bytesStride = 2;
			bColorAttach = true;
			bDepth = false;
			bStencil = false;
			fmt = VK_FORMAT_R16_UNORM;
			break;
		case ASH_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
			bytesStride = 8;
			bDepth = false;
			bStencil = false;
			fmt = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
			break;
		case ASH_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
			bytesStride = 16;
			bDepth = false;
			bStencil = false;
			fmt = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
			break;
		case ASH_FORMAT_ETC2_RG_UNORM_BLOCK:
			bytesStride = 16;
			fmt = VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
			break;
		case ASH_FORMAT_BC1_RGB_UNORM:
			bytesStride = 8;
			fmt = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
			break;
		case ASH_FORMAT_BC3_UNORM:
			bytesStride = 16;
			fmt = VK_FORMAT_BC3_UNORM_BLOCK;
			break;
		case ASH_FORMAT_BC5_UNORM:
			bytesStride = 16;
			fmt = VK_FORMAT_BC5_UNORM_BLOCK;
			break;
		case ASH_FORMAT_ASTC_4X4_UNORM_BLOCK:
			bytesStride = 16;
			fmt = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
			break;
		case ASH_FORMAT_R8_UINT:
			bytesStride = 1;
			fmt = VK_FORMAT_R8_UINT;
			bDepth = false;
			bStencil = false;
			break;
		default:
			H_ASSERT(false);
			break;
		}
		return fmt;
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
	// Converts TextureUsageFlags to VkImageUsageFlags
	inline auto ash_texture_usage_to_vk(AshTextureUsageFlags usageFlags)
	{
		VkImageUsageFlags vkUsage = 0;
		if (usageFlags & ASH_TEXTURE_USAGE_TRANSFER_SRC_BIT)
			vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (usageFlags & ASH_TEXTURE_USAGE_TRANSFER_DST_BIT)
			vkUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (usageFlags & ASH_TEXTURE_USAGE_SAMPLED_BIT)
			vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (usageFlags & ASH_TEXTURE_USAGE_STORAGE_BIT)
			vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
		if (usageFlags & ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)
			vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (usageFlags & ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (usageFlags & ASH_TEXTURE_USAGE_TRANSIENT_ATTACHMENT_BIT)
			vkUsage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		if (usageFlags & ASH_TEXTURE_USAGE_INPUT_ATTACHMENT_BIT)
			vkUsage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		// Add more mappings if needed
		return vkUsage;
	}
	inline auto ash_image_view_dim_to_vk(const AshResourceViewDimension& type) -> VkImageViewType
	{
		VkImageViewType vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D;
		switch (type)
		{
		case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D;
			break;
		case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
			break;
		case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_3D;
			break;
		case ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE;
			break;
		case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			break;
		case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		case ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE_ARRAY:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			break;
		default:
			vktype = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
			break;
		}
		return vktype;
	}
	inline auto ash_image_type_to_image_view_type(const AshImageType& type) -> AshResourceViewDimension
	{
		AshResourceViewDimension oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D;
		switch (type)
		{
		case Ash_Texture1D:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D;
			break;
		case Ash_Texture2D:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D;
			break;
		case Ash_Texture3D:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D;
			break;
		case Ash_TextureCube:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE;
			break;
		case Ash_Texture_1D_Array:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY;
			break;
		case Ash_Texture_2D_Array:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY;
			break;
		case Ash_Texture_Cube_Array:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE_ARRAY;
			break;
		default:
			oType = AshResourceViewDimension::ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D;
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
	inline auto ash_stencil_op_to_vk(const AshStencilOp& op) -> VkStencilOp
	{
		switch (op)
		{
		case ASH_STENCIL_OP_KEEP: return VK_STENCIL_OP_KEEP;
		case ASH_STENCIL_OP_ZERO: return VK_STENCIL_OP_ZERO;
		case ASH_STENCIL_OP_REPLACE: return VK_STENCIL_OP_REPLACE;
		case ASH_STENCIL_OP_INCREMENT_AND_CLAMP: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case ASH_STENCIL_OP_DECREMENT_AND_CLAMP: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case ASH_STENCIL_OP_INVERT: return VK_STENCIL_OP_INVERT;
		case ASH_STENCIL_OP_INCREMENT_AND_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case ASH_STENCIL_OP_DECREMENT_AND_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		default: return VK_STENCIL_OP_KEEP;
		}
	}
	inline auto ash_shader_stage_to_vk(AshShaderStageFlagBits stage) -> VkShaderStageFlagBits
	{
		switch (stage)
		{
		case ASH_SHADER_STAGE_VERTEX_BIT: return VK_SHADER_STAGE_VERTEX_BIT;
		case ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		case ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		case ASH_SHADER_STAGE_GEOMETRY_BIT: return VK_SHADER_STAGE_GEOMETRY_BIT;
		case ASH_SHADER_STAGE_FRAGMENT_BIT: return VK_SHADER_STAGE_FRAGMENT_BIT;
		case ASH_SHADER_STAGE_COMPUTE_BIT: return VK_SHADER_STAGE_COMPUTE_BIT;
		case ASH_SHADER_STAGE_RAYGEN_BIT_KHR: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		case ASH_SHADER_STAGE_ANY_HIT_BIT_KHR: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		case ASH_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		case ASH_SHADER_STAGE_MISS_BIT_KHR: return VK_SHADER_STAGE_MISS_BIT_KHR;
		case ASH_SHADER_STAGE_INTERSECTION_BIT_KHR: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
		case ASH_SHADER_STAGE_CALLABLE_BIT_KHR: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
		case ASH_SHADER_STAGE_TASK_BIT_EXT: return VK_SHADER_STAGE_TASK_BIT_EXT;
		case ASH_SHADER_STAGE_MESH_BIT_EXT: return VK_SHADER_STAGE_MESH_BIT_EXT;
		default: return VK_SHADER_STAGE_ALL;
		}
	}
	inline auto ash_shader_stage_flags_to_vk(AshShaderStageFlagBits stage_flags) -> VkShaderStageFlags
	{
		VkShaderStageFlags flags = 0;
		if (stage_flags & ASH_SHADER_STAGE_VERTEX_BIT) flags |= VK_SHADER_STAGE_VERTEX_BIT;
		if (stage_flags & ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT) flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		if (stage_flags & ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		if (stage_flags & ASH_SHADER_STAGE_GEOMETRY_BIT) flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
		if (stage_flags & ASH_SHADER_STAGE_FRAGMENT_BIT) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (stage_flags & ASH_SHADER_STAGE_COMPUTE_BIT) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
		if (stage_flags & ASH_SHADER_STAGE_RAYGEN_BIT_KHR) flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_ANY_HIT_BIT_KHR) flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_MISS_BIT_KHR) flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_INTERSECTION_BIT_KHR) flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_CALLABLE_BIT_KHR) flags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
		if (stage_flags & ASH_SHADER_STAGE_TASK_BIT_EXT) flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
		if (stage_flags & ASH_SHADER_STAGE_MESH_BIT_EXT) flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
		return flags;
	}
	inline auto ash_primitive_topology_to_vk(const AshPrimitiveTopology topology) -> VkPrimitiveTopology
	{
		switch (topology)
		{
		case ASH_PRIMITIVE_TOPOLOGY_POINT_LIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case ASH_PRIMITIVE_TOPOLOGY_LINE_LIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case ASH_PRIMITIVE_TOPOLOGY_LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		case ASH_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
		case ASH_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
		case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
		case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
		case ASH_PRIMITIVE_TOPOLOGY_PATCH_LIST: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
		default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	}
	inline auto ash_cull_mode_to_vk(const AshCullModeFlagBits mode) -> VkCullModeFlags
	{
		switch (mode)
		{
		case ASH_CULL_MODE_NONE: return VK_CULL_MODE_NONE;
		case ASH_CULL_MODE_FRONT_BIT: return VK_CULL_MODE_FRONT_BIT;
		case ASH_CULL_MODE_BACK_BIT: return VK_CULL_MODE_BACK_BIT;
		case ASH_CULL_MODE_FRONT_AND_BACK: return VK_CULL_MODE_FRONT_AND_BACK;
		default: return VK_CULL_MODE_NONE;
		}
	}
	inline auto ash_front_face_to_vk(const AshFrontFace front_face) -> VkFrontFace
	{
		switch (front_face)
		{
		case ASH_FRONT_FACE_COUNTER_CLOCKWISE: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		case ASH_FRONT_FACE_CLOCKWISE: return VK_FRONT_FACE_CLOCKWISE;
		default: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		}
	}
	inline auto ash_fill_mode_to_vk(const AshFillMode fill_mode) -> VkPolygonMode
	{
		switch (fill_mode)
		{
		case Wireframe: return VK_POLYGON_MODE_LINE;
		case Point: return VK_POLYGON_MODE_POINT;
		case Solid:
		default:
			return VK_POLYGON_MODE_FILL;
		}
	}
	inline auto ash_blend_factor_to_vk(const AshBlendFactor factor) -> VkBlendFactor
	{
		switch (factor)
		{
		case ASH_BLEND_FACTOR_ZERO: return VK_BLEND_FACTOR_ZERO;
		case ASH_BLEND_FACTOR_ONE: return VK_BLEND_FACTOR_ONE;
		case ASH_BLEND_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
		case ASH_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case ASH_BLEND_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
		case ASH_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case ASH_BLEND_FACTOR_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
		case ASH_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case ASH_BLEND_FACTOR_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
		case ASH_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case ASH_BLEND_FACTOR_CONSTANT_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
		case ASH_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
		case ASH_BLEND_FACTOR_CONSTANT_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
		case ASH_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
		case ASH_BLEND_FACTOR_SRC_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		case ASH_BLEND_FACTOR_SRC1_COLOR: return VK_BLEND_FACTOR_SRC1_COLOR;
		case ASH_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
		case ASH_BLEND_FACTOR_SRC1_ALPHA: return VK_BLEND_FACTOR_SRC1_ALPHA;
		case ASH_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
		default: return VK_BLEND_FACTOR_ONE;
		}
	}
	inline auto ash_blend_op_to_vk(const AshBlendOp op) -> VkBlendOp
	{
		switch (op)
		{
		case ASH_BLEND_OP_ADD: return VK_BLEND_OP_ADD;
		case ASH_BLEND_OP_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
		case ASH_BLEND_OP_REVERSE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
		case ASH_BLEND_OP_MIN: return VK_BLEND_OP_MIN;
		case ASH_BLEND_OP_MAX: return VK_BLEND_OP_MAX;
		default: return VK_BLEND_OP_ADD;
		}
	}
	inline auto ash_color_write_mask_to_vk(const AshColorWriteMask mask) -> VkColorComponentFlags
	{
		VkColorComponentFlags result = 0;
		if (mask & Red) result |= VK_COLOR_COMPONENT_R_BIT;
		if (mask & Green) result |= VK_COLOR_COMPONENT_G_BIT;
		if (mask & Blue) result |= VK_COLOR_COMPONENT_B_BIT;
		if (mask & Alpha) result |= VK_COLOR_COMPONENT_A_BIT;
		return result;
	}
	inline auto ash_vertex_input_rate_to_vk(const AshVertexInputRate rate) -> VkVertexInputRate
	{
		switch (rate)
		{
		case PerInstance: return VK_VERTEX_INPUT_RATE_INSTANCE;
		case PerVertex:
		default:
			return VK_VERTEX_INPUT_RATE_VERTEX;
		}
	}
	inline auto ash_vertex_component_format_to_vk(const AshVertexComponentFormat format) -> VkFormat
	{
		switch (format)
		{
		case Float: return VK_FORMAT_R32_SFLOAT;
		case Float2: return VK_FORMAT_R32G32_SFLOAT;
		case Float3: return VK_FORMAT_R32G32B32_SFLOAT;
		case Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
		case Byte: return VK_FORMAT_R8_SINT;
		case Byte4N: return VK_FORMAT_R8G8B8A8_SNORM;
		case UByte: return VK_FORMAT_R8_UINT;
		case UByte4N: return VK_FORMAT_R8G8B8A8_UNORM;
		case Short2: return VK_FORMAT_R16G16_SINT;
		case Short2N: return VK_FORMAT_R16G16_SNORM;
		case Short4: return VK_FORMAT_R16G16B16A16_SINT;
		case Short4N: return VK_FORMAT_R16G16B16A16_SNORM;
		case Uint: return VK_FORMAT_R32_UINT;
		case Uint2: return VK_FORMAT_R32G32_UINT;
		case Uint4: return VK_FORMAT_R32G32B32A32_UINT;
		default: return VK_FORMAT_UNDEFINED;
		}
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
	if (state == AshResourceState::Unknown)
	{
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}
	if (has_any_flags((uint32_t)state, (uint32_t)AshResourceState::UAVMask))
	{
		return VK_IMAGE_LAYOUT_GENERAL;
	}
	if (state == AshResourceState::RTV)
	{
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	if (has_any_flags((uint32_t)state, (uint32_t)AshResourceState::DSVWrite))
	{
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	if (has_any_flags((uint32_t)state, (uint32_t)AshResourceState::DSVRead))
	{
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	}
	if (has_any_flags((uint32_t)state, (uint32_t)AshResourceState::SRVMask))
	{
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	if (has_any_flags((uint32_t)state, (uint32_t)(AshResourceState::CopySrc | AshResourceState::ResolveSrc)))
	{
		return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	}
	if (has_any_flags((uint32_t)state, (uint32_t)(AshResourceState::CopyDst | AshResourceState::ResolveDst)))
	{
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}
	if (state == AshResourceState::Present)
	{
		return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}
	if (state == AshResourceState::ShadingRateSource)
	{
		return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
	}
	HLogWarning("unsupported state, trans to undefined !");
	return VK_IMAGE_LAYOUT_UNDEFINED;
}

	inline auto ash_is_valid_transition(const AshResourceState& src, const AshResourceState& dst) -> bool
	{
		if (src == dst)
		{
			return false;
		}
		if (dst == AshResourceState::Unknown)
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

	inline VkImageAspectFlags get_aspect_flags_from_format(AshFormat eAshFormat)
	{
		switch (eAshFormat)
		{
		case ASH_FORMAT_D24_UNORM_S8_UINT:
			return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		case ASH_FORMAT_D16_UNORM:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		case ASH_FORMAT_D32_SFLOAT:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		case ASH_FORMAT_D32_SFLOAT_S8_UINT:
			return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		default:
			return VK_IMAGE_ASPECT_COLOR_BIT;
		}
	}
	
};

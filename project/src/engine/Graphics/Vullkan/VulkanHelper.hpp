#pragma once
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "VulkanWrapper.h"
#include "Graphics/RHICommon.h"
#include "Graphics/RHIResource.h"
namespace RHI
{
	const char* vulkan_error_string(VkResult errorCode);
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
	auto ash_image_type_to_vk(const AshImageType& type) -> VkImageType;
	auto ash_format_to_vk(const AshFormat& format) -> VkFormat;
	auto ash_color_space_to_vk(const AshColorSpace& colorSpace) -> VkColorSpaceKHR;
	auto ash_present_mode_to_vk(const AshPresentMode& presentMode)->VkPresentModeKHR;
	auto get_image_usage_vulkan(const TextureCreation& creation) -> VkImageUsageFlags;
	auto ash_image_view_type_to_vk(const AshImageViewType& type) -> VkImageViewType;
	auto ash_image_view_type_to_vk(const AshImageViewType& type) -> VkImageViewType;
	auto ash_image_type_to_image_view_type(AshImageType& type) -> AshImageViewType;
};
#pragma once
#include "VulkanHelper.hpp"
namespace RHI
{
	const AshTextureFormatInfo g_ashTextureFormatInfo[] =
	{
		// enumTextureFormat			    uBytesPerBlock	uWidthPerBlock  uHeightPerBlock uHasAlpha	VkFormat								VkFormat(SRGB)
		{ASH_FORMAT_UNDEFINED,                      0u,  0u, 0u, 0u, VK_FORMAT_UNDEFINED,                 VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R8G8B8A8_UNORM,            4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_UNORM,            VK_FORMAT_R8G8B8A8_SRGB           },
		{ASH_FORMAT_R8G8B8A8_SNORM,            4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_SNORM,            VK_FORMAT_R8G8B8A8_SRGB           },
		{ASH_FORMAT_R8G8B8A8_SRGB,             4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_SRGB,             VK_FORMAT_R8G8B8A8_SRGB           },
		{ASH_FORMAT_R8G8B8A8_UINT,             4u,  1u, 1u, 1u, VK_FORMAT_R8G8B8A8_UINT,             VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R8_UNORM,                  1u,  1u, 1u, 0u, VK_FORMAT_R8_UNORM,                  VK_FORMAT_R8_SRGB                 },
		{ASH_FORMAT_R8G8_UNORM,                2u,  1u, 1u, 0u, VK_FORMAT_R8G8_UNORM,                VK_FORMAT_R8G8_SRGB               },
		{ASH_FORMAT_R16G16_UINT,               4u,  1u, 1u, 0u, VK_FORMAT_R16G16_UINT,               VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_B8G8R8_UNORM,              3u,  1u, 1u, 0u, VK_FORMAT_B8G8R8_UNORM,              VK_FORMAT_B8G8R8_SRGB             },
		{ASH_FORMAT_R8G8B8_UNORM,              3u,  1u, 1u, 0u, VK_FORMAT_R8G8B8_UNORM,              VK_FORMAT_R8G8B8_SRGB             },

		{ASH_FORMAT_B8G8R8A8_UNORM,            4u,  1u, 1u, 1u, VK_FORMAT_B8G8R8A8_UNORM,            VK_FORMAT_B8G8R8A8_SRGB           },
		{ASH_FORMAT_B8G8R8A8_SRGB,             4u,  1u, 1u, 1u, VK_FORMAT_B8G8R8A8_SRGB,             VK_FORMAT_B8G8R8A8_SRGB           },
		{ASH_FORMAT_R16G16B16A16_UNORM,        8u,  1u, 1u, 1u, VK_FORMAT_R16G16B16A16_UNORM,        VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R16G16B16A16_SFLOAT,       8u,  1u, 1u, 1u, VK_FORMAT_R16G16B16A16_SFLOAT,       VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_R32G32B32A32_SFLOAT,       16u, 1u, 1u, 1u, VK_FORMAT_R32G32B32A32_SFLOAT,       VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_R16_SFLOAT,                2u,  1u, 1u, 0u, VK_FORMAT_R16_SFLOAT,                VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_R16_UINT,                  2u,  1u, 1u, 0u, VK_FORMAT_R16_UINT,                  VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R16G16_SFLOAT,             4u,  1u, 1u, 0u, VK_FORMAT_R16G16_SFLOAT,             VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_R32_SINT,                  4u,  1u, 1u, 0u, VK_FORMAT_R32_SINT,                  VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_R32_UINT,                  4u,  1u, 1u, 0u, VK_FORMAT_R32_UINT,                  VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R32_FLOAT,                 4u,  1u, 1u, 0u, VK_FORMAT_R32_SFLOAT,                VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_D24_UNORM_S8_UINT,         4u,  1u, 1u, 0u, VK_FORMAT_D24_UNORM_S8_UINT,         VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_D16_UNORM,                 2u,  1u, 1u, 0u, VK_FORMAT_D16_UNORM,                 VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_D32_SFLOAT,                4u,  1u, 1u, 0u, VK_FORMAT_D32_SFLOAT,                VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_D32_SFLOAT_S8_UINT,        8u,  1u, 1u, 0u, VK_FORMAT_D32_SFLOAT_S8_UINT,        VK_FORMAT_UNDEFINED               }, //  24-bits that are unused

		{ASH_FORMAT_R64_UINT,                  8u,  1u, 1u, 0u, VK_FORMAT_R64_UINT,                  VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_BC1_RGB_UNORM,             8u,  4u, 4u, 0u, VK_FORMAT_BC1_RGB_UNORM_BLOCK,       VK_FORMAT_BC1_RGB_SRGB_BLOCK      },
		{ASH_FORMAT_BC1_RGBA_UNORM,            8u,  4u, 4u, 0u, VK_FORMAT_BC1_RGBA_UNORM_BLOCK,      VK_FORMAT_BC1_RGBA_SRGB_BLOCK     },
		{ASH_FORMAT_BC2_UNORM,                 16u, 4u, 4u, 0u, VK_FORMAT_BC2_UNORM_BLOCK,           VK_FORMAT_BC2_SRGB_BLOCK          },
		{ASH_FORMAT_BC3_UNORM,                 16u, 4u, 4u, 1u, VK_FORMAT_BC3_UNORM_BLOCK,           VK_FORMAT_BC3_SRGB_BLOCK          },

		{ASH_FORMAT_BC4_UNORM,                 8u,  4u, 4u, 0u, VK_FORMAT_BC4_UNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_BC4_SNORM,                 8u,  4u, 4u, 0u, VK_FORMAT_BC4_SNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_BC5_UNORM,                 16u, 4u, 4u, 0u, VK_FORMAT_BC5_UNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_BC5_SNORM,                 16u, 4u, 4u, 0u, VK_FORMAT_BC5_SNORM_BLOCK,           VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_BC6H_UFLOAT,               16u, 4u, 4u, 0u, VK_FORMAT_BC6H_UFLOAT_BLOCK,         VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_BC6H_SFLOAT,               16u, 4u, 4u, 0u, VK_FORMAT_BC6H_SFLOAT_BLOCK,         VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_BC7_UNORM,                 16u, 4u, 4u, 1u, VK_FORMAT_BC7_UNORM_BLOCK,           VK_FORMAT_BC7_SRGB_BLOCK          },
		{ASH_FORMAT_BC7_SRGB_UNORM,            16u, 4u, 4u, 1u, VK_FORMAT_BC7_SRGB_BLOCK,            VK_FORMAT_BC7_SRGB_BLOCK          },

		{ASH_FORMAT_B5G6R5_UNORM_PACK16,       2u,  1u, 1u, 0u, VK_FORMAT_B5G6R5_UNORM_PACK16,       VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_A2R10G10B10_UNORM_PACK32,  4u,  1u, 1u, 1u, VK_FORMAT_A2B10G10R10_UNORM_PACK32,  VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_B10G11R11_UFLOAT_PACK32,   4u,  1u, 1u, 0u, VK_FORMAT_B10G11R11_UFLOAT_PACK32,   VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,   8u,  4u, 4u, 0u, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,   VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK  },
		{ASH_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, 16u, 4u, 4u, 1u, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK},

		{ASH_FORMAT_ETC2_R_UNORM_BLOCK,        8u,  4u, 4u, 0u, VK_FORMAT_EAC_R11_UNORM_BLOCK,       VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_ETC2_R_SNORM_BLOCK,        8u,  4u, 4u, 0u, VK_FORMAT_EAC_R11_SNORM_BLOCK,       VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_ETC2_RG_UNORM_BLOCK,       16u, 4u, 4u, 0u, VK_FORMAT_EAC_R11G11_UNORM_BLOCK,    VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_ETC2_RG_SNORM_BLOCK,       16u, 4u, 4u, 0u, VK_FORMAT_EAC_R11G11_SNORM_BLOCK,    VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_ASTC_4X4_UNORM_BLOCK,      16u, 4u, 4u, 1u, VK_FORMAT_ASTC_4x4_UNORM_BLOCK,      VK_FORMAT_ASTC_4x4_SRGB_BLOCK     },
		{ASH_FORMAT_ASTC_6X6_UNORM_BLOCK,      16u, 6u, 6u, 1u, VK_FORMAT_ASTC_6x6_UNORM_BLOCK,      VK_FORMAT_ASTC_6x6_SRGB_BLOCK     },
		{ASH_FORMAT_ASTC_8X8_UNORM_BLOCK,      16u, 8u, 8u, 1u, VK_FORMAT_ASTC_8x8_UNORM_BLOCK,      VK_FORMAT_ASTC_8x8_SRGB_BLOCK     },
		{ASH_FORMAT_R32G32_UINT,               8u,  1u, 1u, 0u, VK_FORMAT_R32G32_UINT,               VK_FORMAT_UNDEFINED               },
		{ASH_FORMAT_R32G32B32A32_UINT,         16u, 1u, 1u, 1u, VK_FORMAT_R32G32B32A32_UINT,         VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R16G16_UNORM,              4u,  1u, 1u, 0u, VK_FORMAT_R16G16_UNORM,              VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R8_UINT,                   1u,  1u, 1u, 0u, VK_FORMAT_R8_UINT,                   VK_FORMAT_UNDEFINED               },

		{ASH_FORMAT_R16_UNORM,                 2u,  1u, 1u, 0u, VK_FORMAT_R16_UNORM,                 VK_FORMAT_UNDEFINED               },
	};


	const AshTextureFormatInfo& get_vk_texture_format_info(AshFormat eFormat)
	{
		H_ASSERT(eFormat < ASH_FORMAT_COUNT);
		H_ASSERT(g_ashTextureFormatInfo[eFormat].format == eFormat);
		return g_ashTextureFormatInfo[eFormat];
	}
};
#include "Function/Render/RenderFormatUtils.h"

#include <algorithm>

namespace AshEngine
{
	bool is_depth_render_texture_format(RenderTextureFormat format)
	{
		return format == RenderTextureFormat::D24_UNORM_S8_UINT ||
			format == RenderTextureFormat::D32_SFLOAT;
	}

	RHI::AshFormat render_texture_format_to_rhi(RenderTextureFormat format)
	{
		switch (format)
		{
		case RenderTextureFormat::RGBA8_UNORM:
			return RHI::ASH_FORMAT_R8G8B8A8_UNORM;
		case RenderTextureFormat::RGBA8_SRGB:
			return RHI::ASH_FORMAT_R8G8B8A8_SRGB;
		case RenderTextureFormat::BGRA8_UNORM:
			return RHI::ASH_FORMAT_B8G8R8A8_UNORM;
		case RenderTextureFormat::BGRA8_SRGB:
			return RHI::ASH_FORMAT_B8G8R8A8_SRGB;
		case RenderTextureFormat::RGBA16_SFLOAT:
			return RHI::ASH_FORMAT_R16G16B16A16_SFLOAT;
		case RenderTextureFormat::RGBA32_SFLOAT:
			return RHI::ASH_FORMAT_R32G32B32A32_SFLOAT;
		case RenderTextureFormat::BC1_RGB_UNORM:
			return RHI::ASH_FORMAT_BC1_RGB_UNORM;
		case RenderTextureFormat::BC1_RGB_SRGB_UNORM:
			return RHI::ASH_FORMAT_BC1_RGB_SRGB_UNORM;
		case RenderTextureFormat::BC1_RGBA_UNORM:
			return RHI::ASH_FORMAT_BC1_RGBA_UNORM;
		case RenderTextureFormat::BC1_RGBA_SRGB_UNORM:
			return RHI::ASH_FORMAT_BC1_RGBA_SRGB_UNORM;
		case RenderTextureFormat::BC2_UNORM:
			return RHI::ASH_FORMAT_BC2_UNORM;
		case RenderTextureFormat::BC2_SRGB_UNORM:
			return RHI::ASH_FORMAT_BC2_SRGB_UNORM;
		case RenderTextureFormat::BC3_UNORM:
			return RHI::ASH_FORMAT_BC3_UNORM;
		case RenderTextureFormat::BC3_SRGB_UNORM:
			return RHI::ASH_FORMAT_BC3_SRGB_UNORM;
		case RenderTextureFormat::BC4_UNORM:
			return RHI::ASH_FORMAT_BC4_UNORM;
		case RenderTextureFormat::BC4_SNORM:
			return RHI::ASH_FORMAT_BC4_SNORM;
		case RenderTextureFormat::BC5_UNORM:
			return RHI::ASH_FORMAT_BC5_UNORM;
		case RenderTextureFormat::BC5_SNORM:
			return RHI::ASH_FORMAT_BC5_SNORM;
		case RenderTextureFormat::BC6H_UFLOAT:
			return RHI::ASH_FORMAT_BC6H_UFLOAT;
		case RenderTextureFormat::BC6H_SFLOAT:
			return RHI::ASH_FORMAT_BC6H_SFLOAT;
		case RenderTextureFormat::BC7_UNORM:
			return RHI::ASH_FORMAT_BC7_UNORM;
		case RenderTextureFormat::BC7_SRGB_UNORM:
			return RHI::ASH_FORMAT_BC7_SRGB_UNORM;
		case RenderTextureFormat::D24_UNORM_S8_UINT:
			return RHI::ASH_FORMAT_D24_UNORM_S8_UINT;
		case RenderTextureFormat::D32_SFLOAT:
			return RHI::ASH_FORMAT_D32_SFLOAT;
		default:
			return RHI::ASH_FORMAT_UNDEFINED;
		}
	}

	RenderTextureFormat render_texture_format_from_rhi(RHI::AshFormat format)
	{
		switch (format)
		{
		case RHI::ASH_FORMAT_R8G8B8A8_UNORM:
			return RenderTextureFormat::RGBA8_UNORM;
		case RHI::ASH_FORMAT_R8G8B8A8_SRGB:
			return RenderTextureFormat::RGBA8_SRGB;
		case RHI::ASH_FORMAT_B8G8R8A8_UNORM:
			return RenderTextureFormat::BGRA8_UNORM;
		case RHI::ASH_FORMAT_B8G8R8A8_SRGB:
			return RenderTextureFormat::BGRA8_SRGB;
		case RHI::ASH_FORMAT_R16G16B16A16_SFLOAT:
			return RenderTextureFormat::RGBA16_SFLOAT;
		case RHI::ASH_FORMAT_R32G32B32A32_SFLOAT:
			return RenderTextureFormat::RGBA32_SFLOAT;
		case RHI::ASH_FORMAT_BC1_RGB_UNORM:
			return RenderTextureFormat::BC1_RGB_UNORM;
		case RHI::ASH_FORMAT_BC1_RGB_SRGB_UNORM:
			return RenderTextureFormat::BC1_RGB_SRGB_UNORM;
		case RHI::ASH_FORMAT_BC1_RGBA_UNORM:
			return RenderTextureFormat::BC1_RGBA_UNORM;
		case RHI::ASH_FORMAT_BC1_RGBA_SRGB_UNORM:
			return RenderTextureFormat::BC1_RGBA_SRGB_UNORM;
		case RHI::ASH_FORMAT_BC2_UNORM:
			return RenderTextureFormat::BC2_UNORM;
		case RHI::ASH_FORMAT_BC2_SRGB_UNORM:
			return RenderTextureFormat::BC2_SRGB_UNORM;
		case RHI::ASH_FORMAT_BC3_UNORM:
			return RenderTextureFormat::BC3_UNORM;
		case RHI::ASH_FORMAT_BC3_SRGB_UNORM:
			return RenderTextureFormat::BC3_SRGB_UNORM;
		case RHI::ASH_FORMAT_BC4_UNORM:
			return RenderTextureFormat::BC4_UNORM;
		case RHI::ASH_FORMAT_BC4_SNORM:
			return RenderTextureFormat::BC4_SNORM;
		case RHI::ASH_FORMAT_BC5_UNORM:
			return RenderTextureFormat::BC5_UNORM;
		case RHI::ASH_FORMAT_BC5_SNORM:
			return RenderTextureFormat::BC5_SNORM;
		case RHI::ASH_FORMAT_BC6H_UFLOAT:
			return RenderTextureFormat::BC6H_UFLOAT;
		case RHI::ASH_FORMAT_BC6H_SFLOAT:
			return RenderTextureFormat::BC6H_SFLOAT;
		case RHI::ASH_FORMAT_BC7_UNORM:
			return RenderTextureFormat::BC7_UNORM;
		case RHI::ASH_FORMAT_BC7_SRGB_UNORM:
			return RenderTextureFormat::BC7_SRGB_UNORM;
		case RHI::ASH_FORMAT_D24_UNORM_S8_UINT:
			return RenderTextureFormat::D24_UNORM_S8_UINT;
		case RHI::ASH_FORMAT_D32_SFLOAT:
			return RenderTextureFormat::D32_SFLOAT;
		default:
			return RenderTextureFormat::Unknown;
		}
	}

	RenderTextureFormat resolve_texture_upload_public_format(const TextureUploadDesc& desc)
	{
		if (!desc.srgb)
		{
			return desc.format;
		}

		switch (desc.format)
		{
		case RenderTextureFormat::RGBA8_UNORM:
			return RenderTextureFormat::RGBA8_SRGB;
		case RenderTextureFormat::BC1_RGB_UNORM:
			return RenderTextureFormat::BC1_RGB_SRGB_UNORM;
		case RenderTextureFormat::BC1_RGBA_UNORM:
			return RenderTextureFormat::BC1_RGBA_SRGB_UNORM;
		case RenderTextureFormat::BC2_UNORM:
			return RenderTextureFormat::BC2_SRGB_UNORM;
		case RenderTextureFormat::BC3_UNORM:
			return RenderTextureFormat::BC3_SRGB_UNORM;
		case RenderTextureFormat::BC7_UNORM:
			return RenderTextureFormat::BC7_SRGB_UNORM;
		default:
			return desc.format;
		}
	}

	RenderTextureFormatBlockInfo get_render_texture_format_block_info(RenderTextureFormat format)
	{
		switch (format)
		{
		case RenderTextureFormat::RGBA8_UNORM:
		case RenderTextureFormat::RGBA8_SRGB:
		case RenderTextureFormat::BGRA8_UNORM:
		case RenderTextureFormat::BGRA8_SRGB:
			return { 4u, 1u, 1u };
		case RenderTextureFormat::RGBA16_SFLOAT:
			return { 8u, 1u, 1u };
		case RenderTextureFormat::RGBA32_SFLOAT:
			return { 16u, 1u, 1u };
		case RenderTextureFormat::BC1_RGB_UNORM:
		case RenderTextureFormat::BC1_RGB_SRGB_UNORM:
		case RenderTextureFormat::BC1_RGBA_UNORM:
		case RenderTextureFormat::BC1_RGBA_SRGB_UNORM:
		case RenderTextureFormat::BC4_UNORM:
		case RenderTextureFormat::BC4_SNORM:
			return { 8u, 4u, 4u };
		case RenderTextureFormat::BC2_UNORM:
		case RenderTextureFormat::BC2_SRGB_UNORM:
		case RenderTextureFormat::BC3_UNORM:
		case RenderTextureFormat::BC3_SRGB_UNORM:
		case RenderTextureFormat::BC5_UNORM:
		case RenderTextureFormat::BC5_SNORM:
		case RenderTextureFormat::BC6H_UFLOAT:
		case RenderTextureFormat::BC6H_SFLOAT:
		case RenderTextureFormat::BC7_UNORM:
		case RenderTextureFormat::BC7_SRGB_UNORM:
			return { 16u, 4u, 4u };
		default:
			return {};
		}
	}

	uint32_t calculate_render_texture_tight_row_pitch(RenderTextureFormat format, uint32_t width)
	{
		const RenderTextureFormatBlockInfo info = get_render_texture_format_block_info(format);
		if (info.bytes_per_block == 0 || info.width_per_block == 0)
		{
			return 0;
		}
		const uint32_t block_count_x = std::max<uint32_t>(1u, (width + info.width_per_block - 1u) / info.width_per_block);
		return block_count_x * info.bytes_per_block;
	}

	uint64_t calculate_render_texture_tight_mip_size(RenderTextureFormat format, uint32_t width, uint32_t height)
	{
		const RenderTextureFormatBlockInfo info = get_render_texture_format_block_info(format);
		if (info.bytes_per_block == 0 || info.width_per_block == 0 || info.height_per_block == 0)
		{
			return 0;
		}
		const uint32_t block_count_y = std::max<uint32_t>(1u, (height + info.height_per_block - 1u) / info.height_per_block);
		return static_cast<uint64_t>(calculate_render_texture_tight_row_pitch(format, width)) * block_count_y;
	}
}

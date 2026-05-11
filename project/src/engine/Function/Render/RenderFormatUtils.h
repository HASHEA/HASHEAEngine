#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include "Graphics/RHICommon.h"
#include <cstdint>

namespace AshEngine
{
	struct RenderTextureFormatBlockInfo
	{
		uint32_t bytes_per_block = 0;
		uint32_t width_per_block = 0;
		uint32_t height_per_block = 0;
	};

	ASH_API bool is_depth_render_texture_format(RenderTextureFormat format);
	ASH_API RHI::AshFormat render_texture_format_to_rhi(RenderTextureFormat format);
	ASH_API RenderTextureFormat render_texture_format_from_rhi(RHI::AshFormat format);
	ASH_API RenderTextureFormat resolve_texture_upload_public_format(const TextureUploadDesc& desc);
	ASH_API RenderTextureFormatBlockInfo get_render_texture_format_block_info(RenderTextureFormat format);
	ASH_API uint32_t calculate_render_texture_tight_row_pitch(RenderTextureFormat format, uint32_t width);
	ASH_API uint64_t calculate_render_texture_tight_mip_size(RenderTextureFormat format, uint32_t width, uint32_t height);
}

#pragma once
#include "DX12Wrapper.h"
#include "Base/hassert.h"
#include "Base/hlog.h"
#include "Graphics/RHICommon.h"
#include "Graphics/RHIResource.h"
#include "Graphics/Texture.h"
#include "Graphics/Pipeline.h"
#include "Graphics/Sampler.h"

namespace RHI
{

// ──────────────────────────────────────────────────────────────────
// Format Mapping: AshFormat ↔ DXGI_FORMAT
// ──────────────────────────────────────────────────────────────────

struct AshDXGIFormatInfo
{
	AshFormat format;
	uint32_t uBytesPerBlock : 15;
	uint32_t uWidthPerBlock : 8;
	uint32_t uHeightPerBlock : 8;
	uint32_t uHasAlpha : 1;
	DXGI_FORMAT dxgiFormat;
	DXGI_FORMAT dxgiFormatSRGB;
};

inline const AshDXGIFormatInfo g_ashDXGIFormatInfo[] =
{
	// AshFormat                                    BPB  W  H  A   DXGI_FORMAT                          DXGI_FORMAT_SRGB
	{ ASH_FORMAT_UNDEFINED,                          0, 0, 0, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R8G8B8A8_UNORM,                     4, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,           DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
	{ ASH_FORMAT_R8G8B8A8_SNORM,                     4, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_SNORM,           DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R8G8B8A8_SRGB,                      4, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
	{ ASH_FORMAT_R8G8B8A8_UINT,                      4, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UINT,            DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R8_UNORM,                           1, 1, 1, 0, DXGI_FORMAT_R8_UNORM,                 DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R8G8_UNORM,                         2, 1, 1, 0, DXGI_FORMAT_R8G8_UNORM,               DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R16G16_UINT,                        4, 1, 1, 0, DXGI_FORMAT_R16G16_UINT,              DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_B8G8R8_UNORM,                       3, 1, 1, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN }, // No direct DX12 equivalent for RGB8
	{ ASH_FORMAT_R8G8B8_UNORM,                       3, 1, 1, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN }, // No direct DX12 equivalent

	{ ASH_FORMAT_B8G8R8A8_UNORM,                     4, 1, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM,           DXGI_FORMAT_B8G8R8A8_UNORM_SRGB },
	{ ASH_FORMAT_B8G8R8A8_SRGB,                      4, 1, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB },
	{ ASH_FORMAT_R16G16B16A16_UNORM,                 8, 1, 1, 1, DXGI_FORMAT_R16G16B16A16_UNORM,       DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R16G16B16A16_SFLOAT,                8, 1, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT,       DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R32G32B32A32_SFLOAT,               16, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,       DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R16_SFLOAT,                         2, 1, 1, 0, DXGI_FORMAT_R16_FLOAT,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R16_UINT,                           2, 1, 1, 0, DXGI_FORMAT_R16_UINT,                 DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R16G16_SFLOAT,                      4, 1, 1, 0, DXGI_FORMAT_R16G16_FLOAT,             DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R32_SINT,                           4, 1, 1, 0, DXGI_FORMAT_R32_SINT,                 DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R32_UINT,                           4, 1, 1, 0, DXGI_FORMAT_R32_UINT,                 DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R32_FLOAT,                          4, 1, 1, 0, DXGI_FORMAT_R32_FLOAT,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_D24_UNORM_S8_UINT,                  4, 1, 1, 0, DXGI_FORMAT_D24_UNORM_S8_UINT,        DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_D16_UNORM,                          2, 1, 1, 0, DXGI_FORMAT_D16_UNORM,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_D32_SFLOAT,                         4, 1, 1, 0, DXGI_FORMAT_D32_FLOAT,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_D32_SFLOAT_S8_UINT,                 8, 1, 1, 0, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,     DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R64_UINT,                           8, 1, 1, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN }, // No DX12 equivalent

	{ ASH_FORMAT_BC1_RGB_UNORM,                      8, 4, 4, 0, DXGI_FORMAT_BC1_UNORM,                DXGI_FORMAT_BC1_UNORM_SRGB },
	{ ASH_FORMAT_BC1_RGBA_UNORM,                     8, 4, 4, 1, DXGI_FORMAT_BC1_UNORM,                DXGI_FORMAT_BC1_UNORM_SRGB },
	{ ASH_FORMAT_BC2_UNORM,                         16, 4, 4, 1, DXGI_FORMAT_BC2_UNORM,                DXGI_FORMAT_BC2_UNORM_SRGB },
	{ ASH_FORMAT_BC3_UNORM,                         16, 4, 4, 1, DXGI_FORMAT_BC3_UNORM,                DXGI_FORMAT_BC3_UNORM_SRGB },

	{ ASH_FORMAT_BC4_UNORM,                          8, 4, 4, 0, DXGI_FORMAT_BC4_UNORM,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_BC4_SNORM,                          8, 4, 4, 0, DXGI_FORMAT_BC4_SNORM,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_BC5_UNORM,                         16, 4, 4, 0, DXGI_FORMAT_BC5_UNORM,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_BC5_SNORM,                         16, 4, 4, 0, DXGI_FORMAT_BC5_SNORM,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_BC6H_UFLOAT,                       16, 4, 4, 0, DXGI_FORMAT_BC6H_UF16,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_BC6H_SFLOAT,                       16, 4, 4, 0, DXGI_FORMAT_BC6H_SF16,                DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_BC7_UNORM,                         16, 4, 4, 1, DXGI_FORMAT_BC7_UNORM,                DXGI_FORMAT_BC7_UNORM_SRGB },
	{ ASH_FORMAT_BC7_SRGB_UNORM,                    16, 4, 4, 1, DXGI_FORMAT_BC7_UNORM_SRGB,           DXGI_FORMAT_BC7_UNORM_SRGB },

	{ ASH_FORMAT_B5G6R5_UNORM_PACK16,                2, 1, 1, 0, DXGI_FORMAT_B5G6R5_UNORM,             DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_A2R10G10B10_UNORM_PACK32,           4, 1, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM,        DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_B10G11R11_UFLOAT_PACK32,            4, 1, 1, 0, DXGI_FORMAT_R11G11B10_FLOAT,          DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,            8, 4, 4, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN }, // ETC2 not supported on DX12
	{ ASH_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,         16, 4, 4, 1, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_ETC2_R_UNORM_BLOCK,                 8, 4, 4, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_ETC2_R_SNORM_BLOCK,                 8, 4, 4, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_ETC2_RG_UNORM_BLOCK,               16, 4, 4, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_ETC2_RG_SNORM_BLOCK,               16, 4, 4, 0, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_ASTC_4X4_UNORM_BLOCK,              16, 4, 4, 1, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN }, // ASTC not supported on DX12
	{ ASH_FORMAT_ASTC_6X6_UNORM_BLOCK,              16, 6, 6, 1, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_ASTC_8X8_UNORM_BLOCK,              16, 8, 8, 1, DXGI_FORMAT_UNKNOWN,                  DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R32G32_UINT,                        8, 1, 1, 0, DXGI_FORMAT_R32G32_UINT,              DXGI_FORMAT_UNKNOWN },
	{ ASH_FORMAT_R32G32B32A32_UINT,                 16, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_UINT,        DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R16G16_UNORM,                       4, 1, 1, 0, DXGI_FORMAT_R16G16_UNORM,             DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R8_UINT,                            1, 1, 1, 0, DXGI_FORMAT_R8_UINT,                  DXGI_FORMAT_UNKNOWN },

	{ ASH_FORMAT_R16_UNORM,                          2, 1, 1, 0, DXGI_FORMAT_R16_UNORM,                DXGI_FORMAT_UNKNOWN },
};

inline const AshDXGIFormatInfo& get_dxgi_format_info(AshFormat eFormat)
{
	H_ASSERT(eFormat < ASH_FORMAT_COUNT);
	return g_ashDXGIFormatInfo[eFormat];
}

inline DXGI_FORMAT ash_to_dxgi_format(AshFormat eFormat)
{
	return get_dxgi_format_info(eFormat).dxgiFormat;
}

inline AshFormat dxgi_to_ash_format(DXGI_FORMAT dxgiFormat)
{
	for (uint32_t i = 0; i < ASH_FORMAT_COUNT; ++i)
	{
		if (g_ashDXGIFormatInfo[i].dxgiFormat == dxgiFormat)
			return g_ashDXGIFormatInfo[i].format;
	}
	HLogWarning("Unsupported DXGI_FORMAT {} in dxgi_to_ash_format, fallback to ASH_FORMAT_R8G8B8A8_UNORM.", static_cast<int32_t>(dxgiFormat));
	return ASH_FORMAT_R8G8B8A8_UNORM;
}

// For depth-stencil, we need typeless format for resource creation, and typed for DSV
inline DXGI_FORMAT ash_to_dxgi_depth_resource_format(AshFormat eFormat)
{
	switch (eFormat)
	{
	case ASH_FORMAT_D16_UNORM:            return DXGI_FORMAT_R16_TYPELESS;
	case ASH_FORMAT_D24_UNORM_S8_UINT:    return DXGI_FORMAT_R24G8_TYPELESS;
	case ASH_FORMAT_D32_SFLOAT:           return DXGI_FORMAT_R32_TYPELESS;
	case ASH_FORMAT_D32_SFLOAT_S8_UINT:   return DXGI_FORMAT_R32G8X24_TYPELESS;
	default:                               return ash_to_dxgi_format(eFormat);
	}
}

// SRV format for depth textures
inline DXGI_FORMAT ash_to_dxgi_depth_srv_format(AshFormat eFormat)
{
	switch (eFormat)
	{
	case ASH_FORMAT_D16_UNORM:            return DXGI_FORMAT_R16_UNORM;
	case ASH_FORMAT_D24_UNORM_S8_UINT:    return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case ASH_FORMAT_D32_SFLOAT:           return DXGI_FORMAT_R32_FLOAT;
	case ASH_FORMAT_D32_SFLOAT_S8_UINT:   return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	default:                               return ash_to_dxgi_format(eFormat);
	}
}

// ──────────────────────────────────────────────────────────────────
// Depth/Stencil Helpers
// ──────────────────────────────────────────────────────────────────
namespace DX12TextureFormat {

	inline bool is_depth_format(AshFormat format)
	{
		return format == ASH_FORMAT_D16_UNORM ||
		       format == ASH_FORMAT_D24_UNORM_S8_UINT ||
		       format == ASH_FORMAT_D32_SFLOAT ||
		       format == ASH_FORMAT_D32_SFLOAT_S8_UINT;
	}

	inline bool has_stencil(AshFormat format)
	{
		return format == ASH_FORMAT_D24_UNORM_S8_UINT ||
		       format == ASH_FORMAT_D32_SFLOAT_S8_UINT;
	}

	inline bool is_depth_format_dxgi(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return true;
		default:
			return false;
		}
	}

} // namespace DX12TextureFormat

// ──────────────────────────────────────────────────────────────────
// AshResourceState → D3D12_RESOURCE_STATES
// ──────────────────────────────────────────────────────────────────
inline D3D12_RESOURCE_STATES ash_to_d3d12_resource_state(AshResourceState state)
{
	D3D12_RESOURCE_STATES d3dState = D3D12_RESOURCE_STATE_COMMON;

	// CPURead is a host visibility semantic, not a D3D12 transition state.
	// Readback resources stay in COPY_DEST through their heap/access type.
	if (is_set(state, AshResourceState::Present))            d3dState |= D3D12_RESOURCE_STATE_PRESENT;
	if (is_set(state, AshResourceState::IndirectArgs))       d3dState |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
	if (is_set(state, AshResourceState::VertexBuffer))       d3dState |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	if (is_set(state, AshResourceState::IndexBuffer))        d3dState |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
	if (is_set(state, AshResourceState::ConstBuffer))        d3dState |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	if (is_set(state, AshResourceState::SRVCompute))         d3dState |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	if (is_set(state, AshResourceState::SRVGraphicsPixel))   d3dState |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	if (is_set(state, AshResourceState::SRVGraphicsNonPixel))d3dState |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	if (is_set(state, AshResourceState::CopySrc))            d3dState |= D3D12_RESOURCE_STATE_COPY_SOURCE;
	if (is_set(state, AshResourceState::ResolveSrc))         d3dState |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
	if (is_set(state, AshResourceState::DSVRead))            d3dState |= D3D12_RESOURCE_STATE_DEPTH_READ;
	if (is_set(state, AshResourceState::UAVCompute))         d3dState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (is_set(state, AshResourceState::UAVGraphics))        d3dState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (is_set(state, AshResourceState::RTV))                d3dState |= D3D12_RESOURCE_STATE_RENDER_TARGET;
	if (is_set(state, AshResourceState::CopyDst))            d3dState |= D3D12_RESOURCE_STATE_COPY_DEST;
	if (is_set(state, AshResourceState::ResolveDst))         d3dState |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
	if (is_set(state, AshResourceState::DSVWrite))           d3dState |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
	if (is_set(state, AshResourceState::BVHRead))            d3dState |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	if (is_set(state, AshResourceState::BVHWrite))           d3dState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (is_set(state, AshResourceState::ShadingRateSource))  d3dState |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;

	return d3dState;
}

// ──────────────────────────────────────────────────────────────────
// AshTextureUsageFlags → D3D12_RESOURCE_FLAGS
// ──────────────────────────────────────────────────────────────────
inline D3D12_RESOURCE_FLAGS ash_texture_usage_to_d3d12_flags(AshTextureUsageFlags usage, AshFormat format)
{
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
	if (usage & ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (usage & ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if (usage & ASH_TEXTURE_USAGE_STORAGE_BIT)
		flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	// If depth-stencil only and not sampled, deny SRV
	if ((usage & ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
	    !(usage & ASH_TEXTURE_USAGE_SAMPLED_BIT))
		flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	return flags;
}

// ──────────────────────────────────────────────────────────────────
// AshBufferUsageFlags → D3D12_RESOURCE_FLAGS
// ──────────────────────────────────────────────────────────────────
inline D3D12_RESOURCE_FLAGS ash_buffer_usage_to_d3d12_flags(AshBufferUsageFlags usage)
{
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
	if (has_any_flags(usage, ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT | ASH_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
		flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	return flags;
}

// ──────────────────────────────────────────────────────────────────
// Blend State Conversion
// ──────────────────────────────────────────────────────────────────
inline D3D12_BLEND ash_to_d3d12_blend(AshBlendFactor factor)
{
	switch (factor)
	{
	case ASH_BLEND_FACTOR_ZERO:                     return D3D12_BLEND_ZERO;
	case ASH_BLEND_FACTOR_ONE:                      return D3D12_BLEND_ONE;
	case ASH_BLEND_FACTOR_SRC_COLOR:                return D3D12_BLEND_SRC_COLOR;
	case ASH_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:      return D3D12_BLEND_INV_SRC_COLOR;
	case ASH_BLEND_FACTOR_DST_COLOR:                return D3D12_BLEND_DEST_COLOR;
	case ASH_BLEND_FACTOR_ONE_MINUS_DST_COLOR:      return D3D12_BLEND_INV_DEST_COLOR;
	case ASH_BLEND_FACTOR_SRC_ALPHA:                return D3D12_BLEND_SRC_ALPHA;
	case ASH_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:      return D3D12_BLEND_INV_SRC_ALPHA;
	case ASH_BLEND_FACTOR_DST_ALPHA:                return D3D12_BLEND_DEST_ALPHA;
	case ASH_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:      return D3D12_BLEND_INV_DEST_ALPHA;
	case ASH_BLEND_FACTOR_CONSTANT_COLOR:           return D3D12_BLEND_BLEND_FACTOR;
	case ASH_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return D3D12_BLEND_INV_BLEND_FACTOR;
	case ASH_BLEND_FACTOR_SRC_ALPHA_SATURATE:       return D3D12_BLEND_SRC_ALPHA_SAT;
	case ASH_BLEND_FACTOR_SRC1_COLOR:               return D3D12_BLEND_SRC1_COLOR;
	case ASH_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:     return D3D12_BLEND_INV_SRC1_COLOR;
	case ASH_BLEND_FACTOR_SRC1_ALPHA:               return D3D12_BLEND_SRC1_ALPHA;
	case ASH_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:     return D3D12_BLEND_INV_SRC1_ALPHA;
	default:                                         return D3D12_BLEND_ONE;
	}
}

inline D3D12_BLEND_OP ash_to_d3d12_blend_op(AshBlendOp op)
{
	switch (op)
	{
	case ASH_BLEND_OP_ADD:              return D3D12_BLEND_OP_ADD;
	case ASH_BLEND_OP_SUBTRACT:         return D3D12_BLEND_OP_SUBTRACT;
	case ASH_BLEND_OP_REVERSE_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
	case ASH_BLEND_OP_MIN:              return D3D12_BLEND_OP_MIN;
	case ASH_BLEND_OP_MAX:              return D3D12_BLEND_OP_MAX;
	default:                             return D3D12_BLEND_OP_ADD;
	}
}

inline UINT8 ash_to_d3d12_color_write_mask(AshColorWriteMask mask)
{
	UINT8 result = 0;
	if (static_cast<int>(mask) & static_cast<int>(AshColorWriteMask::Red))   result |= D3D12_COLOR_WRITE_ENABLE_RED;
	if (static_cast<int>(mask) & static_cast<int>(AshColorWriteMask::Green)) result |= D3D12_COLOR_WRITE_ENABLE_GREEN;
	if (static_cast<int>(mask) & static_cast<int>(AshColorWriteMask::Blue))  result |= D3D12_COLOR_WRITE_ENABLE_BLUE;
	if (static_cast<int>(mask) & static_cast<int>(AshColorWriteMask::Alpha)) result |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
	return result;
}

// ──────────────────────────────────────────────────────────────────
// Depth/Stencil State Conversion
// ──────────────────────────────────────────────────────────────────
inline D3D12_COMPARISON_FUNC ash_to_d3d12_comparison(AshCompareOp op)
{
	switch (op)
	{
	case ASH_COMPARE_OP_NEVER:            return D3D12_COMPARISON_FUNC_NEVER;
	case ASH_COMPARE_OP_LESS:             return D3D12_COMPARISON_FUNC_LESS;
	case ASH_COMPARE_OP_EQUAL:            return D3D12_COMPARISON_FUNC_EQUAL;
	case ASH_COMPARE_OP_LESS_OR_EQUAL:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
	case ASH_COMPARE_OP_GREATER:          return D3D12_COMPARISON_FUNC_GREATER;
	case ASH_COMPARE_OP_NOT_EQUAL:        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
	case ASH_COMPARE_OP_GREATER_OR_EQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	case ASH_COMPARE_OP_ALWAYS:           return D3D12_COMPARISON_FUNC_ALWAYS;
	default:                               return D3D12_COMPARISON_FUNC_ALWAYS;
	}
}

inline D3D12_STENCIL_OP ash_to_d3d12_stencil_op(AshStencilOp op)
{
	switch (op)
	{
	case ASH_STENCIL_OP_KEEP:                return D3D12_STENCIL_OP_KEEP;
	case ASH_STENCIL_OP_ZERO:                return D3D12_STENCIL_OP_ZERO;
	case ASH_STENCIL_OP_REPLACE:             return D3D12_STENCIL_OP_REPLACE;
	case ASH_STENCIL_OP_INCREMENT_AND_CLAMP: return D3D12_STENCIL_OP_INCR_SAT;
	case ASH_STENCIL_OP_DECREMENT_AND_CLAMP: return D3D12_STENCIL_OP_DECR_SAT;
	case ASH_STENCIL_OP_INVERT:              return D3D12_STENCIL_OP_INVERT;
	case ASH_STENCIL_OP_INCREMENT_AND_WRAP:  return D3D12_STENCIL_OP_INCR;
	case ASH_STENCIL_OP_DECREMENT_AND_WRAP:  return D3D12_STENCIL_OP_DECR;
	default:                                  return D3D12_STENCIL_OP_KEEP;
	}
}

// ──────────────────────────────────────────────────────────────────
// Rasterizer State Conversion
// ──────────────────────────────────────────────────────────────────
inline D3D12_CULL_MODE ash_to_d3d12_cull_mode(AshCullModeFlagBits mode)
{
	switch (mode)
	{
	case ASH_CULL_MODE_NONE:          return D3D12_CULL_MODE_NONE;
	case ASH_CULL_MODE_FRONT_BIT:     return D3D12_CULL_MODE_FRONT;
	case ASH_CULL_MODE_BACK_BIT:      return D3D12_CULL_MODE_BACK;
	case ASH_CULL_MODE_FRONT_AND_BACK:return D3D12_CULL_MODE_NONE; // DX12 doesn't have both-sided cull
	default:                           return D3D12_CULL_MODE_NONE;
	}
}

inline D3D12_FILL_MODE ash_to_d3d12_fill_mode(AshFillMode mode)
{
	switch (mode)
	{
	case AshFillMode::Wireframe: return D3D12_FILL_MODE_WIREFRAME;
	case AshFillMode::Solid:     return D3D12_FILL_MODE_SOLID;
	case AshFillMode::Point:     return D3D12_FILL_MODE_WIREFRAME; // DX12 has no point fill mode
	default:                      return D3D12_FILL_MODE_SOLID;
	}
}

// ──────────────────────────────────────────────────────────────────
// Vertex Format Conversion
// ──────────────────────────────────────────────────────────────────
inline DXGI_FORMAT ash_vertex_format_to_dxgi(AshVertexComponentFormat format)
{
	switch (format)
	{
	case AshVertexComponentFormat::Float:   return DXGI_FORMAT_R32_FLOAT;
	case AshVertexComponentFormat::Float2:  return DXGI_FORMAT_R32G32_FLOAT;
	case AshVertexComponentFormat::Float3:  return DXGI_FORMAT_R32G32B32_FLOAT;
	case AshVertexComponentFormat::Float4:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case AshVertexComponentFormat::Byte:    return DXGI_FORMAT_R8_SINT;
	case AshVertexComponentFormat::Byte4N:  return DXGI_FORMAT_R8G8B8A8_SNORM;
	case AshVertexComponentFormat::UByte:   return DXGI_FORMAT_R8_UINT;
	case AshVertexComponentFormat::UByte4N: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case AshVertexComponentFormat::Short2:  return DXGI_FORMAT_R16G16_SINT;
	case AshVertexComponentFormat::Short2N: return DXGI_FORMAT_R16G16_SNORM;
	case AshVertexComponentFormat::Short4:  return DXGI_FORMAT_R16G16B16A16_SINT;
	case AshVertexComponentFormat::Short4N: return DXGI_FORMAT_R16G16B16A16_SNORM;
	case AshVertexComponentFormat::Uint:    return DXGI_FORMAT_R32_UINT;
	case AshVertexComponentFormat::Uint2:   return DXGI_FORMAT_R32G32_UINT;
	case AshVertexComponentFormat::Uint4:   return DXGI_FORMAT_R32G32B32A32_UINT;
	default:                                 return DXGI_FORMAT_UNKNOWN;
	}
}

// ──────────────────────────────────────────────────────────────────
// Topology Conversion
// ──────────────────────────────────────────────────────────────────
inline D3D12_PRIMITIVE_TOPOLOGY_TYPE ash_to_d3d12_topology_type(AshPrimitiveTopology topology)
{
	switch (topology)
	{
	case ASH_PRIMITIVE_TOPOLOGY_POINT_LIST:          return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	case ASH_PRIMITIVE_TOPOLOGY_LINE_LIST:
	case ASH_PRIMITIVE_TOPOLOGY_LINE_STRIP:
	case ASH_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
	case ASH_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	case ASH_PRIMITIVE_TOPOLOGY_PATCH_LIST:
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	default:
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}
}

inline D3D_PRIMITIVE_TOPOLOGY ash_to_d3d_primitive_topology(AshPrimitiveTopology topology)
{
	switch (topology)
	{
	case ASH_PRIMITIVE_TOPOLOGY_POINT_LIST:                   return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	case ASH_PRIMITIVE_TOPOLOGY_LINE_LIST:                    return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
	case ASH_PRIMITIVE_TOPOLOGY_LINE_STRIP:                   return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:               return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	case ASH_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:     return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
	case ASH_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:    return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
	case ASH_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
	case ASH_PRIMITIVE_TOPOLOGY_PATCH_LIST:                   return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
	default:                                                   return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}
}

// ──────────────────────────────────────────────────────────────────
// Sampler State Conversion
// ──────────────────────────────────────────────────────────────────
inline D3D12_FILTER ash_to_d3d12_filter(AshFilter min, AshFilter mag, AshFilter mip, bool aniso, AshSamplerReductionMode reduction, bool compareEnabled)
{
	if (aniso)
	{
		if (compareEnabled)     return D3D12_FILTER_COMPARISON_ANISOTROPIC;
		if (reduction == ASH_SAMPLER_REDUCTION_MODE_MIN) return D3D12_FILTER_MINIMUM_ANISOTROPIC;
		if (reduction == ASH_SAMPLER_REDUCTION_MODE_MAX) return D3D12_FILTER_MAXIMUM_ANISOTROPIC;
		return D3D12_FILTER_ANISOTROPIC;
	}

	// Encode min/mag/mip as D3D12 filter bits
	UINT filterVal = 0;
	if (mip == ASH_FILTER_LINEAR) filterVal |= 0x01;
	if (mag == ASH_FILTER_LINEAR) filterVal |= 0x04;
	if (min == ASH_FILTER_LINEAR) filterVal |= 0x10;

	if (compareEnabled) filterVal |= 0x80;
	else if (reduction == ASH_SAMPLER_REDUCTION_MODE_MIN) filterVal |= 0x100;
	else if (reduction == ASH_SAMPLER_REDUCTION_MODE_MAX) filterVal |= 0x180;

	return static_cast<D3D12_FILTER>(filterVal);
}

inline D3D12_TEXTURE_ADDRESS_MODE ash_to_d3d12_address_mode(AshSamplerAddressMode mode)
{
	switch (mode)
	{
	case ASH_SAMPLER_ADDRESS_MODE_REPEAT:               return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	case ASH_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:      return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	case ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	case ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:      return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	case ASH_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
	default:                                              return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}
}

inline void ash_border_color_to_float4(AshBorderColor bc, float out[4])
{
	switch (bc)
	{
	case ASH_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
	case ASH_BORDER_COLOR_INT_TRANSPARENT_BLACK:
		out[0] = out[1] = out[2] = out[3] = 0.0f; break;
	case ASH_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
	case ASH_BORDER_COLOR_INT_OPAQUE_BLACK:
		out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; break;
	case ASH_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
	case ASH_BORDER_COLOR_INT_OPAQUE_WHITE:
		out[0] = out[1] = out[2] = out[3] = 1.0f; break;
	default:
		out[0] = out[1] = out[2] = out[3] = 0.0f; break;
	}
}

inline D3D12_STATIC_BORDER_COLOR ash_to_d3d12_static_border_color(AshBorderColor bc)
{
	switch (bc)
	{
	case ASH_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
	case ASH_BORDER_COLOR_INT_TRANSPARENT_BLACK:
		return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	case ASH_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
	case ASH_BORDER_COLOR_INT_OPAQUE_BLACK:
		return D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	case ASH_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
	case ASH_BORDER_COLOR_INT_OPAQUE_WHITE:
		return D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	default:
		return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	}
}

// ──────────────────────────────────────────────────────────────────
// Index Type Conversion
// ──────────────────────────────────────────────────────────────────
inline DXGI_FORMAT ash_to_d3d12_index_type(AshIndexType type)
{
	switch (type)
	{
	case ASH_INDEX_TYPE_UINT16: return DXGI_FORMAT_R16_UINT;
	case ASH_INDEX_TYPE_UINT32: return DXGI_FORMAT_R32_UINT;
	default:                     return DXGI_FORMAT_R32_UINT;
	}
}

// ──────────────────────────────────────────────────────────────────
// Load Option Conversion
// ──────────────────────────────────────────────────────────────────
inline D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE ash_to_d3d12_load_op(AshLoadOption op)
{
	switch (op)
	{
	case ASH_LOAD_DONT_CARE: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
	case ASH_LOAD_LOAD:      return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
	case ASH_LOAD_CLEAR:     return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
	default:                  return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
	}
}

// ──────────────────────────────────────────────────────────────────
// Resource Dimension from Image Type
// ──────────────────────────────────────────────────────────────────
inline D3D12_RESOURCE_DIMENSION ash_image_type_to_d3d12_dimension(AshImageType type)
{
	switch (type)
	{
	case Ash_Texture1D:
	case Ash_Texture_1D_Array:
		return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
	case Ash_Texture2D:
	case Ash_TextureCube:
	case Ash_Texture_2D_Array:
	case Ash_Texture_Cube_Array:
		return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	case Ash_Texture3D:
		return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	default:
		return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	}
}

// ──────────────────────────────────────────────────────────────────
// SRV Dimension from Image Type
// ──────────────────────────────────────────────────────────────────
inline D3D12_SRV_DIMENSION ash_view_dim_to_d3d12_srv_dim(AshResourceViewDimension dim)
{
	switch (dim)
	{
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:           return D3D12_SRV_DIMENSION_TEXTURE1D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:           return D3D12_SRV_DIMENSION_TEXTURE2D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D:           return D3D12_SRV_DIMENSION_TEXTURE3D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE:         return D3D12_SRV_DIMENSION_TEXTURECUBE;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:     return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:     return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE_ARRAY:   return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
	case ASH_RESOURCE_VIEW_DIMENSION_BUFFER:              return D3D12_SRV_DIMENSION_BUFFER;
	default:                                               return D3D12_SRV_DIMENSION_TEXTURE2D;
	}
}

inline D3D12_UAV_DIMENSION ash_view_dim_to_d3d12_uav_dim(AshResourceViewDimension dim)
{
	switch (dim)
	{
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:           return D3D12_UAV_DIMENSION_TEXTURE1D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:           return D3D12_UAV_DIMENSION_TEXTURE2D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D:           return D3D12_UAV_DIMENSION_TEXTURE3D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:     return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:     return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
	case ASH_RESOURCE_VIEW_DIMENSION_BUFFER:              return D3D12_UAV_DIMENSION_BUFFER;
	default:                                               return D3D12_UAV_DIMENSION_TEXTURE2D;
	}
}

inline D3D12_RTV_DIMENSION ash_view_dim_to_d3d12_rtv_dim(AshResourceViewDimension dim)
{
	switch (dim)
	{
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:           return D3D12_RTV_DIMENSION_TEXTURE1D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:           return D3D12_RTV_DIMENSION_TEXTURE2D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D:           return D3D12_RTV_DIMENSION_TEXTURE3D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:     return D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:     return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
	default:                                               return D3D12_RTV_DIMENSION_TEXTURE2D;
	}
}

inline D3D12_DSV_DIMENSION ash_view_dim_to_d3d12_dsv_dim(AshResourceViewDimension dim)
{
	switch (dim)
	{
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:           return D3D12_DSV_DIMENSION_TEXTURE1D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:           return D3D12_DSV_DIMENSION_TEXTURE2D;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:     return D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
	case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:     return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	default:                                               return D3D12_DSV_DIMENSION_TEXTURE2D;
	}
}

// ──────────────────────────────────────────────────────────────────
// Sample Count Conversion
// ──────────────────────────────────────────────────────────────────
inline UINT ash_to_d3d12_sample_count(AshSampleCountFlag flag)
{
	switch (flag)
	{
	case ASH_SAMPLE_COUNT_1_BIT:  return 1;
	case ASH_SAMPLE_COUNT_2_BIT:  return 2;
	case ASH_SAMPLE_COUNT_4_BIT:  return 4;
	case ASH_SAMPLE_COUNT_8_BIT:  return 8;
	case ASH_SAMPLE_COUNT_16_BIT: return 16;
	case ASH_SAMPLE_COUNT_32_BIT: return 32;
	case ASH_SAMPLE_COUNT_64_BIT: return 64;
	default:                       return 1;
	}
}

// ──────────────────────────────────────────────────────────────────
// D3D12 Descriptor Handle Pair
// ──────────────────────────────────────────────────────────────────
struct DX12DescriptorHandle
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
	uint32_t heapIndex = UINT32_MAX;

	bool is_valid() const { return cpuHandle.ptr != 0; }
	bool is_shader_visible() const { return gpuHandle.ptr != 0; }
};

} // namespace RHI

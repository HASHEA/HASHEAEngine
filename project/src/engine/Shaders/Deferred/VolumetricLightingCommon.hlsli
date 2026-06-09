#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct VSFullscreenOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSFullscreenOutput AshVolumetricFullscreen(uint vertex_id)
{
	float2 positions[3] = {
		float2(-1.0, -1.0),
		float2(-1.0,  3.0),
		float2( 3.0, -1.0)
	};
	float2 uvs[3] = {
		float2(0.0, 1.0),
		float2(0.0, -1.0),
		float2(2.0, 1.0)
	};

	VSFullscreenOutput output;
	output.position = float4(positions[vertex_id], 0.0, 1.0);
	output.uv = uvs[vertex_id];
	return output;
}

cbuffer AshRootConstants : register(b0)
{
	float4x4 AshInvViewProjection;
	float4x4 AshView;
	float4 AshVolumetricAtlasSize;
	float4 AshVolumetricConfig0;
	float4 AshVolumetricConfig1;
	float4 AshCameraPositionAndFlags;
	float4 AshScreenLightPositionAndParams;
	float4 AshVolumetricVolumeParams;
};

bool AshVolumetricIsReverseZ()
{
	return AshCameraPositionAndFlags.w > 0.5;
}

bool AshVolumetricSceneDepthIsBackground(float depth)
{
	return AshVolumetricIsReverseZ() ? depth <= 0.000001 : depth >= 0.999999;
}

uint AshVolumetricDepthSliceCount()
{
	return max((uint)AshVolumetricVolumeParams.x, 1u);
}

uint AshVolumetricSlicesPerRow()
{
	return max((uint)AshVolumetricVolumeParams.y, 1u);
}

float AshVolumetricAnisotropy()
{
	return clamp(AshVolumetricVolumeParams.w, -0.95, 0.95);
}

float2 AshVolumetricAtlasUV(uint2 pixel, uint slice, uint slices_per_row, float2 atlas_inv_size)
{
	uint tile_x = slice % max(slices_per_row, 1u);
	uint tile_y = slice / max(slices_per_row, 1u);
	uint2 atlas_pixel = uint2(tile_x * (uint)AshVolumetricAtlasSize.x + pixel.x, tile_y * (uint)AshVolumetricAtlasSize.y + pixel.y);
	return (float2(atlas_pixel) + 0.5) * atlas_inv_size;
}

bool AshVolumetricDecodeAtlasPixel(uint2 atlas_pixel, out uint2 tile_pixel, out uint slice)
{
	const uint2 tile_size = max((uint2)AshVolumetricAtlasSize.xy, uint2(1u, 1u));
	const uint slices_per_row = AshVolumetricSlicesPerRow();
	const uint2 tile_index = atlas_pixel / tile_size;
	tile_pixel = atlas_pixel - tile_index * tile_size;
	slice = tile_index.y * slices_per_row + tile_index.x;
	return slice < AshVolumetricDepthSliceCount();
}

float2 AshVolumetricTileUV(uint2 tile_pixel)
{
	const float2 tile_size = max(AshVolumetricAtlasSize.xy, float2(1.0, 1.0));
	return (float2(tile_pixel) + 0.5) / tile_size;
}

uint2 AshVolumetricTilePixelFromUV(float2 uv)
{
	const uint2 tile_size = max((uint2)AshVolumetricAtlasSize.xy, uint2(1u, 1u));
	return min((uint2)floor(saturate(uv) * float2(tile_size)), tile_size - 1u);
}

float AshVolumetricSliceDepth01(uint slice)
{
	return (float(slice) + 0.5) / max((float)AshVolumetricDepthSliceCount(), 1.0);
}

float AshVolumetricMaxViewDepth()
{
	return max(AshVolumetricVolumeParams.z, 0.01);
}

float AshVolumetricSliceViewDepth(uint slice)
{
	return max(AshVolumetricSliceDepth01(slice) * AshVolumetricMaxViewDepth(), 0.01);
}

float AshVolumetricDeviceDepthFromDepth01(float depth01)
{
	const float clamped_depth = saturate(depth01);
	return AshVolumetricIsReverseZ() ? 1.0 - clamped_depth : clamped_depth;
}

float3 AshVolumetricReconstructWorldPosition(float2 uv, float device_depth)
{
	const float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), device_depth, 1.0);
	const float4 world = mul(AshInvViewProjection, clip);
	return world.xyz / max(world.w, 1e-6);
}

float AshVolumetricViewDepthFromWorldPosition(float3 position_ws)
{
	return abs(mul(AshView, float4(position_ws, 1.0)).z);
}

float3 AshVolumetricReconstructWorldPositionAtViewDepth(float2 uv, float view_depth)
{
	const float far_device_depth = AshVolumetricIsReverseZ() ? 0.0 : 1.0;
	const float3 far_position_ws = AshVolumetricReconstructWorldPosition(uv, far_device_depth);
	const float far_view_depth = max(AshVolumetricViewDepthFromWorldPosition(far_position_ws), 1e-4);
	return lerp(AshCameraPositionAndFlags.xyz, far_position_ws, saturate(view_depth / far_view_depth));
}

float AshVolumetricVisibleDepth01(float2 uv, float scene_depth)
{
	if (AshVolumetricSceneDepthIsBackground(scene_depth))
	{
		return 1.0;
	}
	const float3 position_ws = AshVolumetricReconstructWorldPosition(uv, scene_depth);
	return saturate(AshVolumetricViewDepthFromWorldPosition(position_ws) / AshVolumetricMaxViewDepth());
}

float3 AshVolumetricSafeNormalize(float3 value, float3 fallback)
{
	const float length_sq = dot(value, value);
	if (length_sq <= 1e-8)
	{
		return fallback;
	}
	return value * rsqrt(length_sq);
}

float AshVolumetricPhaseHG(float cos_theta, float g)
{
	float g2 = g * g;
	float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4);
	return (1.0 - g2) / max(12.56637061 * denom * sqrt(denom), 1e-4);
}

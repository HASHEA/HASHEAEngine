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
	float4x4 AshPrevViewProjection;
	float4 AshVolumetricAtlasSize;
	float4 AshVolumetricConfig0;
	float4 AshVolumetricConfig1;
	float4 AshCameraPositionAndFlags;
	float4 AshScreenLightPositionAndParams;
};

float2 AshVolumetricAtlasUV(uint2 pixel, uint slice, uint slices_per_row, float2 atlas_inv_size)
{
	uint tile_x = slice % max(slices_per_row, 1u);
	uint tile_y = slice / max(slices_per_row, 1u);
	uint2 atlas_pixel = uint2(tile_x * (uint)AshVolumetricAtlasSize.x + pixel.x, tile_y * (uint)AshVolumetricAtlasSize.y + pixel.y);
	return (float2(atlas_pixel) + 0.5) * atlas_inv_size;
}

float AshVolumetricPhaseHG(float cos_theta, float g)
{
	float g2 = g * g;
	float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4);
	return (1.0 - g2) / max(12.56637061 * denom * sqrt(denom), 1e-4);
}

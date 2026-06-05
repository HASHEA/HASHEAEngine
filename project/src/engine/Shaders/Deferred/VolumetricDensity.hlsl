#include "VolumetricLightingCommon.hlsli"

RWTexture2D<float4> SceneVolumetricDensity : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.z;
	uint height = (uint)AshVolumetricAtlasSize.w;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	uint2 tile_pixel = uint2(0u, 0u);
	uint slice = 0u;
	if (!AshVolumetricDecodeAtlasPixel(dispatch_id.xy, tile_pixel, slice))
	{
		SceneVolumetricDensity[dispatch_id.xy] = float4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	float density = max(AshVolumetricConfig0.x, 0.0);
	float extinction = density * max(AshVolumetricConfig0.y, 0.0);
	SceneVolumetricDensity[dispatch_id.xy] = float4(density, extinction, AshVolumetricSliceDepth01(slice), 1.0);
}

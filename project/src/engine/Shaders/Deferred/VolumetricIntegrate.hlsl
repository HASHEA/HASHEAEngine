#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricScatteringTemporal : register(t0);
Texture2D<float> SceneDepth : register(t1);
RWTexture2D<float4> SceneVolumetricIntegratedLighting : register(u0);
SamplerState SceneLinearClampSampler : register(s0);
SamplerState ScenePointClampSampler : register(s1);

static const float kVolumetricScatteringWorldScale = 0.0125;
static const float kVolumetricExtinctionWorldScale = 0.0125;

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = max((uint)AshVolumetricConfig1.z, 1u);
	uint height = max((uint)AshVolumetricConfig1.w, 1u);
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), float2(1.0, 1.0));
	float scene_depth = SceneDepth.SampleLevel(ScenePointClampSampler, uv, 0);
	float visible_depth = AshVolumetricVisibleDepth01(uv, scene_depth);
	uint2 tile_pixel = AshVolumetricTilePixelFromUV(uv);
	uint depth_slices = AshVolumetricDepthSliceCount();
	uint slices_per_row = AshVolumetricSlicesPerRow();
	float2 atlas_inv_size = 1.0 / max(AshVolumetricAtlasSize.zw, float2(1.0, 1.0));

	float3 lighting = 0.0.xxx;
	float transmittance = 1.0;
	for (uint slice = 0u; slice < 128u && slice < depth_slices; ++slice)
	{
		float slice_start = (float)slice / max((float)depth_slices, 1.0);
		float slice_end = (float)(slice + 1u) / max((float)depth_slices, 1.0);
		float segment_length = max(min(slice_end, visible_depth) - slice_start, 0.0);
		if (segment_length <= 0.0)
		{
			break;
		}

		float2 atlas_uv = AshVolumetricAtlasUV(tile_pixel, slice, slices_per_row, atlas_inv_size);
		float4 scattering = SceneVolumetricScatteringTemporal.SampleLevel(SceneLinearClampSampler, atlas_uv, 0);
		float segment_view_length = segment_length * AshVolumetricMaxViewDepth();
		lighting += scattering.rgb * transmittance * segment_view_length * kVolumetricScatteringWorldScale;
		transmittance *= exp(-max(scattering.a, 0.0) * segment_view_length * kVolumetricExtinctionWorldScale);
	}

	SceneVolumetricIntegratedLighting[dispatch_id.xy] = float4(lighting, transmittance);
}

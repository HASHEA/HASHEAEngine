#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricDensity : register(t0);

struct VolumetricLightData
{
	float4 position_range;
	float4 direction_type;
	float4 color_intensity;
	float4 cone_shadow;
};

StructuredBuffer<VolumetricLightData> SceneVolumetricLights : register(t1);
RWTexture2D<float4> SceneVolumetricScattering : register(u0);
SamplerState ScenePointClampSampler : register(s0);

float AshVolumetricRangeAttenuation(float distance, float range)
{
	const float distance_ratio = saturate(distance / max(range, 1e-4));
	const float smooth_range = saturate(1.0 - distance_ratio * distance_ratio);
	return smooth_range * smooth_range / max(distance * distance, 0.01);
}

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
		SceneVolumetricScattering[dispatch_id.xy] = float4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	float2 atlas_uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), float2(1.0, 1.0));
	float4 density_sample = SceneVolumetricDensity.SampleLevel(ScenePointClampSampler, atlas_uv, 0);
	float density = density_sample.r;
	float extinction = density_sample.g;
	if (density <= 0.0 || AshVolumetricConfig0.z <= 0.0)
	{
		SceneVolumetricScattering[dispatch_id.xy] = float4(0.0, 0.0, 0.0, extinction);
		return;
	}

	float2 tile_uv = AshVolumetricTileUV(tile_pixel);
	float slice_depth = AshVolumetricSliceDepth01(slice);
	float3 position_ws = AshVolumetricReconstructWorldPosition(tile_uv, AshVolumetricDeviceDepthFromDepth01(slice_depth));
	float3 view_dir = AshVolumetricSafeNormalize(position_ws - AshCameraPositionAndFlags.xyz, float3(0.0, 0.0, 1.0));
	uint light_count = (uint)AshVolumetricConfig1.x;
	float3 scattering = 0.0.xxx;
	for (uint light_index = 0u; light_index < min(light_count, 256u); ++light_index)
	{
		VolumetricLightData light = SceneVolumetricLights[light_index];
		float type = light.direction_type.w;
		float3 to_light = float3(0.0, 1.0, 0.0);
		float attenuation = 0.0;

		if (type < 0.5)
		{
			to_light = AshVolumetricSafeNormalize(-light.direction_type.xyz, float3(0.0, 1.0, 0.0));
			attenuation = 1.0;
		}
		else if (type < 1.5)
		{
			float3 light_vector = light.position_range.xyz - position_ws;
			float distance = length(light_vector);
			if (distance <= light.position_range.w)
			{
				to_light = light_vector / max(distance, 1e-4);
				attenuation = AshVolumetricRangeAttenuation(distance, light.position_range.w);
			}
		}
		else
		{
			float3 from_light = position_ws - light.position_range.xyz;
			float distance = length(from_light);
			if (distance <= light.position_range.w)
			{
				float3 light_forward = AshVolumetricSafeNormalize(light.direction_type.xyz, float3(0.0, -1.0, 0.0));
				float3 light_to_sample = from_light / max(distance, 1e-4);
				float cos_angle = dot(light_forward, light_to_sample);
				float cone = saturate((cos_angle - light.cone_shadow.y) / max(light.cone_shadow.x - light.cone_shadow.y, 1e-4));
				to_light = -light_to_sample;
				attenuation = AshVolumetricRangeAttenuation(distance, light.position_range.w) * cone * cone;
			}
		}

		float phase = AshVolumetricPhaseHG(dot(to_light, -view_dir), AshVolumetricAnisotropy());
		scattering += light.color_intensity.rgb * light.color_intensity.w * attenuation * phase;
	}
	scattering *= density * max(AshVolumetricConfig0.z, 0.0);
	SceneVolumetricScattering[dispatch_id.xy] = float4(scattering, extinction);
}

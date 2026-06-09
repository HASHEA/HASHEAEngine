#include "VolumetricLightingCommon.hlsli"
#define ASH_DIRECTIONAL_SHADOW_COMMON_NO_FULLSCREEN
#include "../Shadow/DirectionalShadowCommon.hlsli"

#define AshVolumetricSunShadowParams AshScreenLightPositionAndParams

static const float kVolumetricCascadeTransitionRatio = 0.08;
static const float kVolumetricScatteringDensityNormalization = 12.5;
static const float kVolumetricDirectionalVisibilityScale = 2.0;
static const float kVolumetricLocalVisibilityScale = 4.0;
static const float kVolumetricLocalSoftRangeFloor = 0.15;

Texture2D<float4> SceneVolumetricDensity : register(t0);
Texture2D<float> DirectionalShadowDynamicAtlas : register(t2);

struct VolumetricLightData
{
	float4 position_range;
	float4 direction_type;
	float4 color_intensity;
	float4 cone_shadow;
};

StructuredBuffer<VolumetricLightData> SceneVolumetricLights : register(t1);
StructuredBuffer<DirectionalShadowCascadeShaderData> SceneDirectionalShadowCascades : register(t3);
RWTexture2D<float4> SceneVolumetricScattering : register(u0);
SamplerState ScenePointClampSampler : register(s0);

float AshVolumetricRangeAttenuation(float distance, float range)
{
	const float distance_ratio = saturate(distance / max(range, 1e-4));
	const float smooth_range = saturate(1.0 - distance_ratio * distance_ratio);
	const float range_window = smooth_range * smooth_range;
	const float inverse_square = range_window / max(distance * distance, 1.0);
	const float soft_range_visibility = range_window * kVolumetricLocalSoftRangeFloor;
	return max(inverse_square, soft_range_visibility);
}

float AshVolumetricSampleSunCascade(uint cascade_buffer_index, float3 position_ws)
{
	DirectionalShadowCascadeShaderData cascade = SceneDirectionalShadowCascades[cascade_buffer_index];
	const float4 shadow_clip = mul(cascade.world_to_shadow_clip, float4(position_ws, 1.0));
	const float3 shadow_ndc = shadow_clip.xyz / max(shadow_clip.w, 1e-6);
	float2 tile_uv = shadow_ndc.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	if (tile_uv.x < 0.0 || tile_uv.y < 0.0 || tile_uv.x > 1.0 || tile_uv.y > 1.0 || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0)
	{
		return -1.0;
	}

	const float2 atlas_uv = tile_uv * cascade.atlas_uv_scale_bias.xy + cascade.atlas_uv_scale_bias.zw;
	const int radius = (int)round(AshVolumetricSunShadowParams.w);
	float lit = 0.0;
	float count = 0.0;
	for (int y = -radius; y <= radius; ++y)
	{
		for (int x = -radius; x <= radius; ++x)
		{
			const float2 sample_uv = atlas_uv + float2((float)x, (float)y) * cascade.texel_size_flags.xy;
			const float shadow_depth = DirectionalShadowDynamicAtlas.SampleLevel(ScenePointClampSampler, sample_uv, 0);
			lit += (shadow_ndc.z - cascade.split_depth_bias.z) <= shadow_depth ? 1.0 : 0.0;
			count += 1.0;
		}
	}
	return lit / max(count, 1.0);
}

float ComputeCascadeTransitionWeight(float view_depth, DirectionalShadowCascadeShaderData cascade)
{
	const float cascade_range = max(cascade.split_depth_bias.y - cascade.split_depth_bias.x, 0.0001);
	const float transition_width = max(cascade_range * kVolumetricCascadeTransitionRatio, 0.0001);
	const float transition_start = cascade.split_depth_bias.y - transition_width;
	const float transition_t = saturate((view_depth - transition_start) / transition_width);
	return smoothstep(0.0, 1.0, transition_t);
}

float AshVolumetricSampleSunlightShadow(float3 position_ws, float view_depth)
{
	if (AshVolumetricSunShadowParams.z <= 0.5)
	{
		return 1.0;
	}

	const uint first_cascade = (uint)round(AshVolumetricSunShadowParams.x);
	const uint cascade_count = min((uint)round(AshVolumetricSunShadowParams.y), 8u);
	for (uint cascade_index = 0u; cascade_index < cascade_count; ++cascade_index)
	{
		const uint buffer_index = first_cascade + cascade_index;
		DirectionalShadowCascadeShaderData cascade = SceneDirectionalShadowCascades[buffer_index];
		if (view_depth >= cascade.split_depth_bias.x && view_depth <= cascade.split_depth_bias.y)
		{
			float sunlight_shadow = AshVolumetricSampleSunCascade(buffer_index, position_ws);
			if (sunlight_shadow < 0.0)
			{
				sunlight_shadow = 1.0;
			}
			if (cascade_index + 1u < cascade_count)
			{
				const float transition_weight = ComputeCascadeTransitionWeight(view_depth, cascade);
				if (transition_weight > 0.0)
				{
					const float next_shadow = AshVolumetricSampleSunCascade(buffer_index + 1u, position_ws);
					if (next_shadow >= 0.0)
					{
						sunlight_shadow = lerp(sunlight_shadow, next_shadow, transition_weight);
					}
				}
			}
			return sunlight_shadow;
		}
	}
	return 1.0;
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
	float view_depth = AshVolumetricSliceViewDepth(slice);
	float3 position_ws = AshVolumetricReconstructWorldPositionAtViewDepth(tile_uv, view_depth);
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
			if (light.cone_shadow.z > 0.5 && light.cone_shadow.w > 0.5)
			{
				const float sunlight_shadow = AshVolumetricSampleSunlightShadow(position_ws, view_depth);
				attenuation *= sunlight_shadow;
			}
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
		const float visibility_scale = type < 0.5 ? kVolumetricDirectionalVisibilityScale : kVolumetricLocalVisibilityScale;
		scattering += light.color_intensity.rgb * light.color_intensity.w * attenuation * phase * visibility_scale;
	}
	const float scattering_density_visibility = saturate(density * kVolumetricScatteringDensityNormalization);
	scattering *= scattering_density_visibility * max(AshVolumetricConfig0.z, 0.0);
	SceneVolumetricScattering[dispatch_id.xy] = float4(scattering, extinction);
}

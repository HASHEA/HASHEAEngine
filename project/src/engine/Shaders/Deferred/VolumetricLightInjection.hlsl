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

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.z;
	uint height = (uint)AshVolumetricAtlasSize.w;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), float2(1.0, 1.0));
	float density = SceneVolumetricDensity.SampleLevel(ScenePointClampSampler, uv, 0).r;
	uint light_count = (uint)AshVolumetricConfig1.x;
	float3 scattering = 0.0.xxx;
	for (uint light_index = 0u; light_index < min(light_count, 256u); ++light_index)
	{
		VolumetricLightData light = SceneVolumetricLights[light_index];
		float type = light.direction_type.w;
		float attenuation = type == 0.0 ? 1.0 : saturate(light.position_range.w / max(light.position_range.w + 1.0, 1.0));
		scattering += light.color_intensity.rgb * light.color_intensity.w * attenuation;
	}
	scattering *= density * AshVolumetricConfig0.z / max((float)max(light_count, 1u), 1.0);
	SceneVolumetricScattering[dispatch_id.xy] = float4(scattering, density);
}

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
	float light_count = AshVolumetricConfig1.x;
	float3 scattering = density * AshVolumetricConfig0.z;
	scattering *= saturate(light_count / max(AshVolumetricConfig0.w, 1.0));
	SceneVolumetricScattering[dispatch_id.xy] = float4(scattering, density);
}

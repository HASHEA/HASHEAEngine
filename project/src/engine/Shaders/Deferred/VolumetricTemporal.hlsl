#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricScattering : register(t0);
Texture2D<float4> SceneVolumetricScatteringHistory : register(t1);
RWTexture2D<float4> SceneVolumetricScatteringTemporal : register(u0);
RWTexture2D<float4> SceneVolumetricHistoryValidity : register(u1);
SamplerState SceneLinearClampSampler : register(s0);

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
	float4 current_value = SceneVolumetricScattering.SampleLevel(SceneLinearClampSampler, uv, 0);
	float4 history_value = SceneVolumetricScatteringHistory.SampleLevel(SceneLinearClampSampler, uv, 0);
	float blend = saturate(AshVolumetricConfig1.y);
	float4 filtered = lerp(current_value, history_value, blend);
	SceneVolumetricScatteringTemporal[dispatch_id.xy] = filtered;
	SceneVolumetricHistoryValidity[dispatch_id.xy] = float4(blend.xxx, 1.0);
}

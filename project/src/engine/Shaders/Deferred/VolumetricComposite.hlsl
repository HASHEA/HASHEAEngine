#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
Texture2D<float4> SceneVolumetricIntegratedLighting : register(t1);
SamplerState SceneLinearClampSampler : register(s0);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshVolumetricFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float3 hdr = SceneHDRLinear.Sample(SceneLinearClampSampler, input.uv).rgb;
	float3 volumetric = SceneVolumetricIntegratedLighting.Sample(SceneLinearClampSampler, input.uv).rgb;
	return float4(hdr + volumetric, 1.0);
}

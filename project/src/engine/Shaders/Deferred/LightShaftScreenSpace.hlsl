#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
Texture2D<float> SceneDepth : register(t1);
SamplerState SceneLinearClampSampler : register(s0);
SamplerState ScenePointClampSampler : register(s1);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshVolumetricFullscreen(vertex_id);
}

struct PSScreenSpaceLightShaftOutput
{
	float4 screen_space_mask : SV_Target0;
	float4 screen_space_final : SV_Target1;
};

PSScreenSpaceLightShaftOutput PSMain(VSFullscreenOutput input)
{
	float3 hdr = SceneHDRLinear.Sample(SceneLinearClampSampler, input.uv).rgb;
	float2 light_uv = AshScreenLightPositionAndParams.xy;
	float2 delta = light_uv - input.uv;
	float shaft = 0.0;
	float weight = 1.0;
	for (uint index = 0; index < 16u; ++index)
	{
		float t = (float(index) + 0.5) / 16.0;
		float2 uv = saturate(input.uv + delta * t);
		float depth = SceneDepth.Sample(ScenePointClampSampler, uv);
		float visible = AshVolumetricSceneDepthIsBackground(depth) ? 1.0 : 0.0;
		shaft += visible * weight;
		weight *= 0.92;
	}
	shaft = shaft / 16.0 * AshScreenLightPositionAndParams.z;

	PSScreenSpaceLightShaftOutput output;
	output.screen_space_mask = float4(shaft.xxx, 1.0);
	output.screen_space_final = float4(hdr + shaft.xxx, 1.0);
	return output;
}

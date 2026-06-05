#include "BloomCommon.hlsli"

Texture2D<float4> BloomLowInput : register(t0);
Texture2D<float4> BloomHighInput : register(t1);
SamplerState SceneLinearClampSampler : register(s0);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshBloomFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float2 texel = AshBloomSourceSize.zw * max(AshBloomStageTintRadius.w, 0.0);
	float3 low = 0.0;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(-1.0, 0.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2( 1.0, 0.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(0.0, -1.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(0.0,  1.0)).rgb;
	low += BloomLowInput.Sample(SceneLinearClampSampler, input.uv).rgb * 4.0;
	low *= 0.125;

	float3 high = BloomHighInput.Sample(SceneLinearClampSampler, input.uv).rgb;
	float3 tint = AshBloomStageTintRadius.rgb;
	return float4(AshBloomPositive(high + low * tint), 1.0);
}

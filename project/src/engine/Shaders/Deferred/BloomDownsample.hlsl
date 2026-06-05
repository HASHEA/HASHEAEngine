#include "BloomCommon.hlsli"

Texture2D<float4> BloomInput : register(t0);
SamplerState SceneLinearClampSampler : register(s0);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshBloomFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float2 texel = AshBloomSourceSize.zw;
	float3 color = 0.0;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(-1.0, -1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2( 1.0, -1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2(-1.0,  1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv + texel * float2( 1.0,  1.0)).rgb;
	color += BloomInput.Sample(SceneLinearClampSampler, input.uv).rgb * 4.0;
	color *= 0.125;
	return float4(AshBloomPositive(color), 1.0);
}

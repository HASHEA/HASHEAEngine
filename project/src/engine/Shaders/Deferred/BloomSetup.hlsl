#include "BloomCommon.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
SamplerState SceneLinearClampSampler : register(s0);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return AshBloomFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	float3 color = AshBloomPositive(SceneHDRLinear.Sample(SceneLinearClampSampler, input.uv).rgb);
	float threshold = AshBloomThresholdSoftKnee.x;
	float soft_knee = AshBloomThresholdSoftKnee.y;

	if (threshold < 0.0)
	{
		return float4(color, 1.0);
	}

	float luminance = AshBloomLuminance(color);
	float knee = max(threshold * soft_knee, 1e-5);
	float soft = saturate((luminance - threshold + knee) / (2.0 * knee));
	soft = soft * soft * knee;
	float contribution = max(luminance - threshold, soft);
	float weight = contribution / max(luminance, 1e-5);
	return float4(color * saturate(weight), 1.0);
}

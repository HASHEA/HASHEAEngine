#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricScatteringTemporal : register(t0);
RWTexture2D<float4> SceneVolumetricIntegratedLighting : register(u0);
SamplerState SceneLinearClampSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.x;
	uint height = (uint)AshVolumetricAtlasSize.y;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), float2(1.0, 1.0));
	float3 lighting = SceneVolumetricScatteringTemporal.SampleLevel(SceneLinearClampSampler, uv, 0).rgb;
	SceneVolumetricIntegratedLighting[dispatch_id.xy] = float4(lighting, 1.0);
}

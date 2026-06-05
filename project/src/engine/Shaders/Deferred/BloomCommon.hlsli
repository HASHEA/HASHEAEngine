#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct VSFullscreenOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSFullscreenOutput AshBloomFullscreen(uint vertex_id)
{
	float2 positions[3] = {
		float2(-1.0, -1.0),
		float2(-1.0,  3.0),
		float2( 3.0, -1.0)
	};
	float2 uvs[3] = {
		float2(0.0, 1.0),
		float2(0.0, -1.0),
		float2(2.0, 1.0)
	};

	VSFullscreenOutput output;
	output.position = float4(positions[vertex_id], 0.0, 1.0);
	output.uv = uvs[vertex_id];
	return output;
}

cbuffer AshRootConstants : register(b0)
{
	float4 AshBloomSourceSize;
	float4 AshBloomTargetSize;
	float4 AshBloomThresholdSoftKnee;
	float4 AshBloomStageTintRadius;
	float4 AshBloomCompositeParams;
};

float AshBloomLuminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 AshBloomPositive(float3 color)
{
	return max(color, 0.0);
}

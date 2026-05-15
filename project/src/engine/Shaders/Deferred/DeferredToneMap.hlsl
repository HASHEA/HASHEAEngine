#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

Texture2D<float4> SceneHDRLinear : register(t0);
SamplerState ScenePointClampSampler : register(s0);

struct VSFullscreenOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSFullscreenOutput VSFullscreen(uint vertex_id : SV_VertexID)
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
	float4x4 AshInvViewProjection;
	float4x4 AshLightWorldToClip;
	float4 AshViewportSize;
	float4 AshCameraPositionAndFlags;
	float4 AshLightPositionAndRange;
	float4 AshLightDirectionAndIntensity;
	float4 AshLightColorAndType;
	float4 AshLightConeCos;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
	return VSFullscreen(vertex_id);
}

float3 AshACESFilm(float3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((x * (a * x + b)) / max(x * (c * x + d) + e, 1e-6));
}

float3 AshLinearToSRGB(float3 lin)
{
	lin = max(lin, 0.0);
	return lerp(
		1.055 * pow(lin, 1.0 / 2.4) - 0.055,
		12.92 * lin,
		step(lin, 0.0031308));
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
	const float exposure = AshCameraPositionAndFlags.w;
	const float manual_srgb = AshLightConeCos.z;

	float3 hdr = SceneHDRLinear.Sample(ScenePointClampSampler, input.uv).rgb;
	hdr *= exposure;
	float3 mapped = AshACESFilm(hdr);

	if (manual_srgb > 0.5)
		mapped = AshLinearToSRGB(mapped);

	return float4(mapped, 1.0);
}

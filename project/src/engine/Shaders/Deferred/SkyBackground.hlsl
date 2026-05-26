#include "EnvironmentCommon.hlsli"

Texture2D<float> SceneDepth : register(t5);
Texture2D<float4> SceneHDRLinear : register(t6);
TextureCube<float4> SceneEnvironmentRadiance : register(t9);
SamplerState ScenePointClampSampler : register(s0);
SamplerState SceneEnvironmentSampler : register(s1);

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const float depth = SceneDepth.Sample(ScenePointClampSampler, input.uv);
    const float4 scene_hdr = SceneHDRLinear.Sample(ScenePointClampSampler, input.uv);
    if (!AshSceneDepthIsBackground(depth))
    {
        return scene_hdr;
    }

    const float far_depth = AshIsReverseZ() ? 0.0 : 1.0;
    const float4 clip = float4(input.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), far_depth, 1.0);
    const float4 world = mul(AshInvViewProjection, clip);
    float3 ray_dir = normalize(world.xyz / max(world.w, 1e-6) - AshCameraPositionAndFlags.xyz);
    ray_dir = AshRotateEnvironmentDirection(ray_dir, AshEnvironmentRotationRadians());
    const float3 sky_radiance =
        SceneEnvironmentRadiance.Sample(SceneEnvironmentSampler, ray_dir).rgb * AshEnvironmentIntensity();
    return float4(sky_radiance, 1.0);
}

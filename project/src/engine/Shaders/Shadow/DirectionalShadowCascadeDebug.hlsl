#include "DirectionalShadowCommon.hlsli"

Texture2D<float> SceneDepth : register(t0);
StructuredBuffer<DirectionalShadowCascadeShaderData> SceneDirectionalShadowCascades : register(t2);
SamplerState ScenePointClampSampler : register(s0);

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshInvViewProjection;
    float4x4 AshView;
    float4 AshViewportSize;
    float4 AshShadowLightParams;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    const float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
    const float4 world = mul(AshInvViewProjection, clip);
    return world.xyz / max(world.w, 1e-6);
}

bool IsBackgroundDepth(float depth)
{
    return AshShadowLightParams.z > 0.5 ? depth <= 0.000001 : depth >= 0.999999;
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const float scene_depth = SceneDepth.SampleLevel(ScenePointClampSampler, input.uv, 0);
    if (IsBackgroundDepth(scene_depth))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float3 position_ws = ReconstructWorldPosition(input.uv, scene_depth);
    const float view_depth = abs(mul(AshView, float4(position_ws, 1.0)).z);
    const uint first_cascade = (uint)round(AshShadowLightParams.x);
    const uint cascade_count = max((uint)round(AshShadowLightParams.y), 1u);
    for (uint cascade_index = 0; cascade_index < cascade_count; ++cascade_index)
    {
        const uint buffer_index = first_cascade + cascade_index;
        DirectionalShadowCascadeShaderData cascade = SceneDirectionalShadowCascades[buffer_index];
        if (view_depth >= cascade.split_depth_bias.x && view_depth <= cascade.split_depth_bias.y)
        {
            const float normalized = cascade_count > 1u ?
                (float)cascade_index / (float)(cascade_count - 1u) :
                0.0;
            const float cache_mode = cascade.texel_size_flags.w / 3.0;
            return float4(normalized, cache_mode, 0.0, 1.0);
        }
    }
    return float4(1.0, 0.0, 0.0, 1.0);
}

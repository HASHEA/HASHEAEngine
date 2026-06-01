#include "DirectionalShadowCommon.hlsli"

Texture2D<float> SceneDepth : register(t0);
Texture2D<float> DirectionalShadowDynamicAtlas : register(t1);
Texture2D<float4> SceneGBufferE : register(t3);
StructuredBuffer<DirectionalShadowCascadeShaderData> SceneDirectionalShadowCascades : register(t2);
SamplerState ScenePointClampSampler : register(s0);

static const float kCascadeTransitionRatio = 0.08;

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshInvViewProjection;
    float4x4 AshView;
    float4 AshViewportSize;
    float4 AshShadowLightParams;
    float4 AshShadowLightDirection;
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

float3 ApplyNormalBias(float3 position_ws, float3 normal_ws, float normal_bias)
{
    return position_ws + normal_ws * normal_bias;
}

float SampleCascadeShadow(uint cascade_buffer_index, float3 position_ws, float3 normal_ws)
{
    DirectionalShadowCascadeShaderData cascade = SceneDirectionalShadowCascades[cascade_buffer_index];
    position_ws = ApplyNormalBias(position_ws, normal_ws, cascade.split_depth_bias.w);

    const float4 shadow_clip = mul(cascade.world_to_shadow_clip, float4(position_ws, 1.0));
    const float3 shadow_ndc = shadow_clip.xyz / max(shadow_clip.w, 1e-6);
    float2 tile_uv = shadow_ndc.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    if (tile_uv.x < 0.0 || tile_uv.y < 0.0 || tile_uv.x > 1.0 || tile_uv.y > 1.0 || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0)
    {
        return 1.0;
    }

    const float2 atlas_uv = tile_uv * cascade.atlas_uv_scale_bias.xy + cascade.atlas_uv_scale_bias.zw;
    const int radius = (int)round(AshShadowLightParams.w);
    float lit = 0.0;
    float count = 0.0;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float2 sample_uv = atlas_uv + float2((float)x, (float)y) * cascade.texel_size_flags.xy;
            const float shadow_depth = DirectionalShadowDynamicAtlas.SampleLevel(ScenePointClampSampler, sample_uv, 0);
            lit += (shadow_ndc.z - cascade.split_depth_bias.z) <= shadow_depth ? 1.0 : 0.0;
            count += 1.0;
        }
    }
    return lit / max(count, 1.0);
}

float ComputeCascadeTransitionWeight(float view_depth, DirectionalShadowCascadeShaderData cascade)
{
    const float cascade_range = max(cascade.split_depth_bias.y - cascade.split_depth_bias.x, 0.0001);
    const float transition_width = max(cascade_range * kCascadeTransitionRatio, 0.0001);
    const float transition_start = cascade.split_depth_bias.y - transition_width;
    const float transition_t = saturate((view_depth - transition_start) / transition_width);
    return smoothstep(0.0, 1.0, transition_t);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const float scene_depth = SceneDepth.SampleLevel(ScenePointClampSampler, input.uv, 0);
    if (IsBackgroundDepth(scene_depth))
    {
        return 1.0.xxxx;
    }

    const float3 position_ws = ReconstructWorldPosition(input.uv, scene_depth);
    const float3 normal_ws = AshDecodeNormalOct(SceneGBufferE.SampleLevel(ScenePointClampSampler, input.uv, 0).rg);
    const float view_depth = abs(mul(AshView, float4(position_ws, 1.0)).z);
    const uint first_cascade = (uint)round(AshShadowLightParams.x);
    const uint cascade_count = (uint)round(AshShadowLightParams.y);
    for (uint cascade_index = 0; cascade_index < cascade_count; ++cascade_index)
    {
        const uint buffer_index = first_cascade + cascade_index;
        DirectionalShadowCascadeShaderData cascade = SceneDirectionalShadowCascades[buffer_index];
        if (view_depth >= cascade.split_depth_bias.x && view_depth <= cascade.split_depth_bias.y)
        {
            float shadow = SampleCascadeShadow(buffer_index, position_ws, normal_ws);
            if (cascade_index + 1u < cascade_count)
            {
                const float transition_weight = ComputeCascadeTransitionWeight(view_depth, cascade);
                if (transition_weight > 0.0)
                {
                    const uint next_buffer_index = buffer_index + 1u;
                    const float next_shadow = SampleCascadeShadow(next_buffer_index, position_ws, normal_ws);
                    shadow = lerp(shadow, next_shadow, transition_weight);
                }
            }
            return float4(shadow, shadow, shadow, 1.0);
        }
    }
    return 1.0.xxxx;
}

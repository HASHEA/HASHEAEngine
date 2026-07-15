#include "DirectionalShadowCommon.hlsli"

Texture2D<float> SceneDepth : register(t0);
Texture2D<float> DirectionalShadowDynamicAtlas : register(t1);
Texture2D<float4> SceneGBufferE : register(t3);
StructuredBuffer<DirectionalShadowCascadeShaderData> SceneDirectionalShadowCascades : register(t2);
SamplerState ScenePointClampSampler : register(s0);

static const float kCascadeTransitionRatio = 0.08;
static const float kReceiverPlaneJacobianRelativeEpsilon = 1e-8;
static const float kReceiverPlaneMaximumDepthOffset = 0.05;

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

bool TryComputeReceiverPlaneDepthGradient(
    DirectionalShadowCascadeShaderData cascade,
    float4 shadow_clip,
    float3 position_dx_ws,
    float3 position_dy_ws,
    out float2 depth_gradient)
{
    depth_gradient = 0.0.xx;
    const float shadow_clip_w_squared = shadow_clip.w * shadow_clip.w;
    if (!isfinite(shadow_clip_w_squared) ||
        shadow_clip_w_squared <= 1e-12)
    {
        return false;
    }

    const float4 shadow_clip_dx = mul(
        cascade.world_to_shadow_clip,
        float4(position_dx_ws, 0.0));
    const float4 shadow_clip_dy = mul(
        cascade.world_to_shadow_clip,
        float4(position_dy_ws, 0.0));
    const float3 shadow_ndc_dx =
        (shadow_clip_dx.xyz * shadow_clip.w -
            shadow_clip.xyz * shadow_clip_dx.w) /
        shadow_clip_w_squared;
    const float3 shadow_ndc_dy =
        (shadow_clip_dy.xyz * shadow_clip.w -
            shadow_clip.xyz * shadow_clip_dy.w) /
        shadow_clip_w_squared;
    const float2 atlas_uv_dx = shadow_ndc_dx.xy *
        float2(0.5, -0.5) * cascade.atlas_uv_scale_bias.xy;
    const float2 atlas_uv_dy = shadow_ndc_dy.xy *
        float2(0.5, -0.5) * cascade.atlas_uv_scale_bias.xy;
    const float depth_dx = shadow_ndc_dx.z;
    const float depth_dy = shadow_ndc_dy.z;
    const float atlas_uv_dx_length_squared = dot(atlas_uv_dx, atlas_uv_dx);
    const float atlas_uv_dy_length_squared = dot(atlas_uv_dy, atlas_uv_dy);
    const float jacobian_determinant =
        atlas_uv_dx.x * atlas_uv_dy.y - atlas_uv_dx.y * atlas_uv_dy.x;
    const float jacobian_scale_squared =
        atlas_uv_dx_length_squared * atlas_uv_dy_length_squared;
    if (!isfinite(depth_dx) || !isfinite(depth_dy) ||
        !isfinite(atlas_uv_dx_length_squared) ||
        !isfinite(atlas_uv_dy_length_squared) ||
        !isfinite(jacobian_determinant) ||
        !isfinite(jacobian_scale_squared) ||
        atlas_uv_dx_length_squared <= 0.0 ||
        atlas_uv_dy_length_squared <= 0.0 ||
        jacobian_scale_squared <= 0.0 ||
        jacobian_determinant * jacobian_determinant <=
            kReceiverPlaneJacobianRelativeEpsilon * jacobian_scale_squared)
    {
        return false;
    }

    depth_gradient = float2(
        depth_dx * atlas_uv_dy.y - depth_dy * atlas_uv_dx.y,
        depth_dy * atlas_uv_dx.x - depth_dx * atlas_uv_dy.x) *
        rcp(jacobian_determinant);
    return isfinite(depth_gradient.x) && isfinite(depth_gradient.y);
}

float ReceiverPlaneDepthOffsetOrZero(
    float2 depth_gradient,
    float2 tap_offset,
    bool has_depth_gradient)
{
    if (!has_depth_gradient)
    {
        return 0.0;
    }
    const float depth_offset = dot(depth_gradient, tap_offset);
    return isfinite(depth_offset) &&
        abs(depth_offset) <= kReceiverPlaneMaximumDepthOffset ?
        depth_offset : 0.0;
}

float SampleCascadeShadow(
    uint cascade_buffer_index,
    float3 position_ws,
    float3 normal_ws,
    float3 position_dx_ws,
    float3 position_dy_ws)
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
    float2 receiver_plane_depth_gradient = 0.0.xx;
    const bool has_receiver_plane_depth_gradient =
        TryComputeReceiverPlaneDepthGradient(
            cascade,
            shadow_clip,
            position_dx_ws,
            position_dy_ws,
            receiver_plane_depth_gradient);
    const int radius = (int)round(AshShadowLightParams.w);
    float lit = 0.0;
    float count = 0.0;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float2 tap_offset =
                float2((float)x, (float)y) * cascade.texel_size_flags.xy;
            const float2 sample_uv = atlas_uv + tap_offset;
            const float shadow_depth = DirectionalShadowDynamicAtlas.SampleLevel(ScenePointClampSampler, sample_uv, 0);
            const float receiver_plane_depth_offset =
                ReceiverPlaneDepthOffsetOrZero(
                    receiver_plane_depth_gradient,
                    tap_offset,
                    has_receiver_plane_depth_gradient);
            const float comparison_depth = shadow_ndc.z +
                receiver_plane_depth_offset - cascade.split_depth_bias.z;
            lit += comparison_depth <= shadow_depth ? 1.0 : 0.0;
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
    const float3 position_ws = ReconstructWorldPosition(input.uv, scene_depth);
    const float3 position_dx_ws = ddx(position_ws);
    const float3 position_dy_ws = ddy(position_ws);
    if (IsBackgroundDepth(scene_depth))
    {
        return 1.0.xxxx;
    }

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
            float shadow = SampleCascadeShadow(
                buffer_index,
                position_ws,
                normal_ws,
                position_dx_ws,
                position_dy_ws);
            if (cascade_index + 1u < cascade_count)
            {
                const float transition_weight = ComputeCascadeTransitionWeight(view_depth, cascade);
                if (transition_weight > 0.0)
                {
                    const uint next_buffer_index = buffer_index + 1u;
                    const float next_shadow = SampleCascadeShadow(
                        next_buffer_index,
                        position_ws,
                        normal_ws,
                        position_dx_ws,
                        position_dy_ws);
                    shadow = lerp(shadow, next_shadow, transition_weight);
                }
            }
            return float4(shadow, shadow, shadow, 1.0);
        }
    }
    return 1.0.xxxx;
}

#include "AmbientOcclusionCommon.hlsli"

Texture2D<float4> SceneGBufferD : register(t3);

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const uint debug_view = (uint)round(AshAOParams2.x);
    const float depth = AshAOSampleSceneDepth(input.uv);
    if (AshAOSceneDepthIsBackground(depth))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (debug_view == 1u || debug_view == 2u)
    {
        const float ao = SceneAmbientOcclusionInput.SampleLevel(ScenePointClampSampler, input.uv, 0).r;
        return float4(ao.xxx, 1.0);
    }

    if (debug_view == 3u)
    {
        const float visible_depth = AshAOIsReverseZ() ? depth : (1.0 - depth);
        return float4(visible_depth.xxx, 1.0);
    }

    if (debug_view == 4u)
    {
        const float4 gbuffer_e = AshAOSampleSceneGBufferE(input.uv);
        const float3 normal = AshAODecodeNormalOct(gbuffer_e.rg);
        return float4(normal * 0.5 + 0.5, 1.0);
    }

    if (debug_view == 5u)
    {
        const float4 motion = SceneGBufferD.SampleLevel(ScenePointClampSampler, AshAOAdjustedSceneUv(input.uv), 0);
        const float2 encoded_motion = saturate(motion.xy * 32.0 + 0.5);
        return float4(encoded_motion, saturate(motion.a), 1.0);
    }

    if (debug_view == 6u)
    {
        const float ao = SceneAmbientOcclusionInput.SampleLevel(ScenePointClampSampler, input.uv, 0).r;
        return float4(ao.xxx, 1.0);
    }

    if (debug_view == 7u)
    {
        const float history_weight = SceneAmbientOcclusionInput.SampleLevel(ScenePointClampSampler, input.uv, 0).a;
        return float4(history_weight.xxx, 1.0);
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

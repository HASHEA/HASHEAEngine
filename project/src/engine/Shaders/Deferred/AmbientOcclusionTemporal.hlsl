#include "AmbientOcclusionCommon.hlsli"

Texture2D<float4> SceneGBufferD : register(t3);
Texture2D<float4> SceneAmbientOcclusionHistory : register(t4);
Texture2D<float4> SceneAmbientOcclusionHistoryMeta : register(t5);

struct PSTemporalAOOutput
{
    float4 resolved_ao : SV_Target0;
    float4 history_ao : SV_Target1;
    float4 history_meta : SV_Target2;
};

bool AshAOUvInRange(float2 uv)
{
    return all(uv >= 0.0.xx) && all(uv <= 1.0.xx);
}

PSTemporalAOOutput PSMain(VSFullscreenOutput input)
{
    PSTemporalAOOutput output;

    AshAOSurface current = AshAOLoadSurface(input.uv);
    const float current_ao = SceneAmbientOcclusionInput.SampleLevel(ScenePointClampSampler, input.uv, 0).r;
    float resolved_ao = current_ao;
    float history_weight = 0.0;

    const float max_history_weight = saturate(AshAOParams2.y);
    if (current.valid && max_history_weight > 0.0)
    {
        const float4 motion = SceneGBufferD.SampleLevel(ScenePointClampSampler, AshAOAdjustedSceneUv(input.uv), 0);
        const float2 previous_uv = input.uv - motion.xy;
        const float previous_depth = motion.z;

        if (motion.a > 0.5 && AshAOUvInRange(previous_uv) && !AshAOSceneDepthIsBackground(previous_depth))
        {
            const float4 history_ao = SceneAmbientOcclusionHistory.SampleLevel(ScenePointClampSampler, previous_uv, 0);
            const float4 history_meta = SceneAmbientOcclusionHistoryMeta.SampleLevel(ScenePointClampSampler, previous_uv, 0);
            const float3 previous_normal_ws = AshAODecodeNormalOct(history_meta.gb);
            const bool history_valid = history_meta.a > 0.5;
            const bool depth_valid = abs(previous_depth - history_meta.r) <= max(AshAOParams2.z, 1e-6);
            const bool normal_valid = dot(previous_normal_ws, current.normal_ws) >= saturate(AshAOParams2.w);

            if (history_valid && depth_valid && normal_valid)
            {
                history_weight = max_history_weight;
                resolved_ao = lerp(current_ao, history_ao.r, history_weight);
            }
        }
    }

    const float2 current_normal_oct = current.valid
        ? AshAOSampleSceneGBufferE(input.uv).rg
        : float2(0.5, 0.5);
    const float current_depth = current.valid ? current.depth : (AshAOIsReverseZ() ? 0.0 : 1.0);
    const float current_valid = current.valid ? 1.0 : 0.0;

    output.resolved_ao = float4(resolved_ao, resolved_ao, resolved_ao, history_weight);
    output.history_ao = float4(resolved_ao, resolved_ao, resolved_ao, 1.0);
    output.history_meta = float4(current_depth, current_normal_oct, current_valid);
    return output;
}

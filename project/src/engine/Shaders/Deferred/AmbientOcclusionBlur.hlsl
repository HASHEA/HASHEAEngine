#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const float center_depth = AshAOSampleSceneDepth(input.uv);
    if (AshAOSceneDepthIsBackground(center_depth))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    float weighted_sum = 0.0;
    float weight_sum = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const float2 sample_uv = saturate(input.uv + float2((float)x, (float)y) * AshViewportSize.zw);
            const float sample_depth = AshAOSampleSceneDepth(sample_uv);
            const float ao = SceneAmbientOcclusionInput.SampleLevel(ScenePointClampSampler, sample_uv, 0).r;
            const float depth_weight = saturate(1.0 - abs(sample_depth - center_depth) * 64.0);
            const float spatial_weight = 1.0 / (1.0 + abs((float)x) + abs((float)y));
            const float weight = depth_weight * spatial_weight;
            weighted_sum += ao * weight;
            weight_sum += weight;
        }
    }

    const float blurred = weighted_sum / max(weight_sum, 1e-5);
    return float4(blurred, blurred, blurred, 1.0);
}

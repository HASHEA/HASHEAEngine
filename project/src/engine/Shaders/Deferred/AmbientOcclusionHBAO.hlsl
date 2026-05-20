#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshAOSurface center = AshAOLoadSurface(input.uv);
    if (!center.valid)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint direction_count = (uint)clamp(round(AshAOParams1.y), 2.0, 8.0);
    const uint step_count = (uint)clamp(round(AshAOParams1.z), 2.0, 6.0);
    const float radius_uv = AshAOViewScaledRadiusUv(center);
    const float phase = frac(dot(input.uv * AshViewportSize.xy, float2(0.1031, 0.11369))) * 6.2831853;
    float occlusion = 0.0;

    for (uint direction_index = 0; direction_index < direction_count; ++direction_index)
    {
        const float angle = phase + ((float)direction_index + 0.5) * 6.2831853 / (float)direction_count;
        const float2 direction_uv = float2(cos(angle), sin(angle));
        float horizon = 0.0;
        for (uint step_index = 1; step_index <= step_count; ++step_index)
        {
            const float step_ratio = (float)step_index / (float)step_count;
            const float2 sample_uv = saturate(input.uv + direction_uv * radius_uv * step_ratio);
            AshAOSurface sample_surface = AshAOLoadSurface(sample_uv);
            horizon = max(horizon, AshAOContribution(center, sample_surface));
        }
        occlusion += horizon;
    }

    const float ao = AshAOApplyCurve(occlusion / max((float)direction_count, 1.0));
    return float4(ao, ao, ao, 1.0);
}

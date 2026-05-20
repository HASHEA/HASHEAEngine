#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshAOSurface center = AshAOLoadSurface(input.uv);
    if (!center.valid)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint slice_count = (uint)clamp(round(AshAOParams1.y), 2.0, 8.0);
    const uint step_count = (uint)clamp(round(AshAOParams1.z), 2.0, 6.0);
    const float radius_uv = AshAOViewScaledRadiusUv(center);
    const float phase = frac(dot(input.uv * AshViewportSize.xy, float2(0.75487767, 0.56984029))) * 3.14159265;
    float visibility_loss = 0.0;

    for (uint slice_index = 0; slice_index < slice_count; ++slice_index)
    {
        const float angle = phase + ((float)slice_index + 0.25) * 3.14159265 / (float)slice_count;
        const float2 axis = float2(cos(angle), sin(angle));
        float slice_occlusion = 0.0;

        [unroll]
        for (int side = -1; side <= 1; side += 2)
        {
            float horizon = 0.0;
            for (uint step_index = 1; step_index <= step_count; ++step_index)
            {
                const float step_ratio = (float)step_index / (float)step_count;
                const float2 sample_uv = saturate(input.uv + axis * (float)side * radius_uv * step_ratio);
                AshAOSurface sample_surface = AshAOLoadSurface(sample_uv);
                horizon = max(horizon, AshAOContribution(center, sample_surface));
            }
            slice_occlusion += horizon;
        }

        visibility_loss += slice_occlusion * 0.5;
    }

    const float ao = AshAOApplyCurve(visibility_loss / max((float)slice_count, 1.0));
    return float4(ao, ao, ao, 1.0);
}

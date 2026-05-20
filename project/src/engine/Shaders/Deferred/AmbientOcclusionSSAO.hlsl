#include "AmbientOcclusionCommon.hlsli"

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshAOSurface center = AshAOLoadSurface(input.uv);
    if (!center.valid)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    static const float2 k_offsets[16] = {
        float2( 0.5381,  0.1856), float2(-0.4319,  0.3416),
        float2( 0.1197, -0.4580), float2(-0.7935, -0.0978),
        float2( 0.2788,  0.6997), float2(-0.2287, -0.7863),
        float2( 0.7022, -0.4103), float2(-0.5174,  0.8041),
        float2( 0.9251,  0.1247), float2(-0.1246, -0.9321),
        float2( 0.3994, -0.1412), float2(-0.3319,  0.0440),
        float2( 0.0472,  0.3151), float2(-0.6331, -0.4828),
        float2( 0.8121,  0.5713), float2(-0.9175,  0.2976)
    };

    const uint sample_count = (uint)clamp(round(AshAOParams1.x), 1.0, 16.0);
    const float radius_uv = AshAOViewScaledRadiusUv(center);
    float occlusion = 0.0;
    for (uint i = 0; i < sample_count; ++i)
    {
        const float angle = frac(dot(input.uv * AshViewportSize.xy, float2(0.06711056, 0.00583715))) * 6.2831853;
        const float2 sample_uv = saturate(input.uv + AshAORotate(k_offsets[i], angle) * radius_uv);
        AshAOSurface sample_surface = AshAOLoadSurface(sample_uv);
        occlusion += AshAOContribution(center, sample_surface);
    }

    const float ao = AshAOApplyCurve(occlusion / max((float)sample_count, 1.0));
    return float4(ao, ao, ao, 1.0);
}

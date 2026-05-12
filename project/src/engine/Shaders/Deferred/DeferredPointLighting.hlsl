#include "DeferredCommon.hlsli"

VSVolumeOutput VSMain(VSVolumeInput input)
{
    return VSVolume(input);
}

float4 PSMain(VSVolumeOutput input) : SV_Target0
{
    const float2 uv = input.position.xy / max(AshViewportSize.xy, 1.0.xx);
    AshDeferredSurface surface = AshDecodeDeferredSurface(uv);
    if (!surface.valid)
    {
        discard;
    }

    const float3 to_light = AshLightPositionAndRange.xyz - surface.position_ws;
    const float distance = length(to_light);
    if (distance > AshLightPositionAndRange.w)
    {
        discard;
    }

    const float attenuation = AshRangeAttenuation(distance, AshLightPositionAndRange.w);
    const float3 radiance = AshLightColorAndType.rgb * AshLightDirectionAndIntensity.w * attenuation;
    return float4(AshEvaluateDynamicLight(surface, to_light / max(distance, 1e-4), radiance), 1.0);
}

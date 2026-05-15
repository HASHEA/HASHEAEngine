#include "DeferredCommon.hlsli"

struct PSOutput
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

VSVolumeOutput VSMain(VSVolumeInput input)
{
    return VSVolume(input);
}

PSOutput PSMain(VSVolumeOutput input)
{
    PSOutput output;
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
    const AshSplitLighting lit = AshEvaluateDynamicLight_Split(surface, to_light / max(distance, 1e-4), radiance);
    output.diffuse = float4(lit.diffuse, 1.0);
    output.specular = float4(lit.specular, 1.0);
    return output;
}

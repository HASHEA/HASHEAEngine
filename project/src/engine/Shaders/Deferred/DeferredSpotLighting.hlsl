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

    const float3 from_light = surface.position_ws - AshLightPositionAndRange.xyz;
    const float distance = length(from_light);
    if (distance > AshLightPositionAndRange.w)
    {
        discard;
    }

    const float3 light_forward = normalize(AshLightDirectionAndIntensity.xyz);
    const float3 light_to_surface = from_light / max(distance, 1e-4);
    const float cos_angle = dot(light_forward, light_to_surface);
    if (cos_angle < AshLightConeCos.y)
    {
        discard;
    }

    const float cone = saturate((cos_angle - AshLightConeCos.y) / max(AshLightConeCos.x - AshLightConeCos.y, 1e-4));
    const float attenuation = AshRangeAttenuation(distance, AshLightPositionAndRange.w) * cone * cone;
    const float3 radiance = AshLightColorAndType.rgb * AshLightDirectionAndIntensity.w * attenuation;
    const AshSplitLighting lit = AshEvaluateDynamicLight_Split(surface, -light_to_surface, radiance);
    output.diffuse = float4(lit.diffuse, 1.0);
    output.specular = float4(lit.specular, 1.0);
    return output;
}

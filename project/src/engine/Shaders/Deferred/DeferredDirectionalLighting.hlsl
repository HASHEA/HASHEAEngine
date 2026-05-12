#include "DeferredCommon.hlsli"

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshDeferredSurface surface = AshDecodeDeferredSurface(input.uv);
    const float3 light_dir_to_light = normalize(-AshLightDirectionAndIntensity.xyz);
    const float3 radiance = AshLightColorAndType.rgb * AshLightDirectionAndIntensity.w;
    return float4(AshEvaluateDynamicLight(surface, light_dir_to_light, radiance), 1.0);
}

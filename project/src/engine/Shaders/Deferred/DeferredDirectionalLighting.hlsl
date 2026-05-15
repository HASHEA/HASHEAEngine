#include "DeferredCommon.hlsli"

struct PSOutput
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

PSOutput PSMain(VSFullscreenOutput input)
{
    AshDeferredSurface surface = AshDecodeDeferredSurface(input.uv);
    const float3 light_dir_to_light = normalize(-AshLightDirectionAndIntensity.xyz);
    const float3 radiance = AshLightColorAndType.rgb * AshLightDirectionAndIntensity.w;
    const AshSplitLighting lit = AshEvaluateDynamicLight_Split(surface, light_dir_to_light, radiance);
    PSOutput output;
    output.diffuse = float4(lit.diffuse, 1.0);
    output.specular = float4(lit.specular, 1.0);
    return output;
}

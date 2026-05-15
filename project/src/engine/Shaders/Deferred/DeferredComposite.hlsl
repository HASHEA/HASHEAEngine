#include "DeferredCommon.hlsli"

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const float3 diffuse = SceneLightingDiffuse.Sample(ScenePointClampSampler, input.uv).rgb;
    const float3 specular = SceneLightingSpecular.Sample(ScenePointClampSampler, input.uv).rgb;
    return float4(diffuse + specular, 1.0);
}

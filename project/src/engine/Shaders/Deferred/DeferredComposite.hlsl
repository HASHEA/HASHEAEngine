#include "DeferredCommon.hlsli"

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    return float4(SceneLightingAccum.Sample(ScenePointClampSampler, input.uv).rgb, 1.0);
}

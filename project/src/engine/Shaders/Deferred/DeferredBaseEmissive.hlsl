#include "DeferredCommon.hlsli"

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    AshDeferredSurface surface = AshDecodeDeferredSurface(input.uv);
    return float4(AshEvaluateBaseEmissive(surface), 1.0);
}

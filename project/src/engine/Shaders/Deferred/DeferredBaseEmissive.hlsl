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
    const AshSplitLighting lit = AshEvaluateBaseEmissive_Split(surface);
    PSOutput output;
    output.diffuse = float4(lit.diffuse, 1.0);
    output.specular = float4(lit.specular, 1.0);
    return output;
}

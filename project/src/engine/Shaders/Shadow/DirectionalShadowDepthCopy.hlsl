#include "DirectionalShadowCommon.hlsli"

Texture2D<float> DirectionalShadowStaticCache : register(t0);
SamplerState ScenePointClampSampler : register(s0);

cbuffer AshRootConstants : register(b0)
{
    float4 AshShadowCopyScaleBias;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float PSMain(VSFullscreenOutput input) : SV_Depth
{
    const float2 source_uv = input.uv * AshShadowCopyScaleBias.xy + AshShadowCopyScaleBias.zw;
    return DirectionalShadowStaticCache.SampleLevel(ScenePointClampSampler, source_uv, 0);
}

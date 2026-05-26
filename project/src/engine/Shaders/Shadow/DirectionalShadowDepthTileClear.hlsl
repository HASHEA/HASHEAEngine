#include "DirectionalShadowCommon.hlsli"

cbuffer AshRootConstants : register(b0)
{
    float4 AshShadowClearParams;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

float PSMain(VSFullscreenOutput input) : SV_Depth
{
    return AshShadowClearParams.x;
}

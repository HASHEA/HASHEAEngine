#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct DebugDrawInput
{
    ASH_VK_LOCATION(0) float3 position_ws : POSITION;
    ASH_VK_LOCATION(1) float4 color : COLOR0;
};

struct DebugDrawOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshViewProjection;
};

DebugDrawOutput VSMain(DebugDrawInput input)
{
    DebugDrawOutput output;
    output.position = mul(AshViewProjection, float4(input.position_ws, 1.0));
    output.color = input.color;
    return output;
}

float4 PSMain(DebugDrawOutput input) : SV_Target0
{
    return input.color;
}

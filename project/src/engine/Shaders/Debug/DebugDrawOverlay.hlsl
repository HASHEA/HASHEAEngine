#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct DebugDrawInput
{
    ASH_VK_LOCATION(0) float3 position_ws : POSITION;
    ASH_VK_LOCATION(1) float4 color : COLOR0;
};

struct DebugDrawThickInput
{
    ASH_VK_LOCATION(0) float3 position_ws : POSITION;
    ASH_VK_LOCATION(1) float3 other_position_ws : TEXCOORD0;
    ASH_VK_LOCATION(2) float2 expand : TEXCOORD1; // x: side sign, y: thickness in pixels
    ASH_VK_LOCATION(3) float4 color : COLOR0;
};

struct DebugDrawOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshViewProjection;
    float AshDepthBias;
    float2 AshViewportSize;
    float _Padding;
};

DebugDrawOutput VSMain(DebugDrawInput input)
{
    DebugDrawOutput output;
    output.position = mul(AshViewProjection, float4(input.position_ws, 1.0));
    output.position.z += AshDepthBias * output.position.w;
    output.color = input.color;
    return output;
}

DebugDrawOutput VSThickMain(DebugDrawThickInput input)
{
    DebugDrawOutput output;
    float4 clip_self = mul(AshViewProjection, float4(input.position_ws, 1.0));
    float4 clip_other = mul(AshViewProjection, float4(input.other_position_ws, 1.0));

    const float2 half_viewport = max(AshViewportSize, 1.0) * 0.5;
    // 近平面后方 w<=0 会翻转投影方向，钳制换取稳定（穿近平面线段会有伪影，spec 已知限制）
    const float2 screen_self = clip_self.xy / max(clip_self.w, 1e-4) * half_viewport;
    const float2 screen_other = clip_other.xy / max(clip_other.w, 1e-4) * half_viewport;

    float2 dir = screen_other - screen_self;
    const float len = length(dir);
    dir = len > 1e-4 ? dir / len : float2(1.0, 0.0);
    const float2 perp = float2(-dir.y, dir.x);
    const float2 offset_px = perp * (input.expand.x * input.expand.y * 0.5);

    clip_self.xy += offset_px / half_viewport * clip_self.w;
    clip_self.z += AshDepthBias * clip_self.w;
    output.position = clip_self;
    output.color = input.color;
    return output;
}

float4 PSMain(DebugDrawOutput input) : SV_Target0
{
    return input.color;
}

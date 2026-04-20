#include "../../engine/Graphics/Shaders/AshVertexDeclLocations.hlsli"

#ifndef ASH_MESH_VERTEX_POSITION_ATTR
#if ASH_VULKAN
#define ASH_MESH_VERTEX_POSITION_ATTR [[vk::location(0)]]
#define ASH_MESH_VERTEX_NORMAL_ATTR [[vk::location(1)]]
#define ASH_MESH_VERTEX_TANGENT_ATTR [[vk::location(2)]]
#define ASH_MESH_VERTEX_TEXCOORD0_ATTR [[vk::location(3)]]
#define ASH_MESH_VERTEX_TEXCOORD1_ATTR [[vk::location(4)]]
#define ASH_MESH_VERTEX_COLOR0_ATTR [[vk::location(5)]]
#else
#define ASH_MESH_VERTEX_POSITION_ATTR
#define ASH_MESH_VERTEX_NORMAL_ATTR
#define ASH_MESH_VERTEX_TANGENT_ATTR
#define ASH_MESH_VERTEX_TEXCOORD0_ATTR
#define ASH_MESH_VERTEX_TEXCOORD1_ATTR
#define ASH_MESH_VERTEX_COLOR0_ATTR
#endif
#endif

struct VSInput
{
    ASH_MESH_VERTEX_POSITION_ATTR float3 position : POSITION;
    ASH_MESH_VERTEX_NORMAL_ATTR float3 normal : NORMAL;
    ASH_MESH_VERTEX_TANGENT_ATTR float4 tangent : TANGENT;
    ASH_MESH_VERTEX_TEXCOORD0_ATTR float2 uv0 : TEXCOORD0;
    ASH_MESH_VERTEX_TEXCOORD1_ATTR float2 uv1 : TEXCOORD1;
    ASH_MESH_VERTEX_COLOR0_ATTR float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float4 color : TEXCOORD2;
};

cbuffer AshRootConstants
{
    float4x4 ObjectToClip;
    float4 BaseColorFactor;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(ObjectToClip, float4(input.position, 1.0));
    output.normal = input.normal;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    float3 normal = normalize(abs(input.normal));
    float3 lit = 0.25.xxx + normal * 0.75;
    float3 albedo = saturate(BaseColorFactor.rgb * input.color.rgb);
    return float4(albedo * lit, BaseColorFactor.a * input.color.a);
}

#include "../../../Graphics/Shaders/AshVertexDeclLocations.hlsli"
#include "../Domains/AshSurfaceDomain.hlsli"
#include "GeneratedMaterialBindings.hlsli"
#include "UserShader.hlsli"

#ifndef ASH_MATERIAL_BLEND_MODE_MASKED
#define ASH_MATERIAL_BLEND_MODE_MASKED 0
#endif

#ifndef ASH_MATERIAL_ALPHA_CUTOFF
#define ASH_MATERIAL_ALPHA_CUTOFF 0.5
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
    float3 normal_os : TEXCOORD0;
    float4 tangent_os : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
    float2 uv1 : TEXCOORD3;
    float4 vertex_color : TEXCOORD4;
};

cbuffer AshRootConstants
{
    float4x4 ObjectToClip;
};

inline AshVertexParameters BuildSurfaceStaticMeshVertexParameters(VSInput input)
{
    AshVertexParameters params;
    params.position_os = input.position;
    params.normal_os = input.normal;
    params.tangent_os = input.tangent;
    params.uv0 = input.uv0;
    params.uv1 = input.uv1;
    params.vertex_color = input.color;
    return params;
}

inline VSOutput BuildSurfaceStaticMeshVertexOutput(
    VSInput input,
    AshVertexParameters params,
    AshVertexMainNode node)
{
    VSOutput output;
    const float3 displaced_position_os = params.position_os + node.world_position_offset;
    output.position = mul(ObjectToClip, float4(displaced_position_os, 1.0));
    output.normal_os = params.normal_os;
    output.tangent_os = params.tangent_os;
    output.uv0 = params.uv0;
    output.uv1 = params.uv1;
    output.vertex_color = params.vertex_color;
    return output;
}

inline AshPixelParameters BuildSurfaceStaticMeshPixelParameters(VSOutput input)
{
    AshPixelParameters params;
    params.position_cs = input.position;
    params.normal_os = input.normal_os;
    params.tangent_os = input.tangent_os;
    params.uv0 = input.uv0;
    params.uv1 = input.uv1;
    params.vertex_color = input.vertex_color;
    return params;
}

inline float3 EvaluateSurfaceStaticMeshNormal(AshPixelParameters params, AshPixelMainNode node)
{
    const float3 normal_os = normalize(params.normal_os);
    const float3 tangent_os = normalize(params.tangent_os.xyz);
    const float tangent_sign = params.tangent_os.w >= 0.0 ? 1.0 : -1.0;
    const float3 bitangent_os = normalize(cross(normal_os, tangent_os) * tangent_sign);
    return normalize(
        node.normal_ts.x * tangent_os +
        node.normal_ts.y * bitangent_os +
        node.normal_ts.z * normal_os);
}

inline float4 EvaluateSurfaceStaticMeshBasePass(AshPixelParameters params, AshPixelMainNode node)
{
#if ASH_MATERIAL_BLEND_MODE_MASKED
    if (node.opacity_mask < ASH_MATERIAL_ALPHA_CUTOFF)
    {
        discard;
    }
#endif

    const float alpha = saturate(node.opacity);
    const float3 normal = EvaluateSurfaceStaticMeshNormal(params, node);
    const float3 light_dir = normalize(float3(0.35, 0.55, 0.75));
    const float3 view_dir = normalize(float3(0.0, 0.0, 1.0));
    const float3 half_vector = normalize(light_dir + view_dir);

    const float n_dot_l = saturate(dot(normal, light_dir));
    const float n_dot_h = saturate(dot(normal, half_vector));
    const float roughness = saturate(node.roughness);
    const float metallic = saturate(node.metallic);
    const float diffuse = 0.18 + 0.82 * n_dot_l;
    const float specular_power = lerp(96.0, 8.0, roughness);
    const float specular = pow(n_dot_h, specular_power) * lerp(0.04, 1.0, metallic);

    float3 lit_color = node.base_color * diffuse * saturate(node.ambient_occlusion);
    lit_color += specular.xxx * lerp(0.20, 0.55, metallic);
    lit_color += node.emissive;
    return float4(saturate(lit_color), alpha);
}

VSOutput VSMain(VSInput input)
{
    AshVertexParameters params = BuildSurfaceStaticMeshVertexParameters(input);
    AshVertexMainNode node = AshInitializeVertexMainNode();
    CalculateVertexMainNode(params, node);
    return BuildSurfaceStaticMeshVertexOutput(input, params, node);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    AshPixelParameters params = BuildSurfaceStaticMeshPixelParameters(input);
    AshPixelMainNode node = AshInitializePixelMainNode();
    CalculatePixelMainNode(params, node);
    return EvaluateSurfaceStaticMeshBasePass(params, node);
}

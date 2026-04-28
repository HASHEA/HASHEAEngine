#pragma once

struct SurfaceVertexParameters
{
    float3 position_os;
    float3 normal_os;
    float4 tangent_os;
    float2 uv0;
    float2 uv1;
    float4 vertex_color;
};

struct SurfacePixelParameters
{
    float4 position_cs;
    float3 normal_os;
    float4 tangent_os;
    float2 uv0;
    float2 uv1;
    float4 vertex_color;
};

struct SurfaceVertexMainNode
{
    float3 world_position_offset;
};

struct SurfacePixelMainNode
{
    float3 base_color;
    float opacity;
    float opacity_mask;
    float3 normal_ts;
    float metallic;
    float roughness;
    float3 emissive;
    float ambient_occlusion;
    float pixel_depth_offset;
};

typedef SurfaceVertexParameters AshVertexParameters;
typedef SurfacePixelParameters AshPixelParameters;
typedef SurfaceVertexMainNode AshVertexMainNode;
typedef SurfacePixelMainNode AshPixelMainNode;

inline SurfaceVertexMainNode AshInitializeSurfaceVertexMainNode()
{
    SurfaceVertexMainNode node;
    node.world_position_offset = float3(0.0, 0.0, 0.0);
    return node;
}

inline SurfacePixelMainNode AshInitializeSurfacePixelMainNode()
{
    SurfacePixelMainNode node;
    node.base_color = float3(1.0, 1.0, 1.0);
    node.opacity = 1.0;
    node.opacity_mask = 1.0;
    node.normal_ts = float3(0.0, 0.0, 1.0);
    node.metallic = 0.0;
    node.roughness = 0.5;
    node.emissive = float3(0.0, 0.0, 0.0);
    node.ambient_occlusion = 1.0;
    node.pixel_depth_offset = 0.0;
    return node;
}

inline AshVertexMainNode AshInitializeVertexMainNode()
{
    return AshInitializeSurfaceVertexMainNode();
}

inline AshPixelMainNode AshInitializePixelMainNode()
{
    return AshInitializeSurfacePixelMainNode();
}

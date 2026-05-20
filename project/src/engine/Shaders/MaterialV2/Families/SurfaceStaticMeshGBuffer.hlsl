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

#ifndef ASH_MATERIAL_SHADING_MODEL_ID
#define ASH_MATERIAL_SHADING_MODEL_ID 1
#endif

struct VSInput
{
    ASH_MESH_VERTEX_POSITION_ATTR float3 position : POSITION;
    ASH_MESH_VERTEX_NORMAL_ATTR float3 normal : NORMAL;
    ASH_MESH_VERTEX_TANGENT_ATTR float4 tangent : TANGENT;
    ASH_MESH_VERTEX_TEXCOORD0_ATTR float2 uv0 : TEXCOORD0;
    ASH_MESH_VERTEX_TEXCOORD1_ATTR float2 uv1 : TEXCOORD1;
    ASH_MESH_VERTEX_COLOR0_ATTR float4 color : COLOR0;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL0_ATTR float4 object_to_clip_col0 : TEXCOORD2;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL1_ATTR float4 object_to_clip_col1 : TEXCOORD3;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL2_ATTR float4 object_to_clip_col2 : TEXCOORD4;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL3_ATTR float4 object_to_clip_col3 : TEXCOORD5;
    ASH_MESH_INSTANCE_PREVIOUS_OBJECT_TO_CLIP_COL0_ATTR float4 previous_object_to_clip_col0 : TEXCOORD6;
    ASH_MESH_INSTANCE_PREVIOUS_OBJECT_TO_CLIP_COL1_ATTR float4 previous_object_to_clip_col1 : TEXCOORD7;
    ASH_MESH_INSTANCE_PREVIOUS_OBJECT_TO_CLIP_COL2_ATTR float4 previous_object_to_clip_col2 : TEXCOORD8;
    ASH_MESH_INSTANCE_PREVIOUS_OBJECT_TO_CLIP_COL3_ATTR float4 previous_object_to_clip_col3 : TEXCOORD9;
    ASH_MESH_INSTANCE_TEMPORAL_FLAGS_ATTR float4 temporal_flags : TEXCOORD10;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal_os : TEXCOORD0;
    float4 tangent_os : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
    float2 uv1 : TEXCOORD3;
    float4 vertex_color : TEXCOORD4;
    float4 current_clip : TEXCOORD5;
    float4 previous_clip : TEXCOORD6;
    float temporal_valid : TEXCOORD7;
};

struct AshGBufferOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float4 target4 : SV_Target4;
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

inline float4 TransformSurfaceStaticMeshPositionToClip(VSInput input, float3 position_os)
{
    return
        input.object_to_clip_col0 * position_os.x +
        input.object_to_clip_col1 * position_os.y +
        input.object_to_clip_col2 * position_os.z +
        input.object_to_clip_col3;
}

inline float4 TransformSurfaceStaticMeshPreviousPositionToClip(VSInput input, float3 position_os)
{
    return
        input.previous_object_to_clip_col0 * position_os.x +
        input.previous_object_to_clip_col1 * position_os.y +
        input.previous_object_to_clip_col2 * position_os.z +
        input.previous_object_to_clip_col3;
}

inline VSOutput BuildSurfaceStaticMeshVertexOutput(
    VSInput input,
    AshVertexParameters params,
    AshVertexMainNode node)
{
    VSOutput output;
    const float3 displaced_position_os = params.position_os + node.world_position_offset;
    output.current_clip = TransformSurfaceStaticMeshPositionToClip(input, displaced_position_os);
    output.previous_clip = TransformSurfaceStaticMeshPreviousPositionToClip(input, displaced_position_os);
    output.position = output.current_clip;
    output.normal_os = params.normal_os;
    output.tangent_os = params.tangent_os;
    output.uv0 = params.uv0;
    output.uv1 = params.uv1;
    output.vertex_color = params.vertex_color;
    output.temporal_valid = input.temporal_flags.x;
    return output;
}

inline AshPixelParameters BuildSurfaceStaticMeshPixelParameters(VSOutput input)
{
    AshPixelParameters params;
    params.position_cs = input.position;
    params.clip_position_cs = input.current_clip;
    params.previous_clip_position_cs = input.previous_clip;
    params.normal_os = input.normal_os;
    params.tangent_os = input.tangent_os;
    params.uv0 = input.uv0;
    params.uv1 = input.uv1;
    params.vertex_color = input.vertex_color;
    params.temporal_valid = input.temporal_valid;
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

inline float2 AshSignNotZero(float2 value)
{
    return float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
}

inline float2 AshEncodeNormalOct(float3 normal)
{
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1e-5);
    float2 encoded = normal.xy;
    if (normal.z < 0.0)
    {
        encoded = (1.0 - abs(encoded.yx)) * AshSignNotZero(encoded);
    }
    return encoded * 0.5 + 0.5;
}

inline bool AshClipPositionIsValid(float4 clip)
{
    return abs(clip.w) > 1e-5;
}

inline float2 AshClipToUv(float4 clip)
{
    const float2 ndc = clip.xy / clip.w;
    return ndc * float2(0.5, -0.5) + float2(0.5, 0.5);
}

inline AshGBufferOutput EncodeSurfaceStaticMeshGBuffer(AshPixelParameters params, AshPixelMainNode node)
{
#if ASH_MATERIAL_BLEND_MODE_MASKED
    if (node.opacity_mask < ASH_MATERIAL_ALPHA_CUTOFF)
    {
        discard;
    }
#endif

    AshGBufferOutput output;
    const float3 normal = EvaluateSurfaceStaticMeshNormal(params, node);
    const bool temporal_valid = params.temporal_valid > 0.5 &&
        AshClipPositionIsValid(params.clip_position_cs) &&
        AshClipPositionIsValid(params.previous_clip_position_cs);
    const float2 current_uv = temporal_valid ? AshClipToUv(params.clip_position_cs) : 0.0.xx;
    const float2 previous_uv = temporal_valid ? AshClipToUv(params.previous_clip_position_cs) : current_uv;
    const float2 motion_vector = temporal_valid ? current_uv - previous_uv : 0.0.xx;
    const float previous_depth = temporal_valid ? params.previous_clip_position_cs.z / params.previous_clip_position_cs.w : params.clip_position_cs.z / max(params.clip_position_cs.w, 1e-5);
    output.target0 = float4(saturate(node.base_color), ASH_MATERIAL_SHADING_MODEL_ID / 255.0);
    output.target1 = float4(saturate(node.metallic), saturate(node.roughness), saturate(node.ambient_occlusion), 0.5);
    output.target2 = float4(0.0, 0.0, 0.0, 0.0);
    output.target3 = float4(motion_vector, previous_depth, temporal_valid ? 1.0 : 0.0);
    output.target4 = float4(AshEncodeNormalOct(normal), node.emissive.rg);
    return output;
}

VSOutput VSMain(VSInput input)
{
    AshVertexParameters params = BuildSurfaceStaticMeshVertexParameters(input);
    AshVertexMainNode node = AshInitializeVertexMainNode();
    CalculateVertexMainNode(params, node);
    return BuildSurfaceStaticMeshVertexOutput(input, params, node);
}

AshGBufferOutput PSMain(VSOutput input)
{
    AshPixelParameters params = BuildSurfaceStaticMeshPixelParameters(input);
    AshPixelMainNode node = AshInitializePixelMainNode();
    CalculatePixelMainNode(params, node);
    return EncodeSurfaceStaticMeshGBuffer(params, node);
}

#include "TerrainCommon.hlsli"

#ifndef TERRAIN_GBUFFER
#define TERRAIN_GBUFFER 0
#endif

#ifndef TERRAIN_DEPTH_ONLY
#define TERRAIN_DEPTH_ONLY 0
#endif

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshTerrainObjectToClip;
    float4x4 AshTerrainPreviousObjectToClip;
    float4x4 AshTerrainObjectToWorld;
    // x=height offset, y=height range, z=sample spacing, w=material UV scale
    float4 AshTerrainHeightSpacingUvScale;
    // x=temporal valid
    uint4 AshTerrainFlags;
};

StructuredBuffer<uint> TerrainHeightWords : register(t0);
StructuredBuffer<uint4> TerrainInstances : register(t1);

#if TERRAIN_GBUFFER
Texture2D<float4> TerrainWeightAtlas0 : register(t2);
Texture2D<float4> TerrainWeightAtlas1 : register(t3);
Texture2D<float4> TerrainCoarseWeights : register(t4);
Texture2DArray<float4> TerrainBaseColorLayers : register(t5);
Texture2DArray<float4> TerrainNormalLayers : register(t6);
Texture2DArray<float4> TerrainOrmLayers : register(t7);
SamplerState TerrainWeightSampler : register(s0);
SamplerState TerrainMaterialSampler : register(s1);
#endif

struct AshTerrainVertexOutput
{
    float4 position : SV_Position;
#if TERRAIN_GBUFFER
    float3 normal_ws : TEXCOORD0;
    float2 local_sample : TEXCOORD1;
    float2 material_uv : TEXCOORD2;
    float4 current_clip : TEXCOORD3;
    float4 previous_clip : TEXCOORD4;
    nointerpolation uint atlas_slot : TEXCOORD5;
    nointerpolation uint high_resolution_weights : TEXCOORD6;
    nointerpolation uint2 component_coord : TEXCOORD7;
#endif
};

#if TERRAIN_GBUFFER
struct AshTerrainGBufferOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float4 target4 : SV_Target4;
};

float2 AshTerrainSignNotZero(float2 value)
{
    return float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
}

float2 AshTerrainEncodeNormalOct(float3 normal)
{
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1e-5);
    float2 encoded = normal.xy;
    if (normal.z < 0.0)
    {
        encoded = (1.0 - abs(encoded.yx)) * AshTerrainSignNotZero(encoded);
    }
    return encoded * 0.5 + 0.5;
}

void AshTerrainSelectTopFour(
    float4 weights0,
    float4 weights1,
    out uint4 layer_indices,
    out float4 layer_weights)
{
    float weights[8] = {
        weights0.x, weights0.y, weights0.z, weights0.w,
        weights1.x, weights1.y, weights1.z, weights1.w
    };
    layer_indices = uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
    layer_weights = 0.0.xxxx;
    [unroll]
    for (uint layer = 0u; layer < 8u; ++layer)
    {
        const float candidate_weight = max(weights[layer], 0.0);
        if (candidate_weight <= 0.0)
        {
            continue;
        }
        [unroll]
        for (uint rank = 0u; rank < 4u; ++rank)
        {
            const bool larger = candidate_weight > layer_weights[rank];
            const bool tied_before = candidate_weight == layer_weights[rank] &&
                layer < layer_indices[rank];
            if (larger || tied_before)
            {
                for (int shift = 3; shift > int(rank); --shift)
                {
                    layer_weights[shift] = layer_weights[shift - 1];
                    layer_indices[shift] = layer_indices[shift - 1];
                }
                layer_weights[rank] = candidate_weight;
                layer_indices[rank] = layer;
                break;
            }
        }
    }
    const float weight_sum = dot(layer_weights, 1.0.xxxx);
    if (weight_sum <= 1e-6)
    {
        layer_indices = uint4(0u, 0u, 0u, 0u);
        layer_weights = float4(1.0, 0.0, 0.0, 0.0);
    }
    else
    {
        layer_weights /= weight_sum;
    }
}

float2 AshTerrainClipToUv(float4 clip)
{
    const float2 ndc = clip.xy / clip.w;
    return ndc * float2(0.5, -0.5) + float2(0.5, 0.5);
}
#endif

AshTerrainVertexOutput VSMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    const AshTerrainInstance instance =
        AshTerrainDecodeInstance(TerrainInstances[instance_id]);
    const uint resolution = AshTerrainComponentQuads >> instance.lod;
    const uint row_stride = resolution + 1u;
    const uint2 grid_coord = uint2(vertex_id % row_stride, vertex_id / row_stride);
    const uint sample_step = 1u << instance.lod;
    const uint2 local_sample = grid_coord * sample_step;
    const float height = AshTerrainMorphHeight(
        TerrainHeightWords,
        instance,
        local_sample,
        AshTerrainHeightSpacingUvScale.x,
        AshTerrainHeightSpacingUvScale.y);
    const float2 local_xz = float2(
        instance.component_coord * AshTerrainComponentQuads + local_sample) *
        AshTerrainHeightSpacingUvScale.z;
    const float3 position_os = float3(local_xz.x, height, local_xz.y);

    AshTerrainVertexOutput output;
    output.position = mul(AshTerrainObjectToClip, float4(position_os, 1.0));
#if TERRAIN_GBUFFER
    const int2 global_sample = int2(
        instance.component_coord * AshTerrainComponentQuads + local_sample);
    const float3 normal_os = AshTerrainLocalNormal(
        TerrainHeightWords,
        global_sample,
        AshTerrainHeightSpacingUvScale.x,
        AshTerrainHeightSpacingUvScale.y,
        AshTerrainHeightSpacingUvScale.z);
    const float3 tangent_x_ws = mul(
        (float3x3)AshTerrainObjectToWorld,
        float3(1.0, (-normal_os.x / max(normal_os.y, 1e-5)), 0.0));
    const float3 tangent_z_ws = mul(
        (float3x3)AshTerrainObjectToWorld,
        float3(0.0, (-normal_os.z / max(normal_os.y, 1e-5)), 1.0));
    output.normal_ws = normalize(cross(tangent_z_ws, tangent_x_ws));
    output.local_sample = float2(local_sample);
    output.material_uv = local_xz * AshTerrainHeightSpacingUvScale.w;
    output.current_clip = output.position;
    output.previous_clip = mul(
        AshTerrainPreviousObjectToClip,
        float4(position_os, 1.0));
    output.atlas_slot = instance.atlas_slot;
    output.high_resolution_weights = instance.high_resolution_weights ? 1u : 0u;
    output.component_coord = instance.component_coord;
#endif
    return output;
}

#if TERRAIN_GBUFFER
AshTerrainGBufferOutput PSMain(AshTerrainVertexOutput input)
{
    const AshTerrainInstance weight_instance = {
        input.component_coord,
        0u,
        0u,
        0.0,
        input.atlas_slot,
        input.high_resolution_weights != 0u
    };
    const float2 weight_uv = input.high_resolution_weights != 0u ?
        AshTerrainAtlasUv(weight_instance, input.local_sample) :
        AshTerrainCoarseUv(weight_instance, input.local_sample);
    const float4 weights0 = input.high_resolution_weights != 0u ?
        TerrainWeightAtlas0.Sample(TerrainWeightSampler, weight_uv) :
        TerrainCoarseWeights.Sample(TerrainWeightSampler, weight_uv);
    const float4 weights1 = input.high_resolution_weights != 0u ?
        TerrainWeightAtlas1.Sample(TerrainWeightSampler, weight_uv) : 0.0.xxxx;
    uint4 layer_indices;
    float4 layer_weights;
    AshTerrainSelectTopFour(weights0, weights1, layer_indices, layer_weights);

    float3 base_color = 0.0.xxx;
    float3 normal_ts = 0.0.xxx;
    float3 orm = 0.0.xxx;
    [unroll]
    for (uint rank = 0u; rank < 4u; ++rank)
    {
        const float weight = layer_weights[rank];
        const float3 uv_layer = float3(input.material_uv, float(layer_indices[rank]));
        base_color += TerrainBaseColorLayers.Sample(
            TerrainMaterialSampler, uv_layer).rgb * weight;
        normal_ts += (TerrainNormalLayers.Sample(
            TerrainMaterialSampler, uv_layer).xyz * 2.0 - 1.0) * weight;
        orm += TerrainOrmLayers.Sample(
            TerrainMaterialSampler, uv_layer).rgb * weight;
    }

    const float3 geometric_normal = normalize(input.normal_ws);
    float3 tangent_ws = mul(
        (float3x3)AshTerrainObjectToWorld, float3(1.0, 0.0, 0.0));
    tangent_ws = normalize(tangent_ws - geometric_normal * dot(tangent_ws, geometric_normal));
    const float3 bitangent_ws = normalize(cross(tangent_ws, geometric_normal));
    const float3 normal_ws = normalize(
        tangent_ws * normal_ts.x +
        bitangent_ws * normal_ts.y +
        geometric_normal * max(normal_ts.z, 1e-4));

    const bool temporal_valid = AshTerrainFlags.x != 0u &&
        abs(input.current_clip.w) > 1e-5 && abs(input.previous_clip.w) > 1e-5;
    const float2 current_uv = temporal_valid ? AshTerrainClipToUv(input.current_clip) : 0.0.xx;
    const float2 previous_uv = temporal_valid ? AshTerrainClipToUv(input.previous_clip) : current_uv;
    const float previous_depth = temporal_valid ?
        input.previous_clip.z / input.previous_clip.w :
        input.current_clip.z / max(input.current_clip.w, 1e-5);

    AshTerrainGBufferOutput output;
    output.target0 = float4(saturate(base_color), 1.0 / 255.0);
    output.target1 = float4(saturate(orm.b), saturate(orm.g), saturate(orm.r), 0.5);
    output.target2 = 0.0.xxxx;
    output.target3 = float4(
        temporal_valid ? current_uv - previous_uv : 0.0.xx,
        previous_depth,
        temporal_valid ? 1.0 : 0.0);
    output.target4 = float4(AshTerrainEncodeNormalOct(normal_ws), 0.0, 0.0);
    return output;
}
#else
void PSMain(AshTerrainVertexOutput input)
{
}
#endif

#pragma once

static const uint AshTerrainComponentQuads = 256u;
static const uint AshTerrainComponentSamples = 257u;
static const uint AshTerrainHeightWordsPerComponent = 33025u;
static const uint AshTerrainAtlasSlotExtent = 259u;
static const uint AshTerrainAtlasGridWidth = 16u;
static const uint AshTerrainAtlasExtent = 4144u;

struct AshTerrainInstance
{
    uint2 component_coord;
    uint lod;
    uint neighbor_edge_mask;
    float morph_factor;
    uint atlas_slot;
    bool high_resolution_weights;
    bool implicit_layer_zero;
};

AshTerrainInstance AshTerrainDecodeInstance(uint4 packed)
{
    // packed.x: component x[0..4], z[5..9], lod[10..13], edge mask[14..17]
    // packed.y: asuint(morph), packed.z: atlas slot.
    // packed.w bit 0: high-res, bit 1: implicit material layer zero.
    AshTerrainInstance result;
    result.component_coord = uint2(packed.x & 31u, (packed.x >> 5u) & 31u);
    result.lod = (packed.x >> 10u) & 15u;
    result.neighbor_edge_mask = (packed.x >> 14u) & 15u;
    result.morph_factor = asfloat(packed.y);
    result.atlas_slot = packed.z;
    result.high_resolution_weights = (packed.w & 1u) != 0u;
    result.implicit_layer_zero = (packed.w & 2u) != 0u;
    return result;
}

float AshTerrainDecodeHeight(uint encoded, float height_offset, float height_range)
{
    return height_offset + (float(encoded) * (1.0 / 65535.0)) * height_range;
}

uint AshTerrainLoadEncodedHeight(
    StructuredBuffer<uint> height_words,
    uint2 component_coord,
    uint2 local_sample)
{
    const uint component_index =
        component_coord.y * AshTerrainLayout.x + component_coord.x;
    const uint sample_index =
        local_sample.y * AshTerrainComponentSamples + local_sample.x;
    const uint packed = height_words[
        component_index * AshTerrainHeightWordsPerComponent + sample_index / 2u];
    return (sample_index & 1u) == 0u ? packed & 0xffffu : packed >> 16u;
}

float AshTerrainLoadHeight(
    StructuredBuffer<uint> height_words,
    uint2 component_coord,
    uint2 local_sample,
    float height_offset,
    float height_range)
{
    return AshTerrainDecodeHeight(
        AshTerrainLoadEncodedHeight(height_words, component_coord, local_sample),
        height_offset,
        height_range);
}

float AshTerrainLoadGlobalHeight(
    StructuredBuffer<uint> height_words,
    int2 global_sample,
    float height_offset,
    float height_range)
{
    const int2 max_sample = int2(AshTerrainLayout.zw) - int2(1, 1);
    const uint2 clamped_sample = uint2(clamp(
        global_sample,
        int2(0, 0),
        max_sample));
    const uint2 component_coord = min(
        clamped_sample / AshTerrainComponentQuads,
        AshTerrainLayout.xy - 1u);
    const uint2 local_sample =
        clamped_sample - component_coord * AshTerrainComponentQuads;
    return AshTerrainLoadHeight(
        height_words,
        component_coord,
        local_sample,
        height_offset,
        height_range);
}

float AshTerrainCoarseTriangleHeight(
    StructuredBuffer<uint> height_words,
    uint2 component_coord,
    uint2 local_sample,
    uint coarse_step,
    float height_offset,
    float height_range)
{
    const uint2 cell_min =
        min((local_sample / coarse_step) * coarse_step,
            uint2(AshTerrainComponentQuads - coarse_step, AshTerrainComponentQuads - coarse_step));
    const uint2 cell_max = cell_min + coarse_step;
    const float2 fraction =
        (float2(local_sample) - float2(cell_min)) / float(coarse_step);
    const float h00 = AshTerrainLoadHeight(
        height_words, component_coord, cell_min, height_offset, height_range);
    const float h10 = AshTerrainLoadHeight(
        height_words, component_coord, uint2(cell_max.x, cell_min.y), height_offset, height_range);
    const float h01 = AshTerrainLoadHeight(
        height_words, component_coord, uint2(cell_min.x, cell_max.y), height_offset, height_range);
    const float h11 = AshTerrainLoadHeight(
        height_words, component_coord, cell_max, height_offset, height_range);
    if (fraction.x + fraction.y <= 1.0)
    {
        return h00 + fraction.x * (h10 - h00) + fraction.y * (h01 - h00);
    }
    return h11 + (1.0 - fraction.y) * (h10 - h11) +
        (1.0 - fraction.x) * (h01 - h11);
}

float AshTerrainMorphFactorAtSample(
    AshTerrainInstance instance,
    uint2 local_sample)
{
    const bool west = local_sample.x == 0u &&
        (instance.neighbor_edge_mask & 1u) != 0u;
    const bool east = local_sample.x == AshTerrainComponentQuads &&
        (instance.neighbor_edge_mask & 2u) != 0u;
    const bool north = local_sample.y == 0u &&
        (instance.neighbor_edge_mask & 4u) != 0u;
    const bool south = local_sample.y == AshTerrainComponentQuads &&
        (instance.neighbor_edge_mask & 8u) != 0u;
    return (west || east || north || south) ?
        1.0 : saturate(instance.morph_factor);
}

float AshTerrainMorphHeight(
    StructuredBuffer<uint> height_words,
    AshTerrainInstance instance,
    uint2 local_sample,
    float height_offset,
    float height_range)
{
    const float fine_height = AshTerrainLoadHeight(
        height_words,
        instance.component_coord,
        local_sample,
        height_offset,
        height_range);
    const uint fine_step = 1u << instance.lod;
    const uint coarse_step = min(fine_step * 2u, AshTerrainComponentQuads);
    const float coarse_height = AshTerrainCoarseTriangleHeight(
        height_words,
        instance.component_coord,
        local_sample,
        coarse_step,
        height_offset,
        height_range);

    const float morph = AshTerrainMorphFactorAtSample(
        instance,
        local_sample);
    return lerp(fine_height, coarse_height, morph);
}

float2 AshTerrainCanonicalHeightGradient(
    StructuredBuffer<uint> height_words,
    int2 global_sample,
    float height_offset,
    float height_range,
    float sample_spacing)
{
    const int2 max_sample = int2(AshTerrainLayout.zw) - int2(1, 1);
    const int2 center_sample = clamp(
        global_sample,
        int2(0, 0),
        max_sample);
    const int2 west_sample = int2(
        max(center_sample.x - 1, 0),
        center_sample.y);
    const int2 east_sample = int2(
        min(center_sample.x + 1, max_sample.x),
        center_sample.y);
    const int2 north_sample = int2(
        center_sample.x,
        max(center_sample.y - 1, 0));
    const int2 south_sample = int2(
        center_sample.x,
        min(center_sample.y + 1, max_sample.y));
    const float west = AshTerrainLoadGlobalHeight(
        height_words, west_sample, height_offset, height_range);
    const float east = AshTerrainLoadGlobalHeight(
        height_words, east_sample, height_offset, height_range);
    const float north = AshTerrainLoadGlobalHeight(
        height_words, north_sample, height_offset, height_range);
    const float south = AshTerrainLoadGlobalHeight(
        height_words, south_sample, height_offset, height_range);
    const float safe_spacing = max(sample_spacing, 1e-5);
    const float x_span = max(
        float(east_sample.x - west_sample.x) * safe_spacing,
        1e-5);
    const float z_span = max(
        float(south_sample.y - north_sample.y) * safe_spacing,
        1e-5);
    const float2 gradient = float2(
        (east - west) / x_span,
        (south - north) / z_span);
    return isfinite(gradient.x) && isfinite(gradient.y) ?
        gradient : 0.0.xx;
}

float2 AshTerrainInterpolateCanonicalEdgeGradient(
    StructuredBuffer<uint> height_words,
    int2 global_sample,
    uint coarse_neighbor_step,
    bool along_x,
    float height_offset,
    float height_range,
    float sample_spacing)
{
    const int2 max_sample = int2(AshTerrainLayout.zw) - int2(1, 1);
    const int2 center_sample = clamp(
        global_sample,
        int2(0, 0),
        max_sample);
    const int safe_step = int(max(coarse_neighbor_step, 1u));
    const int varying_sample = along_x ?
        center_sample.x : center_sample.y;
    const int varying_max = along_x ? max_sample.x : max_sample.y;
    const int segment_begin =
        (varying_sample / safe_step) * safe_step;
    const int segment_end = min(segment_begin + safe_step, varying_max);
    if (segment_end <= segment_begin)
    {
        return AshTerrainCanonicalHeightGradient(
            height_words,
            center_sample,
            height_offset,
            height_range,
            sample_spacing);
    }

    int2 begin_sample = center_sample;
    int2 end_sample = center_sample;
    if (along_x)
    {
        begin_sample.x = segment_begin;
        end_sample.x = segment_end;
    }
    else
    {
        begin_sample.y = segment_begin;
        end_sample.y = segment_end;
    }
    const float2 begin_gradient = AshTerrainCanonicalHeightGradient(
        height_words,
        begin_sample,
        height_offset,
        height_range,
        sample_spacing);
    const float2 end_gradient = AshTerrainCanonicalHeightGradient(
        height_words,
        end_sample,
        height_offset,
        height_range,
        sample_spacing);
    const float fraction = saturate(
        float(varying_sample - segment_begin) /
        float(segment_end - segment_begin));
    return lerp(begin_gradient, end_gradient, fraction);
}

float2 AshTerrainShadingHeightGradient(
    StructuredBuffer<uint> height_words,
    AshTerrainInstance instance,
    uint2 local_sample,
    float height_offset,
    float height_range,
    float sample_spacing)
{
    const int2 global_sample = int2(
        instance.component_coord * AshTerrainComponentQuads + local_sample);
    const float2 canonical_gradient = AshTerrainCanonicalHeightGradient(
        height_words,
        global_sample,
        height_offset,
        height_range,
        sample_spacing);
    const bool west = local_sample.x == 0u &&
        (instance.neighbor_edge_mask & 1u) != 0u;
    const bool east = local_sample.x == AshTerrainComponentQuads &&
        (instance.neighbor_edge_mask & 2u) != 0u;
    const bool north = local_sample.y == 0u &&
        (instance.neighbor_edge_mask & 4u) != 0u;
    const bool south = local_sample.y == AshTerrainComponentQuads &&
        (instance.neighbor_edge_mask & 8u) != 0u;
    const uint coarse_neighbor_step = min(
        (1u << instance.lod) * 2u,
        AshTerrainComponentQuads);
    if (west || east)
    {
        return AshTerrainInterpolateCanonicalEdgeGradient(
            height_words,
            global_sample,
            coarse_neighbor_step,
            false,
            height_offset,
            height_range,
            sample_spacing);
    }
    if (north || south)
    {
        return AshTerrainInterpolateCanonicalEdgeGradient(
            height_words,
            global_sample,
            coarse_neighbor_step,
            true,
            height_offset,
            height_range,
            sample_spacing);
    }
    return canonical_gradient;
}

float2 AshTerrainAtlasUv(AshTerrainInstance instance, float2 local_sample)
{
    const uint2 slot_coord = uint2(
        instance.atlas_slot % AshTerrainAtlasGridWidth,
        instance.atlas_slot / AshTerrainAtlasGridWidth);
    const float2 pixel = float2(slot_coord * AshTerrainAtlasSlotExtent) +
        local_sample + 1.5;
    return pixel / float(AshTerrainAtlasExtent);
}

float2 AshTerrainCoarseUv(AshTerrainInstance instance, float2 local_sample)
{
    const float2 global_sample =
        float2(instance.component_coord * AshTerrainComponentQuads) + local_sample;
    const float2 coarse_extent = float2(AshTerrainLayout.xy * 32u + 1u);
    return (global_sample / 8.0 + 0.5) / coarse_extent;
}

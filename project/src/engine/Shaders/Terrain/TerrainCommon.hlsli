#pragma once

static const uint AshTerrainComponentCount = 32u;
static const uint AshTerrainComponentQuads = 256u;
static const uint AshTerrainComponentSamples = 257u;
static const uint AshTerrainHeightWordsPerComponent = 33025u;
static const uint AshTerrainAtlasSlotExtent = 259u;
static const uint AshTerrainAtlasGridWidth = 16u;
static const uint AshTerrainAtlasExtent = 4144u;
static const uint AshTerrainCoarseExtent = 1025u;

struct AshTerrainInstance
{
    uint2 component_coord;
    uint lod;
    uint neighbor_edge_mask;
    float morph_factor;
    uint atlas_slot;
    bool high_resolution_weights;
};

AshTerrainInstance AshTerrainDecodeInstance(uint4 packed)
{
    // packed.x: component x[0..4], z[5..9], lod[10..13], edge mask[14..17]
    // packed.y: asuint(morph), packed.z: atlas slot, packed.w bit 0: high-res.
    AshTerrainInstance result;
    result.component_coord = uint2(packed.x & 31u, (packed.x >> 5u) & 31u);
    result.lod = (packed.x >> 10u) & 15u;
    result.neighbor_edge_mask = (packed.x >> 14u) & 15u;
    result.morph_factor = asfloat(packed.y);
    result.atlas_slot = packed.z;
    result.high_resolution_weights = (packed.w & 1u) != 0u;
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
        component_coord.y * AshTerrainComponentCount + component_coord.x;
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
    const int max_sample = int(AshTerrainComponentCount * AshTerrainComponentQuads);
    const uint2 clamped_sample = uint2(clamp(global_sample, int2(0, 0), int2(max_sample, max_sample)));
    const uint2 component_coord = min(
        clamped_sample / AshTerrainComponentQuads,
        uint2(AshTerrainComponentCount - 1u, AshTerrainComponentCount - 1u));
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

    const bool west = local_sample.x == 0u &&
        (instance.neighbor_edge_mask & 1u) != 0u;
    const bool east = local_sample.x == AshTerrainComponentQuads &&
        (instance.neighbor_edge_mask & 2u) != 0u;
    const bool north = local_sample.y == 0u &&
        (instance.neighbor_edge_mask & 4u) != 0u;
    const bool south = local_sample.y == AshTerrainComponentQuads &&
        (instance.neighbor_edge_mask & 8u) != 0u;
    const float morph = (west || east || north || south) ?
        1.0 : saturate(instance.morph_factor);
    return lerp(fine_height, coarse_height, morph);
}

float3 AshTerrainLocalNormal(
    StructuredBuffer<uint> height_words,
    int2 global_sample,
    float height_offset,
    float height_range,
    float sample_spacing)
{
    const float west = AshTerrainLoadGlobalHeight(
        height_words, global_sample + int2(-1, 0), height_offset, height_range);
    const float east = AshTerrainLoadGlobalHeight(
        height_words, global_sample + int2(1, 0), height_offset, height_range);
    const float north = AshTerrainLoadGlobalHeight(
        height_words, global_sample + int2(0, -1), height_offset, height_range);
    const float south = AshTerrainLoadGlobalHeight(
        height_words, global_sample + int2(0, 1), height_offset, height_range);
    const float safe_spacing = max(sample_spacing, 1e-5);
    return normalize(float3(west - east, 2.0 * safe_spacing, north - south));
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
    return (global_sample / 8.0 + 0.5) / float(AshTerrainCoarseExtent);
}

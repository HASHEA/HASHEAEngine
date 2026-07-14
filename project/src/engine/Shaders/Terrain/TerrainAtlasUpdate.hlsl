// Incremental Terrain weight upload. The CPU provides one 257x257 component
// per frame through a raw buffer: RGBA layers 0..3 followed by RGBA layers 4..7.

cbuffer AshRootConstants : register(b0)
{
    uint2 AshTerrainAtlasOrigin;
    uint2 AshTerrainComponentCoord;
    uint AshTerrainWriteHighResolution;
    uint3 AshTerrainPadding;
};

ByteAddressBuffer TerrainWeightUpload : register(t0);
RWTexture2D<unorm float4> TerrainWeightAtlas0 : register(u0);
RWTexture2D<unorm float4> TerrainWeightAtlas1 : register(u1);
RWTexture2D<unorm float4> TerrainCoarseWeights : register(u2);

static const uint AshTerrainComponentSamples = 257u;
static const uint AshTerrainComponentQuads = 256u;
static const uint AshTerrainAtlasSlotExtent = 259u;
static const uint AshTerrainWeightLayerBytes =
    AshTerrainComponentSamples * AshTerrainComponentSamples * 4u;

float4 AshTerrainLoadRgba8(uint byte_offset)
{
    const uint packed = TerrainWeightUpload.Load(byte_offset);
    return float4(
        packed & 0xffu,
        (packed >> 8u) & 0xffu,
        (packed >> 16u) & 0xffu,
        (packed >> 24u) & 0xffu) * (1.0 / 255.0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (dispatch_id.x >= AshTerrainAtlasSlotExtent ||
        dispatch_id.y >= AshTerrainAtlasSlotExtent)
    {
        return;
    }

    const int2 source_with_gutter = int2(dispatch_id.xy) - int2(1, 1);
    const uint2 source = uint2(clamp(
        source_with_gutter,
        int2(0, 0),
        int2(AshTerrainComponentQuads, AshTerrainComponentQuads)));
    const uint source_linear = source.y * AshTerrainComponentSamples + source.x;
    const uint source_byte_offset = source_linear * 4u;
    const float4 weights0 = AshTerrainLoadRgba8(source_byte_offset);
    const float4 weights1 = AshTerrainLoadRgba8(
        AshTerrainWeightLayerBytes + source_byte_offset);

    if (AshTerrainWriteHighResolution != 0u)
    {
        const uint2 atlas_pixel = AshTerrainAtlasOrigin + dispatch_id.xy;
        TerrainWeightAtlas0[atlas_pixel] = weights0;
        TerrainWeightAtlas1[atlas_pixel] = weights1;
    }

    // A component owns its coarse samples except its +X/+Z boundary. The last
    // component owns the final boundary, yielding exactly 1025 samples per axis.
    const bool is_component_sample =
        dispatch_id.x >= 1u && dispatch_id.x <= AshTerrainComponentSamples &&
        dispatch_id.y >= 1u && dispatch_id.y <= AshTerrainComponentSamples;
    const bool on_coarse_grid =
        (source.x & 7u) == 0u && (source.y & 7u) == 0u;
    const bool owns_x =
        source.x < AshTerrainComponentQuads || AshTerrainComponentCoord.x == 31u;
    const bool owns_z =
        source.y < AshTerrainComponentQuads || AshTerrainComponentCoord.y == 31u;
    if (is_component_sample && on_coarse_grid && owns_x && owns_z)
    {
        const uint2 coarse_pixel =
            AshTerrainComponentCoord * 32u + source / 8u;
        TerrainCoarseWeights[coarse_pixel] = weights0;
    }
}

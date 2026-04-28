void CalculateVertexMainNode(in AshVertexParameters params, inout AshVertexMainNode node)
{
    node.world_position_offset = float3(0.0, 0.0, 0.0);
}

void CalculatePixelMainNode(in AshPixelParameters params, inout AshPixelMainNode node)
{
    const float4 base_sample = BaseColorTexture.Sample(ASH_SurfacePBRSampler, params.uv0);
    const float4 normal_sample = NormalTexture.Sample(ASH_SurfacePBRSampler, params.uv0);
    const float4 metallic_roughness_sample = MetallicRoughnessTexture.Sample(ASH_SurfacePBRSampler, params.uv0);
    const float4 emissive_sample = EmissiveTexture.Sample(ASH_SurfacePBRSampler, params.uv0);

    node.base_color = base_sample.rgb * BaseColorFactor.rgb;
    node.opacity = base_sample.a * BaseColorFactor.a;
    node.opacity_mask = node.opacity;
    node.normal_ts = normalize(float3(
        normal_sample.xy * 2.0f - 1.0f,
        normal_sample.z * 2.0f - 1.0f));
    node.metallic = saturate(Metallic * metallic_roughness_sample.b);
    node.roughness = saturate(Roughness * metallic_roughness_sample.g);
    node.emissive = emissive_sample.rgb * EmissiveColor.rgb;
    node.ambient_occlusion = 1.0f;
}

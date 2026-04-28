void CalculateVertexMainNode(in AshVertexParameters params, inout AshVertexMainNode node)
{
    node.world_position_offset = float3(0.0, 0.0, 0.0);
}

void CalculatePixelMainNode(in AshPixelParameters params, inout AshPixelMainNode node)
{
    float4 albedo = BaseColorTex.Sample(WrapLinear, params.uv0);

#if ASH_HAS_VERTEX_COLOR
    const float3 vertex_color = params.vertex_color.rgb;
    const float vertex_alpha = params.vertex_color.a;
#else
    const float3 vertex_color = float3(1.0, 1.0, 1.0);
    const float vertex_alpha = 1.0;
#endif

    node.base_color = albedo.rgb * BaseColorTint.rgb * vertex_color;
    node.opacity = albedo.a * BaseColorTint.a * vertex_alpha;
    node.opacity_mask = node.opacity;
    node.normal_ts = float3(0.0, 0.0, 1.0);
    node.metallic = 0.0;
    node.roughness = RoughnessScale;
    node.emissive = float3(0.0, 0.0, 0.0);
    node.ambient_occlusion = 1.0;
}

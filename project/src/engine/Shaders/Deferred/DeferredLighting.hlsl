Texture2D<float4> SceneGBufferA : register(t0);
Texture2D<float4> SceneGBufferB : register(t1);
Texture2D<float4> SceneGBufferC : register(t2);
Texture2D<float4> SceneGBufferD : register(t3);
Texture2D<float4> SceneGBufferE : register(t4);
Texture2D<float> SceneDepth : register(t5);
SamplerState SceneLinearClampSampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    VSOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = uvs[vertex_id];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const float4 gbuffer_a = SceneGBufferA.Sample(SceneLinearClampSampler, input.uv);
    const float4 gbuffer_b = SceneGBufferB.Sample(SceneLinearClampSampler, input.uv);
    const float4 gbuffer_e = SceneGBufferE.Sample(SceneLinearClampSampler, input.uv);

    const float3 base_color = saturate(gbuffer_a.rgb);
    const float roughness = saturate(gbuffer_b.g);
    const float ao = saturate(gbuffer_b.b);
    const float3 emissive = float3(gbuffer_e.b, gbuffer_e.a, 0.0);
    const float diffuse = lerp(0.75, 1.0, 1.0 - roughness);
    return float4(saturate(base_color * diffuse * ao + emissive), 1.0);
}

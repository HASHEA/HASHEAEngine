Texture2D<float4> SourceTexture;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float2 kFullscreenPositions[4] =
{
    float2(-1.0, -1.0),
    float2( 1.0, -1.0),
    float2(-1.0,  1.0),
    float2( 1.0,  1.0)
};

static const float2 kFullscreenUVs[4] =
{
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(0.0, 0.0),
    float2(1.0, 0.0)
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.position = float4(kFullscreenPositions[vertexID], 0.0, 1.0);
    output.uv = kFullscreenUVs[vertexID];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    uint width;
    uint height;
    SourceTexture.GetDimensions(width, height);
    float2 clamped_uv = saturate(input.uv);
    uint2 pixel = min(uint2(clamped_uv * float2(width, height)), uint2(width - 1, height - 1));
    float4 color = SourceTexture.Load(int3(pixel, 0));
#if PRESENT_OUTPUT_IS_SRGB
    return color;
#else
    return float4(pow(saturate(color.rgb), 1.0 / 2.2), color.a);
#endif
}

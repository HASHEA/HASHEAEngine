struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

StructuredBuffer<float4> PaletteBuffer;
RWTexture2D<float4> OutputTexture;
Texture2D<float4> LogoTexture;

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

float sdRoundedBox(float2 p, float2 halfExtents, float radius)
{
    float2 q = abs(p) - halfExtents + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float2 rotate2D(float2 p, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(c * p.x - s * p.y, s * p.x + c * p.y);
}

float capsuleMask(float2 p, float2 halfExtents, float radius, float blur)
{
    float dist = sdRoundedBox(p, halfExtents, radius);
    return 1.0 - smoothstep(0.0, blur, dist);
}

float hexRingMask(float2 p, float radius, float thickness, float blur)
{
    float angle = atan2(p.y, p.x);
    float sector = 3.14159265 / 3.0;
    float localAngle = fmod(angle + sector * 0.5, sector) - sector * 0.5;
    float dist = cos(localAngle) * length(p) - radius;
    float outer = 1.0 - smoothstep(0.0, blur, dist);
    float inner = 1.0 - smoothstep(0.0, blur, dist + thickness);
    return saturate(outer - inner);
}

float logoMask(float2 p)
{
    float mask = 0.0;

    float hex = hexRingMask(p, 0.47, 0.12, 0.012);
    mask = max(mask, hex);

    float2 branchCenters[6] =
    {
        float2( 0.00,  0.41),
        float2( 0.35,  0.20),
        float2( 0.35, -0.20),
        float2( 0.00, -0.41),
        float2(-0.35, -0.20),
        float2(-0.35,  0.20)
    };

    float branchAngles[6] =
    {
        0.0,
        1.04719755,
        -1.04719755,
        0.0,
        1.04719755,
        -1.04719755
    };

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float2 local = rotate2D(p - branchCenters[i], branchAngles[i]);
        float branch = capsuleMask(local, float2(0.13, 0.055), 0.05, 0.010);
        mask = max(mask, branch);
    }

    float2 crossA = rotate2D(p, 0.78539816);
    float2 crossB = rotate2D(p, -0.78539816);
    mask = max(mask, capsuleMask(crossA, float2(0.20, 0.040), 0.035, 0.010) * 0.85);
    mask = max(mask, capsuleMask(crossB, float2(0.20, 0.040), 0.035, 0.010) * 0.85);

    float innerHole = 1.0 - capsuleMask(p, float2(0.11, 0.11), 0.09, 0.008);
    return saturate(mask * innerHole);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width;
    uint height;
    OutputTexture.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
    {
        return;
    }

    float2 uv = (float2(dispatchThreadID.xy) + 0.5) / float2(width, height);
    float2 p = uv * 2.0 - 1.0;
    p.x *= (float)width / (float)height;

    float vignette = saturate(1.0 - dot(p * 0.62, p * 0.62));
    float3 backgroundA = PaletteBuffer[0].rgb;
    float3 backgroundB = PaletteBuffer[1].rgb;
    float3 logoColor = PaletteBuffer[2].rgb;
    float3 highlightColor = PaletteBuffer[3].rgb;

    float3 background = lerp(backgroundA, backgroundB, saturate(uv.y * 0.75 + 0.15));
    background += 0.06 * vignette;

    float glow = exp(-4.2 * dot(p, p));
    float mask = logoMask(p * 1.05);
    float glowMask = logoMask(p * 1.20) * glow;

    float3 color = background;
    color += glowMask * float3(0.020, 0.180, 0.160);
    color = lerp(color, logoColor, mask);
    color += highlightColor * (mask * glow * 0.24);

    OutputTexture[dispatchThreadID.xy] = float4(saturate(color), 1.0);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    uint width;
    uint height;
    LogoTexture.GetDimensions(width, height);
    float2 clampedUV = saturate(input.uv);
    uint2 pixel = min(uint2(clampedUV * float2(width, height)), uint2(width - 1, height - 1));
    return LogoTexture.Load(int3(pixel, 0));
}

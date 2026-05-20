#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

Texture2D<float4> RenderDebugInput : register(t0);
SamplerState ScenePointClampSampler : register(s0);

struct VSFullscreenOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer AshRootConstants : register(b0)
{
    float4 AshDebugViewParams0;
    float4 AshDebugViewParams1;
};

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
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

    VSFullscreenOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = uvs[vertex_id];
    return output;
}

float3 AshDebugVisualizeLinearHDR(float3 hdr)
{
    return max(hdr, 0.0.xxx);
}

float3 AshDebugLinearToSRGB(float3 lin)
{
    lin = max(lin, 0.0);
    return lerp(
        1.055 * pow(lin, 1.0 / 2.4) - 0.055,
        12.92 * lin,
        step(lin, 0.0031308));
}

float3 AshDebugDecodeNormalOct(float2 encoded)
{
    float2 f = encoded * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += lerp(t.xx, -t.xx, step(float2(0.0, 0.0), n.xy));
    return normalize(n);
}

float3 AshDebugVisualizeDepth(float depth, bool reverse_z)
{
    const bool background = reverse_z ? depth <= 0.000001 : depth >= 0.999999;
    if (background)
    {
        return 0.0.xxx;
    }
    const float remapped = reverse_z ? depth : (1.0 - depth);
    return saturate(remapped).xxx;
}

float4 PSMain(VSFullscreenOutput input) : SV_Target0
{
    const uint mode = (uint)round(AshDebugViewParams0.x);
    const bool reverse_z = AshDebugViewParams0.y > 0.5;
    const bool manual_srgb = AshDebugViewParams0.z > 0.5;
    const float motion_scale = max(AshDebugViewParams0.w, 1.0);

    const float4 value = RenderDebugInput.SampleLevel(ScenePointClampSampler, input.uv, 0);
    float3 color = saturate(value.rgb);

    if (mode == 1u)
    {
        color = AshDebugVisualizeLinearHDR(value.rgb);
    }
    else if (mode == 2u)
    {
        color = AshDebugVisualizeDepth(value.r, reverse_z);
    }
    else if (mode == 3u)
    {
        const float3 normal = AshDebugDecodeNormalOct(value.rg);
        color = normal * 0.5 + 0.5;
    }
    else if (mode == 4u)
    {
        color = float3(0.5 + value.x * motion_scale, 0.5 - value.y * motion_scale, saturate(value.w));
    }
    else if (mode == 5u || mode == 6u)
    {
        color = saturate(value.r).xxx;
    }

    if (manual_srgb)
    {
        color = AshDebugLinearToSRGB(color);
    }

    return float4(saturate(color), 1.0);
}

Texture2D<float> SceneDepth : register(t0);
Texture2D<float4> SceneGBufferE : register(t1);
Texture2D<float4> SceneAmbientOcclusionInput : register(t2);
SamplerState ScenePointClampSampler : register(s0);

struct VSFullscreenOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshInvViewProjection;
    float4 AshViewportSize;
    float4 AshCameraPositionAndFlags;
    float4 AshAOParams0;
    float4 AshAOParams1;
};

VSFullscreenOutput VSFullscreen(uint vertex_id : SV_VertexID)
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

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

bool AshAOIsReverseZ()
{
    return AshCameraPositionAndFlags.w > 0.5;
}

bool AshAOSceneDepthIsBackground(float depth)
{
    return AshAOIsReverseZ() ? depth <= 0.000001 : depth >= 0.999999;
}

float3 AshAODecodeNormalOct(float2 encoded)
{
    float2 f = encoded * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += lerp(t.xx, -t.xx, step(0.0.xx, n.xy));
    return normalize(n);
}

float3 AshAOReconstructWorldPosition(float2 uv, float depth)
{
    const float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
    const float4 world = mul(AshInvViewProjection, clip);
    return world.xyz / max(world.w, 1e-6);
}

struct AshAOSurface
{
    bool valid;
    float depth;
    float3 position_ws;
    float3 normal_ws;
};

AshAOSurface AshAOLoadSurface(float2 uv)
{
    AshAOSurface surface;
    surface.valid = false;
    surface.depth = SceneDepth.SampleLevel(ScenePointClampSampler, uv, 0);
    surface.position_ws = 0.0.xxx;
    surface.normal_ws = float3(0.0, 0.0, 1.0);
    if (AshAOSceneDepthIsBackground(surface.depth))
    {
        return surface;
    }

    const float4 gbuffer_e = SceneGBufferE.SampleLevel(ScenePointClampSampler, uv, 0);
    surface.position_ws = AshAOReconstructWorldPosition(uv, surface.depth);
    surface.normal_ws = AshAODecodeNormalOct(gbuffer_e.rg);
    surface.valid = true;
    return surface;
}

float AshAOViewScaledRadiusUv(AshAOSurface surface)
{
    const float radius = max(AshAOParams0.x, 0.05);
    const float view_distance = max(length(surface.position_ws - AshCameraPositionAndFlags.xyz), 0.5);
    return saturate(radius / view_distance) * 0.25;
}

float AshAOApplyCurve(float occlusion)
{
    const float intensity = max(AshAOParams0.y, 0.0);
    const float power_value = max(AshAOParams0.z, 0.05);
    return pow(saturate(1.0 - occlusion * intensity), power_value);
}

float AshAOContribution(AshAOSurface center, AshAOSurface sample_surface)
{
    if (!sample_surface.valid)
    {
        return 0.0;
    }

    const float3 delta = sample_surface.position_ws - center.position_ws;
    const float distance_ws = length(delta);
    const float radius = max(AshAOParams0.x, 0.05);
    if (distance_ws <= 0.0001 || distance_ws > radius)
    {
        return 0.0;
    }

    const float3 direction_ws = delta / distance_ws;
    const float facing = saturate(dot(center.normal_ws, direction_ws) - AshAOParams1.w);
    const float falloff = saturate(1.0 - distance_ws / radius);
    return facing * falloff;
}

float2 AshAORotate(float2 value, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(value.x * c - value.y * s, value.x * s + value.y * c);
}

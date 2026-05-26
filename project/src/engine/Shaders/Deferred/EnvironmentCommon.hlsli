cbuffer AshRootConstants : register(b0)
{
    float4x4 AshInvViewProjection;
    float4 AshCameraPositionAndFlags;
    float4 AshEnvironmentParams;
};

struct VSFullscreenOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
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

bool AshIsReverseZ()
{
    return AshCameraPositionAndFlags.w > 0.5;
}

bool AshSceneDepthIsBackground(float depth)
{
    return AshIsReverseZ() ? depth <= 0.000001 : depth >= 0.999999;
}

float3 AshReconstructWorldPosition(float2 uv, float depth)
{
    const float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
    const float4 world = mul(AshInvViewProjection, clip);
    return world.xyz / max(world.w, 1e-6);
}

float3 AshRotateEnvironmentDirection(float3 direction_ws, float rotation_radians)
{
    const float s = sin(rotation_radians);
    const float c = cos(rotation_radians);
    return float3(
        c * direction_ws.x - s * direction_ws.z,
        direction_ws.y,
        s * direction_ws.x + c * direction_ws.z);
}

float3 AshFresnelSchlickRoughness(float cos_theta, float3 f0, float roughness)
{
    return f0 + (max(1.0.xxx - roughness.xxx, f0) - f0) * pow(saturate(1.0 - cos_theta), 5.0);
}

float3 AshDiffuseLambert(float3 base_color)
{
    return base_color * 0.31830988618;
}

float AshEnvironmentIntensity()
{
    return AshEnvironmentParams.y;
}

float AshEnvironmentRotationRadians()
{
    return AshEnvironmentParams.x;
}

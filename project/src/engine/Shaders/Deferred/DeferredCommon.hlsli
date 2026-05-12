#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

Texture2D<float4> SceneGBufferA : register(t0);
Texture2D<float4> SceneGBufferB : register(t1);
Texture2D<float4> SceneGBufferC : register(t2);
Texture2D<float4> SceneGBufferD : register(t3);
Texture2D<float4> SceneGBufferE : register(t4);
Texture2D<float> SceneDepth : register(t5);
Texture2D<float4> SceneLightingAccum : register(t6);
SamplerState ScenePointClampSampler : register(s0);

static const uint ASH_SHADING_MODEL_EMPTY = 0;
static const uint ASH_SHADING_MODEL_DEFAULT_LIT_GGX = 1;
static const uint ASH_SHADING_MODEL_UNLIT = 2;
static const uint ASH_SHADING_MODEL_BLINN_PHONG = 3;

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

struct VSVolumeInput
{
    ASH_MESH_VERTEX_POSITION_ATTR float3 position_os : POSITION;
};

struct VSVolumeOutput
{
    float4 position : SV_Position;
};

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshInvViewProjection;
    float4x4 AshLightWorldToClip;
    float4 AshViewportSize;
    float4 AshCameraPositionAndFlags;
    float4 AshLightPositionAndRange;
    float4 AshLightDirectionAndIntensity;
    float4 AshLightColorAndType;
    float4 AshLightConeCos;
};

VSVolumeOutput VSVolume(VSVolumeInput input)
{
    VSVolumeOutput output;
    output.position = mul(AshLightWorldToClip, float4(input.position_os, 1.0));
    return output;
}

uint AshDecodeShadingModelId(float encoded)
{
    return (uint)round(saturate(encoded) * 255.0);
}

float3 AshDecodeNormalOct(float2 encoded)
{
	float2 f = encoded * 2.0 - 1.0;
	float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = saturate(-n.z);
	n.xy += lerp(t.xx, -t.xx, step(0.0.xx, n.xy));
	return normalize(n);
}

float3 AshReconstructWorldPosition(float2 uv, float depth)
{
    const float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
    const float4 world = mul(AshInvViewProjection, clip);
    return world.xyz / max(world.w, 1e-6);
}

struct AshDeferredSurface
{
    bool valid;
    uint shading_model;
    float depth;
    float3 position_ws;
    float3 normal_ws;
    float3 base_color;
    float metallic;
    float roughness;
    float ao;
    float specular_scalar;
    float3 specular_color;
    float3 emissive;
};

AshDeferredSurface AshDecodeDeferredSurface(float2 uv)
{
    AshDeferredSurface surface;
    surface.valid = false;
    surface.shading_model = ASH_SHADING_MODEL_EMPTY;
    surface.depth = SceneDepth.Sample(ScenePointClampSampler, uv);
    surface.position_ws = 0.0.xxx;
    surface.normal_ws = float3(0.0, 0.0, 1.0);
    surface.base_color = 0.0.xxx;
    surface.metallic = 0.0;
    surface.roughness = 1.0;
    surface.ao = 1.0;
    surface.specular_scalar = 0.5;
    surface.specular_color = 0.04.xxx;
    surface.emissive = 0.0.xxx;

    if (surface.depth >= 0.999999)
    {
        return surface;
    }

    const float4 gbuffer_a = SceneGBufferA.Sample(ScenePointClampSampler, uv);
    surface.shading_model = AshDecodeShadingModelId(gbuffer_a.a);
    if (surface.shading_model == ASH_SHADING_MODEL_EMPTY)
    {
        return surface;
    }

    const float4 gbuffer_b = SceneGBufferB.Sample(ScenePointClampSampler, uv);
    const float4 gbuffer_c = SceneGBufferC.Sample(ScenePointClampSampler, uv);
    const float4 gbuffer_e = SceneGBufferE.Sample(ScenePointClampSampler, uv);
    surface.position_ws = AshReconstructWorldPosition(uv, surface.depth);
    surface.normal_ws = AshDecodeNormalOct(gbuffer_e.rg);
    surface.base_color = saturate(gbuffer_a.rgb);
    surface.metallic = saturate(gbuffer_b.r);
    surface.roughness = max(saturate(gbuffer_b.g), 0.045);
    surface.ao = saturate(gbuffer_b.b);
    surface.specular_scalar = saturate(gbuffer_b.a);
    surface.specular_color = max(gbuffer_c.rgb, lerp(0.04.xxx * surface.specular_scalar, surface.base_color, surface.metallic));
    surface.emissive = float3(gbuffer_e.b, gbuffer_e.a, 0.0);
    surface.valid = true;
    return surface;
}

float3 AshDiffuseLambert(float3 base_color)
{
    return base_color * 0.31830988618;
}

float AshDistributionGGX(float n_dot_h, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denom = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom * denom, 1e-5);
}

float AshGeometrySchlickGGX(float n_dot_v, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return n_dot_v / max(n_dot_v * (1.0 - k) + k, 1e-5);
}

float3 AshFresnelSchlick(float cos_theta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cos_theta), 5.0);
}

float3 AshEvaluateDefaultLitGGX(AshDeferredSurface surface, float3 light_dir_ws, float3 light_radiance)
{
    const float3 n = normalize(surface.normal_ws);
    const float3 v = normalize(AshCameraPositionAndFlags.xyz - surface.position_ws);
    const float3 l = normalize(light_dir_ws);
    const float3 h = normalize(v + l);
    const float n_dot_l = saturate(dot(n, l));
    const float n_dot_v = saturate(dot(n, v));
    const float n_dot_h = saturate(dot(n, h));
    const float h_dot_v = saturate(dot(h, v));
    if (n_dot_l <= 0.0 || n_dot_v <= 0.0)
    {
        return 0.0.xxx;
    }

    const float3 f0 = surface.specular_color;
    const float3 f = AshFresnelSchlick(h_dot_v, f0);
    const float d = AshDistributionGGX(n_dot_h, surface.roughness);
    const float g = AshGeometrySchlickGGX(n_dot_v, surface.roughness) * AshGeometrySchlickGGX(n_dot_l, surface.roughness);
    const float3 specular = (d * g * f) / max(4.0 * n_dot_v * n_dot_l, 1e-4);
    const float3 kd = (1.0 - f) * (1.0 - surface.metallic);
    return (kd * AshDiffuseLambert(surface.base_color) + specular) * light_radiance * n_dot_l * surface.ao;
}

float3 AshEvaluateBlinnPhong(AshDeferredSurface surface, float3 light_dir_ws, float3 light_radiance)
{
    const float3 n = normalize(surface.normal_ws);
    const float3 v = normalize(AshCameraPositionAndFlags.xyz - surface.position_ws);
    const float3 l = normalize(light_dir_ws);
    const float3 h = normalize(v + l);
    const float n_dot_l = saturate(dot(n, l));
    const float n_dot_h = saturate(dot(n, h));
    const float shininess = lerp(96.0, 8.0, surface.roughness);
    const float specular = pow(n_dot_h, shininess) * surface.specular_scalar;
    return (surface.base_color * n_dot_l + specular.xxx) * light_radiance * surface.ao;
}

float3 AshEvaluateDynamicLight(AshDeferredSurface surface, float3 light_dir_ws, float3 light_radiance)
{
    if (!surface.valid || surface.shading_model == ASH_SHADING_MODEL_EMPTY || surface.shading_model == ASH_SHADING_MODEL_UNLIT)
    {
        return 0.0.xxx;
    }
    if (surface.shading_model == ASH_SHADING_MODEL_BLINN_PHONG)
    {
        return AshEvaluateBlinnPhong(surface, light_dir_ws, light_radiance);
    }
    return AshEvaluateDefaultLitGGX(surface, light_dir_ws, light_radiance);
}

float3 AshEvaluateBaseEmissive(AshDeferredSurface surface)
{
    if (!surface.valid)
    {
        return 0.0.xxx;
    }
    if (surface.shading_model == ASH_SHADING_MODEL_UNLIT)
    {
        return surface.base_color * surface.ao + surface.emissive;
    }
    return surface.emissive;
}

float AshRangeAttenuation(float distance, float range)
{
    const float distance_ratio = saturate(distance / max(range, 1e-4));
    const float smooth_range = saturate(1.0 - distance_ratio * distance_ratio);
    return smooth_range * smooth_range / max(distance * distance, 0.01);
}

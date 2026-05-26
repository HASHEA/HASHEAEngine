#include "EnvironmentCommon.hlsli"

Texture2D<float4> SceneGBufferA : register(t0);
Texture2D<float4> SceneGBufferB : register(t1);
Texture2D<float4> SceneGBufferC : register(t2);
Texture2D<float4> SceneGBufferD : register(t3);
Texture2D<float4> SceneGBufferE : register(t4);
Texture2D<float> SceneDepth : register(t5);
Texture2D<float4> SceneAmbientOcclusion : register(t8);
TextureCube<float4> SceneEnvironmentIrradiance : register(t9);
TextureCube<float4> SceneEnvironmentPrefilteredSpecular : register(t10);
Texture2D<float2> SceneEnvironmentBRDFLUT : register(t11);
SamplerState ScenePointClampSampler : register(s0);
SamplerState SceneEnvironmentSampler : register(s1);

static const uint ASH_SHADING_MODEL_EMPTY = 0;
static const uint ASH_SHADING_MODEL_DEFAULT_LIT_GGX = 1;
static const uint ASH_SHADING_MODEL_UNLIT = 2;
static const uint ASH_SHADING_MODEL_BLINN_PHONG = 3;

struct PSOutput
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

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
    float material_ao;
    float screen_ao;
    float ao;
    float specular_scalar;
    float3 specular_color;
    float3 emissive;
};

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
    surface.material_ao = 1.0;
    surface.screen_ao = 1.0;
    surface.ao = 1.0;
    surface.specular_scalar = 0.5;
    surface.specular_color = 0.04.xxx;
    surface.emissive = 0.0.xxx;

    if (AshSceneDepthIsBackground(surface.depth))
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
    surface.material_ao = saturate(gbuffer_b.b);
    surface.screen_ao = saturate(SceneAmbientOcclusion.Sample(ScenePointClampSampler, uv).r);
    surface.ao = surface.material_ao * surface.screen_ao;
    surface.specular_scalar = saturate(gbuffer_b.a);
    surface.specular_color = max(gbuffer_c.rgb, lerp(0.04.xxx * surface.specular_scalar, surface.base_color, surface.metallic));
    surface.emissive = float3(gbuffer_e.b, gbuffer_e.a, 0.0);
    surface.valid = true;
    return surface;
}

VSFullscreenOutput VSMain(uint vertex_id : SV_VertexID)
{
    return VSFullscreen(vertex_id);
}

PSOutput PSMain(VSFullscreenOutput input)
{
    PSOutput output;
    output.diffuse = 0.0.xxxx;
    output.specular = 0.0.xxxx;

    AshDeferredSurface surface = AshDecodeDeferredSurface(input.uv);
    if (!surface.valid ||
        surface.shading_model == ASH_SHADING_MODEL_EMPTY ||
        surface.shading_model == ASH_SHADING_MODEL_UNLIT)
    {
        return output;
    }

    const float3 n = normalize(surface.normal_ws);
    const float3 v = normalize(AshCameraPositionAndFlags.xyz - surface.position_ws);
    const float n_dot_v = saturate(dot(n, v));
    const float3 f0 = surface.specular_color;
    const float3 f = AshFresnelSchlickRoughness(n_dot_v, f0, surface.roughness);
    const float3 kd = (1.0 - f) * (1.0 - surface.metallic);
    const float intensity = AshEnvironmentIntensity();
    const float rotation_radians = AshEnvironmentRotationRadians();

    const float3 irradiance_n = AshRotateEnvironmentDirection(n, rotation_radians);
    const float3 irradiance = SceneEnvironmentIrradiance.Sample(SceneEnvironmentSampler, irradiance_n).rgb;
    const float3 diffuse_ibl = irradiance * AshDiffuseLambert(surface.base_color) * kd * surface.ao * intensity;

    const float3 r = normalize(reflect(-v, n));
    const float3 prefilter_dir = AshRotateEnvironmentDirection(r, rotation_radians);
    uint prefilter_width = 0;
    uint prefilter_height = 0;
    uint prefilter_mip_count = 0;
    SceneEnvironmentPrefilteredSpecular.GetDimensions(0, prefilter_width, prefilter_height, prefilter_mip_count);
    const float mip_level = surface.roughness * max((float)prefilter_mip_count - 1.0, 0.0);
    const float3 prefiltered_color =
        SceneEnvironmentPrefilteredSpecular.SampleLevel(SceneEnvironmentSampler, prefilter_dir, mip_level).rgb;
    const float2 env_brdf = SceneEnvironmentBRDFLUT.Sample(SceneEnvironmentSampler, float2(n_dot_v, surface.roughness)).rg;
    const float3 specular_ibl = prefiltered_color * (f * env_brdf.x + env_brdf.y) * surface.ao * intensity;

    output.diffuse = float4(diffuse_ibl, 1.0);
    output.specular = float4(specular_ibl, 1.0);
    return output;
}

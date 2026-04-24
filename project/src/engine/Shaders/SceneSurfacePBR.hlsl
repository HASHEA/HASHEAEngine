#include "../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct VSInput
{
    ASH_MESH_VERTEX_POSITION_ATTR float3 position : POSITION;
    ASH_MESH_VERTEX_NORMAL_ATTR float3 normal : NORMAL;
    ASH_MESH_VERTEX_TANGENT_ATTR float4 tangent : TANGENT;
    ASH_MESH_VERTEX_TEXCOORD0_ATTR float2 uv0 : TEXCOORD0;
    ASH_MESH_VERTEX_TEXCOORD1_ATTR float2 uv1 : TEXCOORD1;
    ASH_MESH_VERTEX_COLOR0_ATTR float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
    float4 color : TEXCOORD3;
};

cbuffer AshRootConstants
{
    float4x4 ObjectToClip;
};

cbuffer MaterialUniforms : register(b1)
{
    float4 BaseColorFactor;
    float4 EmissiveFactorAndAlphaCutoff;
    float4 MetallicRoughnessAndFlags;
    float4 TextureFlags;
};

Texture2D<float4> BaseColorTexture : register(t0);
Texture2D<float4> NormalTexture : register(t1);
Texture2D<float4> MetallicRoughnessTexture : register(t2);
Texture2D<float4> EmissiveTexture : register(t3);

#ifndef ASH_PBR_SAMPLER_COUNT
#define ASH_PBR_SAMPLER_COUNT 1
#endif

#ifndef ASH_SAMPLER_0_NAME
#define ASH_SAMPLER_0_NAME ASH_DefaultSampler
#endif

#ifndef ASH_BASE_COLOR_SAMPLER_NAME
#define ASH_BASE_COLOR_SAMPLER_NAME ASH_SAMPLER_0_NAME
#endif

#ifndef ASH_NORMAL_SAMPLER_NAME
#define ASH_NORMAL_SAMPLER_NAME ASH_SAMPLER_0_NAME
#endif

#ifndef ASH_METALLIC_ROUGHNESS_SAMPLER_NAME
#define ASH_METALLIC_ROUGHNESS_SAMPLER_NAME ASH_SAMPLER_0_NAME
#endif

#ifndef ASH_EMISSIVE_SAMPLER_NAME
#define ASH_EMISSIVE_SAMPLER_NAME ASH_SAMPLER_0_NAME
#endif

#define ASH_DECLARE_SURFACE_SAMPLER(slot, name) SamplerState name : register(s##slot);

ASH_DECLARE_SURFACE_SAMPLER(0, ASH_SAMPLER_0_NAME)

#if ASH_PBR_SAMPLER_COUNT > 1
#ifndef ASH_SAMPLER_1_NAME
#error ASH_SAMPLER_1_NAME must be defined when ASH_PBR_SAMPLER_COUNT > 1
#endif
ASH_DECLARE_SURFACE_SAMPLER(1, ASH_SAMPLER_1_NAME)
#endif

#if ASH_PBR_SAMPLER_COUNT > 2
#ifndef ASH_SAMPLER_2_NAME
#error ASH_SAMPLER_2_NAME must be defined when ASH_PBR_SAMPLER_COUNT > 2
#endif
ASH_DECLARE_SURFACE_SAMPLER(2, ASH_SAMPLER_2_NAME)
#endif

#if ASH_PBR_SAMPLER_COUNT > 3
#ifndef ASH_SAMPLER_3_NAME
#error ASH_SAMPLER_3_NAME must be defined when ASH_PBR_SAMPLER_COUNT > 3
#endif
ASH_DECLARE_SURFACE_SAMPLER(3, ASH_SAMPLER_3_NAME)
#endif

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(ObjectToClip, float4(input.position, 1.0));
    output.normal = input.normal;
    output.tangent = input.tangent;
    output.uv0 = input.uv0;
    output.color = input.color;
    return output;
}

float3 sample_surface_normal(VSOutput input)
{
    float3 normal = normalize(input.normal);
    if (TextureFlags.y > 0.5)
    {
        float3 tangent = normalize(input.tangent.xyz);
        float tangentSign = input.tangent.w >= 0.0 ? 1.0 : -1.0;
        float3 bitangent = normalize(cross(normal, tangent) * tangentSign);
        float3 tangentSpaceNormal = NormalTexture.Sample(ASH_NORMAL_SAMPLER_NAME, input.uv0).xyz * 2.0 - 1.0;
        normal = normalize(
            tangentSpaceNormal.x * tangent +
            tangentSpaceNormal.y * bitangent +
            tangentSpaceNormal.z * normal);
    }
    return normal;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    float4 baseSample = float4(1.0, 1.0, 1.0, 1.0);
    if (TextureFlags.x > 0.5)
    {
        baseSample = BaseColorTexture.Sample(ASH_BASE_COLOR_SAMPLER_NAME, input.uv0);
    }

    float3 emissiveSample = float3(1.0, 1.0, 1.0);
    if (TextureFlags.w > 0.5)
    {
        emissiveSample = EmissiveTexture.Sample(ASH_EMISSIVE_SAMPLER_NAME, input.uv0).rgb;
    }

    float2 metallicRoughnessSample = float2(1.0, 1.0);
    if (TextureFlags.z > 0.5)
    {
        float4 packedSample = MetallicRoughnessTexture.Sample(ASH_METALLIC_ROUGHNESS_SAMPLER_NAME, input.uv0);
        metallicRoughnessSample = float2(packedSample.b, packedSample.g);
    }

    float4 baseColor = saturate(BaseColorFactor * baseSample * input.color);
    if (EmissiveFactorAndAlphaCutoff.w >= 0.0 && baseColor.a < EmissiveFactorAndAlphaCutoff.w)
    {
        discard;
    }

    float metallic = saturate(MetallicRoughnessAndFlags.x * metallicRoughnessSample.x);
    float roughness = saturate(MetallicRoughnessAndFlags.y * metallicRoughnessSample.y);
    float3 emissive = EmissiveFactorAndAlphaCutoff.rgb * emissiveSample;

    float3 normal = sample_surface_normal(input);
    float3 lightDir = normalize(float3(0.35, 0.55, 0.75));
    float3 viewDir = normalize(float3(0.0, 0.0, 1.0));
    float3 halfVector = normalize(lightDir + viewDir);

    float nDotL = saturate(dot(normal, lightDir));
    float nDotH = saturate(dot(normal, halfVector));
    float diffuse = 0.18 + 0.82 * nDotL;
    float specularPower = lerp(96.0, 8.0, roughness);
    float specular = pow(nDotH, specularPower) * lerp(0.04, 1.0, metallic);

    float3 litColor = baseColor.rgb * diffuse;
    litColor += specular.xxx * lerp(0.20, 0.55, metallic);
    litColor += emissive;

    return float4(saturate(litColor), baseColor.a);
}

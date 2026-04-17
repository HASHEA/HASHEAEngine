struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
};

cbuffer AshRootConstants
{
    float4x4 ObjectToClip;
    float4 BaseColorFactor;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(ObjectToClip, float4(input.position, 1.0));
    output.normal = input.normal;
    output.uv = input.uv0;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    float3 normal = normalize(abs(input.normal));
    float3 lit = 0.25.xxx + normal * 0.75;
    float3 albedo = saturate(BaseColorFactor.rgb * input.color.rgb);
    return float4(albedo * lit, BaseColorFactor.a * input.color.a);
}

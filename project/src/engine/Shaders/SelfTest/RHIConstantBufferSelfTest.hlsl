cbuffer ComputeConstants : register(b0)
{
    uint4 ComputeRgba;
};

RWByteAddressBuffer ComputeResultUAV : register(u0);

[numthreads(1, 1, 1)]
void CSMain()
{
    ComputeResultUAV.Store4(0, ComputeRgba);
}

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    VSOutput output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

cbuffer FragmentConstants : register(b1)
{
    uint4 FragmentRgba;
};

ByteAddressBuffer ComputeResultSRV : register(t0);

float4 PSMain(VSOutput input) : SV_Target0
{
    const uint4 rgba = input.position.x < 32.0 ? ComputeResultSRV.Load4(0) : FragmentRgba;
    return float4(rgba) / 255.0;
}

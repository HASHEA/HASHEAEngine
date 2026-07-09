// RHI indirect draw substrate self-test (SDD-2026-07-09-indirect-draw-substrate).
// Args layouts must match Ash*IndirectArgs structs in RHICommon.h.
RWByteAddressBuffer SelfTestArgs : register(u0);

[numthreads(1, 1, 1)]
void CSWriteDispatchArgs()
{
    // AshDispatchIndirectArgs { 1, 1, 1 }
    SelfTestArgs.Store3(0, uint3(1u, 1u, 1u));
}

[numthreads(1, 1, 1)]
void CSWriteDrawArgs()
{
    // offset 0: AshDrawIndirectArgs { vertexCount=3, instanceCount=1, firstVertex=0, firstInstance=0 }
    SelfTestArgs.Store4(0, uint4(3u, 1u, 0u, 0u));
    // offset 16: AshDrawIndexedIndirectArgs { indexCount=3, instanceCount=1, firstIndex=0, vertexOffset=0, firstInstance=0 }
    SelfTestArgs.Store4(16, uint4(3u, 1u, 0u, 0u));
    SelfTestArgs.Store(32, 0u);
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

float4 PSMain(VSOutput input) : SV_Target0
{
    // Additive blend: the non-indexed and indexed indirect draws each add 100/255 green.
    return float4(0.0, 100.0 / 255.0, 0.0, 1.0);
}

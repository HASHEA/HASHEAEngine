static const uint kCandidateValue = 0x47505544u;

StructuredBuffer<uint> Candidate : register(t0);
RWStructuredBuffer<uint> Visible : register(u0);
RWByteAddressBuffer DrawArgs : register(u1);

[numthreads(1, 1, 1)]
void CSBuildVisibleAndArgs()
{
    Visible[0] = Candidate[0];
    DrawArgs.Store(0, 3u);
    DrawArgs.Store(4, 1u);
    DrawArgs.Store(8, 0u);
    DrawArgs.Store(12, 0u);
    DrawArgs.Store(16, 0u);
}

StructuredBuffer<uint> VisibleInstances : register(t1);
ByteAddressBuffer ValidationArgs : register(t2);

struct VSOutput
{
    float4 position : SV_Position;
};

float2 triangle_position(uint vertex_id, float center_x)
{
    if (vertex_id == 0u)
    {
        return float2(center_x - 0.4, -0.5);
    }
    if (vertex_id == 1u)
    {
        return float2(center_x + 0.4, -0.5);
    }
    return float2(center_x, 0.5);
}

VSOutput VSIndirect(uint vertex_id : SV_VertexID)
{
    VSOutput output;
    output.position = float4(triangle_position(vertex_id, -0.5), 0.0, 1.0);
    return output;
}

float4 PSIndirect(VSOutput input) : SV_Target0
{
    return VisibleInstances[0] == kCandidateValue
        ? float4(0.0, 1.0, 0.0, 1.0)
        : float4(0.0, 0.0, 0.0, 1.0);
}

VSOutput VSValidation(uint vertex_id : SV_VertexID)
{
    VSOutput output;
    output.position = float4(triangle_position(vertex_id, 0.5), 0.0, 1.0);
    return output;
}

float4 PSValidation(VSOutput input) : SV_Target0
{
    const bool valid = ValidationArgs.Load(0) == 3u &&
        ValidationArgs.Load(4) == 1u &&
        ValidationArgs.Load(8) == 0u &&
        ValidationArgs.Load(12) == 0u &&
        ValidationArgs.Load(16) == 0u;
    return valid
        ? float4(0.0, 0.0, 1.0, 1.0)
        : float4(0.0, 0.0, 0.0, 1.0);
}

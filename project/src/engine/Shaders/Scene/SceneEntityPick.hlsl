#include "../../Graphics/Shaders/AshVertexDeclLocations.hlsli"

struct VSInput
{
    ASH_MESH_VERTEX_POSITION_ATTR float3 position : POSITION;
    ASH_MESH_VERTEX_NORMAL_ATTR float3 normal : NORMAL;
    ASH_MESH_VERTEX_TANGENT_ATTR float4 tangent : TANGENT;
    ASH_MESH_VERTEX_TEXCOORD0_ATTR float2 uv0 : TEXCOORD0;
    ASH_MESH_VERTEX_TEXCOORD1_ATTR float2 uv1 : TEXCOORD1;
    ASH_MESH_VERTEX_COLOR0_ATTR float4 color : COLOR0;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL0_ATTR float4 object_to_clip_col0 : TEXCOORD2;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL1_ATTR float4 object_to_clip_col1 : TEXCOORD3;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL2_ATTR float4 object_to_clip_col2 : TEXCOORD4;
    ASH_MESH_INSTANCE_OBJECT_TO_CLIP_COL3_ATTR float4 object_to_clip_col3 : TEXCOORD5;
};

struct VSOutput
{
    float4 position : SV_Position;
};

cbuffer AshRootConstants : register(b0)
{
    uint2 EntityId;
    uint2 _Padding;
};

float4 TransformPositionToClip(VSInput input, float3 position_os)
{
    return
        input.object_to_clip_col0 * position_os.x +
        input.object_to_clip_col1 * position_os.y +
        input.object_to_clip_col2 * position_os.z +
        input.object_to_clip_col3;
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = TransformPositionToClip(input, input.position);
    return output;
}

uint2 PSMain(VSOutput input) : SV_Target0
{
    return EntityId;
}

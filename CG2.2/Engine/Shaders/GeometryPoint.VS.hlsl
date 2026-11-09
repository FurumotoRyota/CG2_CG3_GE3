#include "Geometry.hlsli"

struct PointVertexInput
{
    float3 position : POSITION0;
    float size : TEXCOORD0;
    float4 color : COLOR0;
    float seed : TEXCOORD1;
};

PointVSOutput main(PointVertexInput input)
{
    PointVSOutput output;
    output.worldPosition = mul(float4(input.position, 1.0f), gParams.world).xyz;
    output.size = input.size;
    output.color = input.color;
    output.seed = input.seed;
    return output;
}

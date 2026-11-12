// 板ポリ大量配置用VertexShader（Instancing）
// 板ごとのWVP・色は StructuredBuffer(t0) に入っていて、SV_InstanceID で取り出す

struct PlaneForGPU
{
    float4x4 WVP;
    float4 color;
};
StructuredBuffer<PlaneForGPU> gPlanes : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    PlaneForGPU plane = gPlanes[instanceId];

    VertexShaderOutput output;
    output.position = mul(input.position, plane.WVP);
    output.texcoord = input.texcoord;
    output.color = plane.color;
    return output;
}

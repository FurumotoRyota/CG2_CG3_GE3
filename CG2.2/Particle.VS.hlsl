struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // パーティクルでは未使用
};

struct ParticleForGPU
{
    float4x4 WVP;
    float4 color;
};
StructuredBuffer<ParticleForGPU> gParticles : register(t0);

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gParticles[instanceId].WVP);
    output.texcoord = input.texcoord;
    output.color = gParticles[instanceId].color;
    return output;
}

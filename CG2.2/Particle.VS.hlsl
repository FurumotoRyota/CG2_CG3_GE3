// パーティクル用VertexShader（Instancing）
// 粒ごとのWVP・色・形は StructuredBuffer(t0) に入っていて、SV_InstanceID で取り出す

struct ParticleForGPU
{
    float4x4 WVP;
    float4 color;
    float4 params; // x: 形(0:SoftCircle 1:Circle 2:Square)
};
StructuredBuffer<ParticleForGPU> gParticles : register(t0);

// このDrawが読み始めるインスタンスの番号（SV_InstanceIDは毎回0始まりなので足して使う）
cbuffer InstanceOffset : register(b0)
{
    uint gInstanceOffset;
};

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
    float shape : TEXCOORD1;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    ParticleForGPU particle = gParticles[gInstanceOffset + instanceId];

    VertexShaderOutput output;
    output.position = mul(input.position, particle.WVP);
    output.texcoord = input.texcoord;
    output.color = particle.color;
    output.shape = particle.params.x;
    return output;
}

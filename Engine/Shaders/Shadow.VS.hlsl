// 影の描画パス：光から見た位置だけを出力する（深度だけ描く）
struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
    float4x4 LightWVP;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

float4 main(VertexShaderInput input) : SV_POSITION
{
    return mul(input.position, gTransformationMatrix.LightWVP);
}

#include "Geometry.hlsli"

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

// 頂点シェーダーはワールド座標へ変換するだけ。画面座標への変換はジオメトリシェーダーで行う
ModelVSOutput main(VertexShaderInput input)
{
    ModelVSOutput output;
    output.worldPosition = mul(input.position, gParams.world).xyz;
    output.normal = normalize(mul(input.normal, (float3x3) gParams.world));
    output.uv = input.texcoord;
    return output;
}

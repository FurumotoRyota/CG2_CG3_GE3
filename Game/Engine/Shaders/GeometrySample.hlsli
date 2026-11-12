#include "Geometry.hlsli"

// 頂点シェーダーの出力（ワールド座標）を画面座標に変換して、ピクセルシェーダーへ渡す頂点にする
ModelGSOutput MakeVertex(ModelVSOutput v)
{
    ModelGSOutput o;
    o.svpos = mul(float4(v.worldPosition, 1.0f), gParams.viewProj);
    o.normal = v.normal;
    o.uv = v.uv;
    return o;
}

#include "GeometrySample.hlsli"

// 数値の加工：UVを gParams.size 倍にして出力する。
// 1を超えたUVは、サンプラーが WRAP なら繰り返し（タイリング）になる
[maxvertexcount(3)]
void main(triangle ModelVSOutput input[3], inout TriangleStream<ModelGSOutput> output)
{
    for (uint i = 0; i < 3; i++)
    {
        ModelGSOutput e = MakeVertex(input[i]);
        e.uv *= gParams.size;
        output.Append(e);
    }
}

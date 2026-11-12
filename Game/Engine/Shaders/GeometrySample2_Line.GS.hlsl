#include "GeometrySample.hlsli"

// Lineの出力：最初の2頂点を結ぶ線分を1本出力する
[maxvertexcount(2)]
void main(triangle ModelVSOutput input[3], inout LineStream<ModelGSOutput> output)
{
    output.Append(MakeVertex(input[0]));
    output.Append(MakeVertex(input[1]));
}

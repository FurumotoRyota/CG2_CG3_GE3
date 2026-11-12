#include "GeometrySample.hlsli"

// 複数頂点の出力：三角形の3頂点をそのまま3つの点として出力する
[maxvertexcount(3)]
void main(triangle ModelVSOutput input[3], inout PointStream<ModelGSOutput> output)
{
    for (uint i = 0; i < 3; i++)
    {
        output.Append(MakeVertex(input[i]));
    }
}

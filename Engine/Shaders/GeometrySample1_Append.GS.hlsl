#include "GeometrySample.hlsli"

// Append：1頂点だけ出力する（三角形1つにつき点が1つだけ残る）
[maxvertexcount(1)]
void main(triangle ModelVSOutput input[3], inout PointStream<ModelGSOutput> output)
{
    output.Append(MakeVertex(input[0]));
}

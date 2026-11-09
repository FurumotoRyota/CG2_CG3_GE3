#include "GeometrySample.hlsli"

// LineStrip：3頂点を順に出力し、最後に最初の頂点へ戻って輪郭線にする
[maxvertexcount(4)]
void main(triangle ModelVSOutput input[3], inout LineStream<ModelGSOutput> output)
{
    for (uint i = 0; i < 3; i++)
    {
        output.Append(MakeVertex(input[i]));
    }
    output.Append(MakeVertex(input[0]));
}

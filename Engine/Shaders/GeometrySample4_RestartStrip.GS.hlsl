#include "GeometrySample.hlsli"

// RestartStrip：辺ごとに「始点→終点」を出力してストリップを区切る（辺が独立した線分になる）
[maxvertexcount(6)]
void main(triangle ModelVSOutput input[3], inout LineStream<ModelGSOutput> output)
{
    for (uint i = 0; i < 3; i++)
    {
        uint next = (i == 2) ? 0 : i + 1; // 最後の辺は最初の頂点に戻る
        output.Append(MakeVertex(input[i]));
        output.Append(MakeVertex(input[next]));
        output.RestartStrip();
    }
}

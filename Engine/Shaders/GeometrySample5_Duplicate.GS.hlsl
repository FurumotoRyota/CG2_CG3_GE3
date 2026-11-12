#include "GeometrySample.hlsli"

// プリミティブの複製：元の三角形を出力し、RestartStripで区切って、横にずらしたコピーをもう1つ出力する
// コピーはUVを gParams.size 倍にする（数値の加工）。ずらす量は gParams.amount（カメラの右方向のワールド距離）
[maxvertexcount(6)]
void main(triangle ModelVSOutput input[3], inout TriangleStream<ModelGSOutput> output)
{
    // 1つ目：そのまま
    for (uint i = 0; i < 3; i++)
    {
        output.Append(MakeVertex(input[i]));
    }
    output.RestartStrip();

    // 2つ目：ずらしてUVを拡大
    float3 offset = gParams.camRight.xyz * gParams.amount;
    for (uint j = 0; j < 3; j++)
    {
        ModelVSOutput v = input[j];
        v.worldPosition += offset;
        ModelGSOutput e = MakeVertex(v);
        e.uv *= gParams.size;
        output.Append(e);
    }
}

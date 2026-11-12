#include "Geometry.hlsli"

// 軸 axis まわりに角度 angle だけ回す（ロドリゲスの回転公式）
float3 RotateAround(float3 v, float3 axis, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0f - c);
}

// 入力：三角形（頂点3つ） 出力：三角形ストリーム（最大3頂点）
[maxvertexcount(3)]
void main(triangle ModelVSOutput input[3], inout TriangleStream<ModelGSOutput> output)
{
    float3 p[3] = { input[0].worldPosition, input[1].worldPosition, input[2].worldPosition };
    float3 center = (p[0] + p[1] + p[2]) / 3.0f;
    float3 faceNormal = normalize(input[0].normal + input[1].normal + input[2].normal);

    // 三角形ごとに少しずつタイミングをずらすための値（重心から作る疑似乱数）
    float phase = dot(center, float3(12.9898f, 78.233f, 37.719f));
    float wave = (gParams.animate > 0.5f) ? (0.5f + 0.5f * sin(gParams.time * 2.0f + phase)) : 1.0f;

    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        float3 pos = p[i];
        if (gParams.mode > 2.5f)
        {
            // 3: 三角形ごとに、自分の法線まわりに回転する
            float angle = gParams.amount * ((gParams.animate > 0.5f) ? sin(gParams.time * 2.0f + phase) : 1.0f) * 3.14159265f;
            pos = center + RotateAround(pos - center, faceNormal, angle);
        }
        else if (gParams.mode > 1.5f)
        {
            // 2: 重心に向かって縮める
            pos = lerp(pos, center, saturate(gParams.amount));
        }
        else if (gParams.mode > 0.5f)
        {
            // 1: 面の向き（法線）に沿って外へ飛ばす（爆発）
            pos += faceNormal * gParams.amount * wave;
        }
        // 0: そのまま（何も起こらない）

        ModelGSOutput element;
        element.svpos = mul(float4(pos, 1.0f), gParams.viewProj);
        element.normal = input[i].normal;
        element.uv = input[i].uv;
        output.Append(element);
    }
}

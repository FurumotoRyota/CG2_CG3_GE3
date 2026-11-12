#include "Geometry.hlsli"

// 入力：点（頂点1つ） 出力：三角形ストリーム（最大4頂点 = 四角形 or 三角形）
[maxvertexcount(4)]
void main(point PointVSOutput input[1], inout TriangleStream<PointGSOutput> output)
{
    PointVSOutput p = input[0];

    // 点ごとに大きさを脈動させ、画面の向きに合わせた平面の中でゆっくり回す
    float s = p.size * gParams.size;
    float angle = 0.0f;
    if (gParams.animate > 0.5f)
    {
        s *= 0.75f + 0.25f * sin(gParams.time * 3.0f + p.seed * 6.2831853f);
        angle = gParams.time * (p.seed - 0.5f) * 4.0f;
    }
    float c = cos(angle);
    float sn = sin(angle);
    // カメラの右・上方向を回した軸（常にカメラの方を向く = ビルボード）
    float3 right = gParams.camRight.xyz * c + gParams.camUp.xyz * sn;
    float3 up = -gParams.camRight.xyz * sn + gParams.camUp.xyz * c;

    PointGSOutput v;
    v.color = p.color * gParams.color;

    if (gParams.shape < 0.5f)
    {
        // 80_02：点から三角形を作る
        float3 pos[3] = {
            p.worldPosition + up * s,
            p.worldPosition + right * (s * 0.87f) - up * (s * 0.5f),
            p.worldPosition - right * (s * 0.87f) - up * (s * 0.5f)
        };
        float2 uvs[3] = { float2(0.5f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f) };
        [unroll]
        for (uint i = 0; i < 3; i++)
        {
            v.svpos = mul(float4(pos[i], 1.0f), gParams.viewProj);
            v.uv = uvs[i];
            output.Append(v);
        }
    }
    else
    {
        // 80_03：点からテクスチャ付きの四角形を作る（三角形ストリップ4頂点）
        float2 corners[4] = { float2(-1.0f, 1.0f), float2(1.0f, 1.0f), float2(-1.0f, -1.0f), float2(1.0f, -1.0f) };
        float2 uvs[4] = { float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f) };
        [unroll]
        for (uint i = 0; i < 4; i++)
        {
            float3 pos = p.worldPosition + right * (corners[i].x * s) + up * (corners[i].y * s);
            v.svpos = mul(float4(pos, 1.0f), gParams.viewProj);
            v.uv = uvs[i];
            output.Append(v);
        }
    }
}

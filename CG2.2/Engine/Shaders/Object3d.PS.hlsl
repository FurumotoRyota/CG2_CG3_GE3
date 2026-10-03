#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int lightingMode; // 0:なし 1:Lambert 2:HalfLambert
    float alphaCutoff; // テクスチャのα値がこれ未満のピクセルは描かない（0なら無効）
    float2 padding;
    float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input, bool isFrontFace : SV_IsFrontFace)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // アルファテスト：透明に近いピクセルは discard で描かない
    // → 透過PNG（フェンスなど）の透明部分が深度バッファに書き込まれず、奥のオブジェクトが見える
    if (textureColor.a < gMaterial.alphaCutoff)
    {
        discard;
    }

    // 法線。両面描画で裏面が映っているときは向きを反転して、裏側でも正しくライティングする
    float3 normal = normalize(input.normal);
    if (!isFrontFace)
    {
        normal = -normal;
    }

    // α値は Material と Texture だけで決める（ライトの強さ・向きには影響させない）
    // → ブレンドモード（αブレンド・加算など）でα値がそのまま使えるようになる
    output.color.a = gMaterial.color.a * textureColor.a;

    // 色(rgb)だけにライティングを行う
    if (gMaterial.lightingMode == 1)
    {
        // Lambertian Reflectance（通常のランバート反射）
        float NdotL = dot(normal, -gDirectionalLight.direction);
        float cos = saturate(NdotL);

        output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
    }
    else if (gMaterial.lightingMode == 2)
    {
        // 1. 法線とライトの逆方向の内積を計算（範囲: -1.0 ～ 1.0）
        float NdotL = dot(normal, -gDirectionalLight.direction);

        // 2. ハーフランバートの計算
        // 0.5を掛けて0.5を足すことで、-1.0～1.0の範囲を0.0～1.0にマッピングし直す
        float halfLambert = NdotL * 0.5f + 0.5f;

        // 3. （オプション）コントラストを調整するために二乗する
        float cos = pow(halfLambert, 2.0f);

        // 4. 最終的な色に反映
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
    }
    else
    {
        // ライティングなし（スプライト等はこちらを通るので絶対に必要！）
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb;
    }

    // 完全に透明なピクセルは描かない（深度バッファにも書き込まない）
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}

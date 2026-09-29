#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int lightingMode; // 0:なし 1:Lambert 2:HalfLambert
    float3 padding;
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

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    if (gMaterial.lightingMode == 1)
    {
        // Lambertian Reflectance（通常のランバート反射）
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cos = saturate(NdotL);

        output.color = gMaterial.color * textureColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
    }
    else if (gMaterial.lightingMode == 2)
    {
        // 1. 法線とライトの逆方向の内積を計算（範囲: -1.0 ～ 1.0）
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        
        // 2. ハーフランバートの計算
        // 0.5を掛けて0.5を足すことで、-1.0～1.0の範囲を0.0～1.0にマッピングし直す
        float halfLambert = NdotL * 0.5f + 0.5f;
        
        // 3. （オプション）コントラストを調整するために二乗する
        float cos = pow(halfLambert, 2.0f);
        
        // 4. 最終的な色に反映
        output.color = gMaterial.color * textureColor * gDirectionalLight.color * cos * gDirectionalLight.intensity;
    }
    else
    {
        // ライティングなし（スプライト等はこちらを通るので絶対に必要！）
        output.color = gMaterial.color * textureColor;
    }

    return output;
}
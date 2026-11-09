#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int lightingMode;  // 0:なし 1:Lambert 2:HalfLambert
    float alphaCutoff; // テクスチャのα値がこれ未満のピクセルは描かない（0なら無効）
    float shininess;   // 鏡面反射の鋭さ
    int specularMode;  // 0:なし 1:Phong 2:Blinn-Phong 3:PBR(GGX)
    float roughness;   // PBR：粗さ
    float metallic;    // PBR：金属度
    float receiveShadow; // 影を受けるか
    float padding;
    float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);

// ポイントライト / スポットライト1つぶん（ShaderTypes.h の LightData と同じ並び）
struct LightData
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float range;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    int type; // 0:ポイント 1:スポット
};

// C++ の DirectionalLight 構造体（ShaderTypes.h）と同じ並び
struct LightBuffer
{
    // ディレクショナルライト
    float4 color;
    float3 direction;
    float intensity;

    float3 cameraPosition;
    float padding0;

    // 配置したライト（ポイント / スポット）
    int lightCount;
    float shadowBias;
    float shadowStrength;
    int shadowEnabled;
    LightData lights[16];
};
ConstantBuffer<LightBuffer> gLight : register(b1);

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gShadowMap : register(t2);           // ライトから見た深度
SamplerComparisonState gShadowSampler : register(s1); // 深度の比較サンプラー
TextureCube<float4> gEnvironment : register(t1); // 環境キューブマップ（PBRの反射・拡散に使う）
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// 拡散反射の係数（lightingMode: 1=Lambert, 2=HalfLambert）
float DiffuseFactor(float NdotL)
{
    if (gMaterial.lightingMode == 2)
    {
        float halfLambert = NdotL * 0.5f + 0.5f;
        return pow(halfLambert, 2.0f);
    }
    return saturate(NdotL);
}

static const float kPi = 3.14159265f;

// PBR（Cook-Torrance）：GGX法線分布 × Smithの幾何減衰(Schlick-GGX) × Schlickフレネル
float3 ComputePbrLight(float3 N, float3 V, float3 L, float3 lit, float3 albedo)
{
    float3 Ldir = -L;
    float NdotL = dot(N, Ldir);
    if (NdotL <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float rough = clamp(gMaterial.roughness, 0.04f, 1.0f);
    float metallic = saturate(gMaterial.metallic);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float3 H = normalize(Ldir + V);
    float NdotV = max(dot(N, V), 0.0001f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // D：GGX
    float a = rough * rough;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    float D = a2 / (kPi * denom * denom);

    // G：Smith（Schlick-GGX）
    float k = (rough + 1.0f) * (rough + 1.0f) / 8.0f;
    float G = (NdotL / (NdotL * (1.0f - k) + k)) * (NdotV / (NdotV * (1.0f - k) + k));

    // F：Schlick
    float3 F = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

    float3 specular = D * G * F / (4.0f * NdotL * NdotV);
    float3 kD = (1.0f - F) * (1.0f - metallic);
    // 既存ライトの明るさ(強さ1で白)に合わせるため、拡散は 1/π を掛けず、鏡面は π を掛ける
    float3 diffuse = kD * albedo * DiffuseFactor(NdotL);
    return (diffuse + specular * kPi * NdotL) * lit;
}

// ディレクショナルライトの影：1=光が当たる、0=影。3x3でぼかして縁をなめらかにする
float ComputeShadow(float4 lightPos, float3 N, float3 lightDir)
{
    if (gLight.shadowEnabled == 0 || gMaterial.receiveShadow < 0.5f)
    {
        return 1.0f;
    }
    float3 p = lightPos.xyz / lightPos.w;
    float2 uv = float2(p.x * 0.5f + 0.5f, -p.y * 0.5f + 0.5f);
    if (p.z > 1.0f)
    {
        return 1.0f;
    }

    // 光に対して斜めの面ほどバイアスを大きくして、縞模様(シャドウアクネ)を防ぐ
    float NdotL = saturate(dot(N, -lightDir));
    float bias = gLight.shadowBias * (1.0f + 3.0f * (1.0f - NdotL));
    float reference = p.z - bias;

    float texel = 1.0f / 2048.0f;
    float sum = 0.0f;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            sum += gShadowMap.SampleCmpLevelZero(gShadowSampler, uv + float2(x, y) * texel, reference);
        }
    }
    float lit = sum / 9.0f;
    return lerp(1.0f, lit, saturate(gLight.shadowStrength));
}

// 1つのライトによる色。L はライトの進む向き（光源 → 表面）、attenuation は距離などによる減衰
float3 ComputeLight(float3 N, float3 V, float3 L, float3 lightColor, float lightIntensity, float attenuation, float3 albedo)
{
    float NdotL = dot(N, -L);
    float3 lit = lightColor * lightIntensity * attenuation;

    if (gMaterial.specularMode == 3)
    {
        return ComputePbrLight(N, V, L, lit, albedo);
    }

    // 拡散反射
    float3 result = albedo * lit * DiffuseFactor(NdotL);

    // 鏡面反射（光が表面の表側から当たっているときだけ）
    if (gMaterial.specularMode != 0 && NdotL > 0.0f)
    {
        float specularPow;
        if (gMaterial.specularMode == 1)
        {
            // Phong：反射ベクトルと視線の一致具合
            float3 R = reflect(L, N);
            specularPow = pow(saturate(dot(R, V)), gMaterial.shininess);
        }
        else
        {
            // Blinn-Phong：ハーフベクトルと法線の一致具合
            float3 H = normalize(-L + V);
            specularPow = pow(saturate(dot(N, H)), gMaterial.shininess);
        }
        result += lit * specularPow;
    }
    return result;
}

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

    float3 albedo = gMaterial.color.rgb * textureColor.rgb;

    if (gMaterial.lightingMode == 1 || gMaterial.lightingMode == 2)
    {
        float3 toEye = normalize(gLight.cameraPosition - input.worldPosition);
        float3 result = float3(0.0f, 0.0f, 0.0f);

        // ディレクショナルライト（減衰なし）
        float shadow = ComputeShadow(input.lightPos, normal, normalize(gLight.direction));
        result += shadow * ComputeLight(normal, toEye, normalize(gLight.direction), gLight.color.rgb, gLight.intensity, 1.0f, albedo);

        // 配置したライト：距離で暗くなる。スポットはさらに円錐の角度でも暗くなる
        for (int i = 0; i < gLight.lightCount; ++i)
        {
            LightData l = gLight.lights[i];
            if (l.intensity <= 0.0f)
            {
                continue;
            }
            float3 toSurface = input.worldPosition - l.position;
            float distance = length(toSurface);
            float3 L = toSurface / max(distance, 0.0001f);
            float attenuation = pow(saturate(-distance / l.range + 1.0f), l.decay);
            if (l.type == 1)
            {
                float cosAngle = dot(L, normalize(l.direction));
                attenuation *= saturate((cosAngle - l.cosAngle) / max(l.cosFalloffStart - l.cosAngle, 0.0001f));
            }
            result += ComputeLight(normal, toEye, L, l.color.rgb, l.intensity, attenuation, albedo);
        }

        // PBR：空の環境光（金属は空を映し、非金属はうっすら拡散する）。ライトが無くても質感が出る
        if (gMaterial.specularMode == 3)
        {
            float rough = clamp(gMaterial.roughness, 0.04f, 1.0f);
            float metallic = saturate(gMaterial.metallic);
            float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
            float NdotV = saturate(dot(normal, toEye));
            float3 F = F0 + (max(float3(1.0f - rough, 1.0f - rough, 1.0f - rough), F0) - F0) * pow(1.0f - NdotV, 5.0f);

            // 環境キューブマップ：粗いほどぼやけたミップを読む（鏡面）。拡散は一番ぼやけたミップ（周囲の平均）
            uint envWidth, envHeight, envLevels;
            gEnvironment.GetDimensions(0, envWidth, envHeight, envLevels);
            float maxMip = (float)(envLevels - 1);

            float3 R = reflect(-toEye, normal);
            float3 env = gEnvironment.SampleLevel(gSampler, R, rough * maxMip).rgb;
            float3 irradiance = gEnvironment.SampleLevel(gSampler, normal, max(maxMip - 1.0f, 0.0f)).rgb;
            float3 ambientDiffuse = (1.0f - F) * (1.0f - metallic) * albedo * irradiance * 0.6f;
            result += ambientDiffuse + env * F;
        }

        output.color.rgb = result;
    }
    else
    {
        // ライティングなし（スプライト等はこちらを通るので絶対に必要！）
        output.color.rgb = albedo;
    }

    // 完全に透明なピクセルは描かない（深度バッファにも書き込まない）
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}

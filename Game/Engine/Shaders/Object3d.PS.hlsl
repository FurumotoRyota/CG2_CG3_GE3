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
    float shadowRange;

    // 配置したライト（ポイント / スポット）
    int lightCount;
    float shadowBias;
    float shadowStrength;
    int shadowEnabled;
    float shadowSoftness;
    float ambientSky;
    float groundAo;
    int shadowQuality;

    // 時間帯（TimeOfDay）：空の色と霧
    float3 skyHorizon;
    float fogDensity;
    float3 skyZenith;
    float fogStart;
    float3 skyGround;
    int skyMode;      // 0:キューブマップ 1:空の色から計算
    float3 fogColor;
    float skyPadding;

    // 雨で濡れた見た目
    float wetness;
    float puddleAmount;
    float rainTime;
    float rainAmount;

    LightData lights[16];
};
ConstantBuffer<LightBuffer> gLight : register(b1);

// 1ピクセルごとの粗さ・金属度。材質の値を、濡れ具合などで変えて使う
static float g_rough = 0.5f;
static float g_metal = 0.0f;

// ---- 濡れた地面（水たまり）のためのノイズ
float WetHash(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float WetNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(WetHash(i), WetHash(i + float2(1.0f, 0.0f)), f.x),
        lerp(WetHash(i + float2(0.0f, 1.0f)), WetHash(i + float2(1.0f, 1.0f)), f.x), f.y);
}

// 水たまりの形。0〜1（1が水たまりの中）。濡れるほど広がる
float PuddleMask(float2 xz, float3 N, float worldY)
{
    float k = saturate(gLight.wetness * gLight.puddleAmount * 2.0f);
    if (k <= 0.001f)
    {
        return 0.0f;
    }
    float n = WetNoise(xz * 0.30f) * 0.6f + WetNoise(xz * 0.9f + 7.3f) * 0.3f + WetNoise(xz * 2.7f + 3.1f) * 0.1f;
    float thr = lerp(0.88f, 0.48f, k);
    float flatMask = smoothstep(0.85f, 0.97f, N.y) * saturate(1.0f - worldY / 0.35f); // 低くて平らな面だけ
    return flatMask * smoothstep(thr, thr + 0.10f, n);
}

// 雨粒が落ちて広がる波紋による、法線のずれ（水たまりの水面用）
float3 RippleNormal(float2 xz)
{
    float2 p = xz / 0.9f;
    float2 id = floor(p);
    float2 f = frac(p);
    float h = WetHash(id);
    float2 center = 0.2f + 0.6f * float2(h, WetHash(id + 7.0f));
    float age = frac(gLight.rainTime * 0.9f + h * 5.0f);
    float2 d = (f - center) * 0.9f;
    float dist = length(d);
    float q = (dist - age * 0.45f) / 0.04f;
    float ring = exp(-q * q);
    float strength = (1.0f - age) * ring * step(0.45f, h);
    float2 dir = d / max(dist, 0.0001f);
    return float3(dir.x, 0.0f, dir.y) * strength * 0.9f;
}

// 時間帯の空の色（Skybox.PS.hlsl と同じ式）。blur が大きいほど、ぼやけた（周囲の平均に近い）色になる
float3 ProceduralSky(float3 dir, float blur)
{
    float3 c;
    if (dir.y >= 0.0f)
    {
        c = lerp(gLight.skyHorizon, gLight.skyZenith, pow(dir.y, 0.5f));
    }
    else
    {
        c = lerp(gLight.skyHorizon * 0.85f, gLight.skyGround, pow(-dir.y, 0.4f));
    }
    float3 average = gLight.skyHorizon * 0.45f + gLight.skyZenith * 0.30f + gLight.skyGround * 0.25f;
    return lerp(c, average, saturate(blur));
}

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
    float rough = clamp(g_rough, 0.04f, 1.0f);
    float metallic = saturate(g_metal);
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

// 影の判定に使う、円盤状のサンプル点（ポアソンディスク）
static const float2 kPoisson[16] =
{
    float2(-0.94201624f, -0.39906216f), float2(0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f), float2(0.34495938f, 0.29387760f),
    float2(-0.91588581f, 0.45771432f), float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f, 0.27676845f), float2(0.97484398f, 0.75648379f),
    float2(0.44323325f, -0.97511554f), float2(0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f), float2(0.79197514f, 0.19090188f),
    float2(-0.24188840f, 0.99706507f), float2(-0.81409955f, 0.91437590f),
    float2(0.19984126f, 0.78641367f), float2(0.14383161f, -0.14100790f)
};

static const float kShadowRes = 2048.0f; // ShadowMap::kSize と同じ

// 画面の位置ごとに違う乱数（0〜1）。サンプル点を回して、ぼかしの縞を細かいノイズに変える
float InterleavedGradientNoise(float2 p)
{
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

// ディレクショナルライトの影：1=光が当たる、0=影
//   shadowQuality 0 … 3x3でぼかして縁をなめらかにする
//   shadowQuality 1 … PCSS：影を落とす物（ブロッカー）を探して、距離が離れるほど縁を広くぼかす（足もとはくっきり、先端はふんわり）
float ComputeShadow(float4 lightPos, float3 N, float3 lightDir, float2 pixelPos)
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

    float texel = 1.0f / kShadowRes;
    float lit = 0.0f;

    if (gLight.shadowQuality == 0 || gLight.shadowRange <= 0.0f || gLight.shadowSoftness <= 0.0f)
    {
        float sum = 0.0f;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                sum += gShadowMap.SampleCmpLevelZero(gShadowSampler, uv + float2(x, y) * texel, reference);
            }
        }
        lit = sum / 9.0f;
    }
    else
    {
        // 影の深さ(0〜1)をワールドの長さに直す倍率と、ワールドの長さをUVに直す倍率
        float depthToWorld = gLight.shadowRange * 3.0f - 0.1f;
        float worldToUv = 0.5f / gLight.shadowRange;

        float angle = InterleavedGradientNoise(pixelPos) * 6.2831853f;
        float2 rot = float2(cos(angle), sin(angle));

        // 1) ブロッカー探し：このあたりで、光を遮っているものの平均の深さを求める
        float searchUv = gLight.shadowSoftness * 6.0f * worldToUv;
        searchUv = clamp(searchUv, 2.0f * texel, 40.0f * texel);
        float blockerSum = 0.0f;
        float blockerCount = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            float2 o = float2(kPoisson[i].x * rot.x - kPoisson[i].y * rot.y, kPoisson[i].x * rot.y + kPoisson[i].y * rot.x);
            float2 suv = uv + o * searchUv;
            int2 ip = clamp(int2(suv * kShadowRes), int2(0, 0), int2(2047, 2047));
            float d = gShadowMap.Load(int3(ip, 0));
            if (d < reference)
            {
                blockerSum += d;
                blockerCount += 1.0f;
            }
        }
        if (blockerCount < 0.5f)
        {
            return 1.0f; // 遮るものが無い
        }
        float avgBlocker = blockerSum / blockerCount;

        // 2) 縁の広さ：遮る物から離れているほど広い
        float penumbraWorld = (reference - avgBlocker) * depthToWorld * gLight.shadowSoftness;
        float filterUv = clamp(penumbraWorld * worldToUv, 1.5f * texel, 40.0f * texel);

        // 3) その広さで影を判定してぼかす
        float sum = 0.0f;
        for (int k = 0; k < 12; ++k)
        {
            float2 o = float2(kPoisson[k].x * rot.x - kPoisson[k].y * rot.y, kPoisson[k].x * rot.y + kPoisson[k].y * rot.x);
            sum += gShadowMap.SampleCmpLevelZero(gShadowSampler, uv + o * filterUv, reference);
        }
        lit = sum / 12.0f;
    }
    return lerp(1.0f, lit, saturate(gLight.shadowStrength));
}

// 壁の足もとの暗がり（アンビエントオクルージョン風）。縦向きの面ほど、地面に近いほど暗くする
float ComputeGroundAo(float3 worldPos, float3 N)
{
    if (gLight.groundAo <= 0.0f)
    {
        return 1.0f;
    }
    float vertical = 1.0f - saturate(abs(N.y));
    float nearGround = 1.0f - saturate(worldPos.y / 1.4f);
    return 1.0f - gLight.groundAo * vertical * nearGround * nearGround;
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

        // 雨で濡れた見た目：暗く・つやが強くなる。低い平らな所には水たまりができ、雨粒の波紋が立つ
        g_rough = gMaterial.roughness;
        g_metal = gMaterial.metallic;
        float puddle = 0.0f;
        if (gLight.wetness > 0.001f)
        {
            float wet = gLight.wetness * lerp(0.55f, 1.0f, saturate(normal.y)); // 壁より地面が濡れる
            albedo *= lerp(1.0f, 0.62f, wet * (1.0f - saturate(g_metal)));
            g_rough = lerp(g_rough, g_rough * 0.4f, wet);
            puddle = PuddleMask(input.worldPosition.xz, normal, input.worldPosition.y);
            if (puddle > 0.001f)
            {
                albedo *= lerp(1.0f, 0.6f, puddle);
                g_rough = lerp(g_rough, 0.05f, puddle);
                normal = normalize(normal + RippleNormal(input.worldPosition.xz) * puddle * gLight.rainAmount);
            }
        }

        // ディレクショナルライト（減衰なし）
        float shadow = ComputeShadow(input.lightPos, normal, normalize(gLight.direction), input.position.xy);
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

        float ao = ComputeGroundAo(input.worldPosition, normal);

        // PBRでない材質：空と地面からの環境光（上向きの面は空色、下向きの面は砂の照り返し）
        if (gMaterial.specularMode != 3 && gLight.ambientSky > 0.0f)
        {
            float hemi = normal.y * 0.5f + 0.5f;
            float3 groundAmb = float3(0.42f, 0.31f, 0.21f);
            float3 skyAmb = float3(0.52f, 0.62f, 0.82f);
            if (gLight.skyMode == 1)
            {
                // 時間帯の空の色から環境光を作る（夕方はオレンジ寄り、夜は暗い青）
                groundAmb = gLight.skyGround * 1.4f;
                skyAmb = lerp(gLight.skyHorizon, gLight.skyZenith, 0.35f) * 0.9f;
            }
            float3 amb = lerp(groundAmb, skyAmb, hemi) * gLight.ambientSky;
            result += albedo * amb * ao;
        }

        // PBR：空の環境光（金属は空を映し、非金属はうっすら拡散する）。ライトが無くても質感が出る
        if (gMaterial.specularMode == 3)
        {
            float rough = clamp(g_rough, 0.04f, 1.0f);
            float metallic = saturate(g_metal);
            float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
            float NdotV = saturate(dot(normal, toEye));
            float3 F = F0 + (max(float3(1.0f - rough, 1.0f - rough, 1.0f - rough), F0) - F0) * pow(1.0f - NdotV, 5.0f);
            F = lerp(F, max(F, float3(0.35f, 0.35f, 0.35f)), puddle); // 水たまりは真上から見ても空を映す

            // 環境キューブマップ：粗いほどぼやけたミップを読む（鏡面）。拡散は一番ぼやけたミップ（周囲の平均）
            uint envWidth, envHeight, envLevels;
            gEnvironment.GetDimensions(0, envWidth, envHeight, envLevels);
            float maxMip = (float)(envLevels - 1);

            float3 R = reflect(-toEye, normal);
            float3 env = gEnvironment.SampleLevel(gSampler, R, rough * maxMip).rgb;
            float3 irradiance = gEnvironment.SampleLevel(gSampler, normal, max(maxMip - 1.0f, 0.0f)).rgb;
            if (gLight.skyMode == 1)
            {
                env = ProceduralSky(R, rough);
                irradiance = ProceduralSky(normal, 0.6f);
            }
            float3 ambientDiffuse = (1.0f - F) * (1.0f - metallic) * albedo * irradiance * 0.6f;
            float envScale = gLight.ambientSky > 0.0f ? gLight.ambientSky * 2.0f : 1.0f; // 0なら従来どおり（等倍）
            result += (ambientDiffuse + env * F) * envScale * ao;
        }

        // 霧：カメラから遠いほど霧の色に近づく（時間帯の空気感。距離が fogStart までは変わらない）
        if (gLight.fogDensity > 0.0f)
        {
            float dist = length(input.worldPosition - gLight.cameraPosition);
            float fog = 1.0f - exp(-max(dist - gLight.fogStart, 0.0f) * gLight.fogDensity);
            result = lerp(result, gLight.fogColor, saturate(fog));
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

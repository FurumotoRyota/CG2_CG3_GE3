// スカイボックス：キューブマップ、または時間帯（TimeOfDay）の空の色から計算して描く
TextureCube<float4> gEnvironment : register(t0);
SamplerState gSampler : register(s0);

struct SkyConstants
{
    float4x4 invView;
    float projScaleX; // 射影行列の m[0][0]
    float projScaleY; // 射影行列の m[1][1]
    float mode;       // 0:キューブマップ 1:時間帯の空の色から計算
    float padding;
    float4 horizon;   // 地平線の色
    float4 zenith;    // 真上の色
    float4 ground;    // 地面側の色
    float4 toSun;     // 太陽（月）のある方向（xyz）
    float4 discColor; // 太陽の円盤の色（HDR。沈んでいれば0）
    float4 glowColor; // 太陽まわりのにじみの色
    float4 cloud;     // x=雲の量 y=星の量 z,w=雲の流れた量
    float4 cloudLit;  // rgb=雲の明るい側の色 w=経過秒
    float4 cloudShade;// rgb=雲の暗い側の色
    float4 moonDir;   // 月のある方向
    float4 moonColor; // 月の明るさ
};
ConstantBuffer<SkyConstants> gSky : register(b0);

// ---- 雲・星のための乱数とノイズ
float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.zyx + 31.32f);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float Fbm(float2 p)
{
    float v = 0.0f;
    float a = 0.5f;
    for (int k = 0; k < 4; ++k)
    {
        v += a * ValueNoise(p);
        p = p * 2.03f + float2(17.1f, 9.2f);
        a *= 0.5f;
    }
    return v;
}

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET0
{
    float3 dir = normalize(input.direction);
    float3 color;
    if (gSky.mode > 0.5f)
    {
        // 上が天頂の色、下が地面の色（Object3d.PS.hlsl の ProceduralSky と同じ式）
        if (dir.y >= 0.0f)
        {
            color = lerp(gSky.horizon.rgb, gSky.zenith.rgb, pow(dir.y, 0.5f));
        }
        else
        {
            color = lerp(gSky.horizon.rgb * 0.85f, gSky.ground.rgb, pow(-dir.y, 0.4f));
        }

        // 太陽：地平線ぞいの広いにじみ（夕焼けの赤み）＋ 円盤 ＋ 円盤のまわりの光
        float d = dot(dir, normalize(gSky.toSun.xyz));
        float nearHorizon = saturate(1.0f - abs(dir.y) * 2.5f);
        color += gSky.glowColor.rgb * (pow(saturate(d), 6.0f) * nearHorizon + pow(saturate(d), 48.0f) * 0.6f);
        if (dir.y > -0.02f)
        {
            float disc = smoothstep(0.9993f, 0.9996f, d);
            float halo = pow(saturate(d), 200.0f) * 0.05f;
            color += gSky.discColor.rgb * (disc + halo);

            // 月
            float dm = dot(dir, normalize(gSky.moonDir.xyz));
            color += gSky.moonColor.rgb * (smoothstep(0.9993f, 0.9996f, dm) + pow(saturate(dm), 90.0f) * 0.04f);
        }

        // 星（またたく小さな点）。夜だけ
        if (dir.y > 0.02f && gSky.cloud.y > 0.001f)
        {
            float3 sp = dir * 140.0f;
            float3 id = floor(sp);
            float3 fr = frac(sp) - 0.5f;
            float h = Hash31(id);
            float star = step(0.985f, h) * smoothstep(0.22f, 0.0f, length(fr));
            float twinkle = 0.6f + 0.4f * sin(gSky.cloudLit.w * 2.5f + h * 60.0f);
            color += float3(0.85f, 0.9f, 1.0f) * star * twinkle * gSky.cloud.y * 3.0f * smoothstep(0.02f, 0.2f, dir.y);
        }

        // 雲：空の平面に投影したノイズ。太陽側が明るく、反対側が暗い
        if (dir.y > 0.0f)
        {
            float2 p = dir.xz / (dir.y + 0.12f) * 0.9f + gSky.cloud.zw;
            float n = Fbm(p);
            float edge0 = lerp(0.95f, 0.30f, saturate(gSky.cloud.x));
            float dens = smoothstep(edge0, edge0 + 0.22f, n);
            dens *= smoothstep(0.0f, 0.18f, dir.y); // 地平線の近くはかすませる
            float n2 = Fbm(p + normalize(gSky.toSun.xz + float2(0.0001f, 0.0f)) * 0.12f);
            float lit = saturate(0.5f + (n - n2) * 2.5f);
            float3 cloudColor = lerp(gSky.cloudShade.rgb, gSky.cloudLit.rgb, lit);
            color = lerp(color, cloudColor, dens * 0.92f);
        }
    }
    else
    {
        color = gEnvironment.SampleLevel(gSampler, dir, 0).rgb;
    }
    // HDR（太陽など1を超える値）を簡単にトーンマップして画面に出す
    color = color / (1.0f + color * 0.15f);
    return float4(color, 1.0f);
}

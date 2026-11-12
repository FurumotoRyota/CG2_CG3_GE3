// ポストエフェクト（SSAO・ブルーム・トーンマップ）。1つのファイルに、パスごとの入口(エントリ)をまとめてある
//   PostProcess.cpp が VSMain と PS_xxx をそれぞれコンパイルして使う

cbuffer Params : register(b0)
{
    float4 gProj;     // x=射影のP00, y=P11, z=near, w=far
    float4 gTexel;    // x=入力1ピクセルの大きさU, y=V, z/w=ぼかしの向き
    float4 gAoP;      // x=半径, y=強さ, z=バイアス, w=合成時の濃さ
    float4 gBloomP;   // x=しきい値, y=ひざ, z=合成時の強さ
    float4 gGrade;    // x=露出, y=彩度, z=コントラスト, w=ビネット
    float4 gMode;     // x=トーンマップ(0/1), y=ポストエフェクトON(0なら素通し), z=AO使用, w=ブルーム使用
    float4 gWarm;     // 色味の掛け率(rgb)
    float4 gInvSize;  // x=1/画面幅, y=1/画面高さ（深度テクスチャ用）
    float4 gWeather;  // x=雨, y=砂ぼこり, z=経過秒, w=風
    float4 gRainCol;  // 雨の筋の色(rgb)
    float4 gDustCol;  // 砂ぼこりの色(rgb)
};

Texture2D<float4> gColor : register(t0);   // シーンの色（HDR）／ブルームの入力
Texture2D<float> gDepth : register(t1);    // 深度
Texture2D<float4> gAoTex : register(t2);   // AO
Texture2D<float4> gBloomTex : register(t3); // ブルーム
SamplerState sLinear : register(s0);
SamplerState sPoint : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// 画面全体を覆う三角形1枚（頂点バッファなし）
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.uv = uv;
    return o;
}

// ---------------------------------------------------------------- SSAO

float LinearizeDepth(float d)
{
    float n = gProj.z;
    float f = gProj.w;
    return n * f / (f - d * (f - n));
}

float SampleZ(float2 uv)
{
    return LinearizeDepth(gDepth.SampleLevel(sPoint, uv, 0));
}

// 画面の位置 uv と、その奥行き z から、カメラから見た3D位置を求める
float3 ViewPos(float2 uv, float z)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    return float3(ndc.x * z / gProj.x, ndc.y * z / gProj.y, z);
}

float Ign(float2 p)
{
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

float4 PS_SSAO(VSOut i) : SV_TARGET
{
    float d = gDepth.SampleLevel(sPoint, i.uv, 0);
    if (d >= 0.99999f)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f); // 空（何も描かれていない所）
    }
    float z = LinearizeDepth(d);
    float3 p = ViewPos(i.uv, z);

    // 周りの深度から、面の向き（カメラから見た法線）を求める。段差をまたがないよう、近いほうの隣を使う
    float2 ex = float2(gInvSize.x, 0.0f);
    float2 ey = float2(0.0f, gInvSize.y);
    float3 pr = ViewPos(i.uv + ex, SampleZ(i.uv + ex));
    float3 pl = ViewPos(i.uv - ex, SampleZ(i.uv - ex));
    float3 pd = ViewPos(i.uv + ey, SampleZ(i.uv + ey));
    float3 pu = ViewPos(i.uv - ey, SampleZ(i.uv - ey));
    float3 dxv = abs(pr.z - p.z) < abs(p.z - pl.z) ? (pr - p) : (p - pl);
    float3 dyv = abs(pd.z - p.z) < abs(p.z - pu.z) ? (pd - p) : (p - pu);
    float3 n = normalize(cross(dxv, dyv)); // カメラのほう(-z)を向く

    // ピクセルごとに回した、半球のサンプル点で「手前に何かあるか」を調べる
    float noise = Ign(i.pos.xy);
    float angle0 = noise * 6.2831853f;
    float3 rv = float3(cos(angle0), sin(angle0), 0.0f);
    float3 t = normalize(rv - n * dot(rv, n));
    float3 b = cross(n, t);

    const int kCount = 12;
    float occ = 0.0f;
    for (int k = 0; k < kCount; ++k)
    {
        float h = (k + 0.5f) / kCount;
        float a = (k + noise) * 2.3999632f;
        float r = sqrt(h);
        float3 dirT = float3(cos(a) * r, sin(a) * r, sqrt(max(0.0f, 1.0f - r * r)));
        float scale = lerp(0.15f, 1.0f, h * h);
        float3 sp = p + (t * dirT.x + b * dirT.y + n * dirT.z) * gAoP.x * scale;

        float2 suv = float2(sp.x * gProj.x / sp.z * 0.5f + 0.5f, 0.5f - sp.y * gProj.y / sp.z * 0.5f);
        if (suv.x < 0.0f || suv.x > 1.0f || suv.y < 0.0f || suv.y > 1.0f)
        {
            continue;
        }
        float sz = SampleZ(suv);
        float rangeCheck = smoothstep(0.0f, 1.0f, gAoP.x / max(abs(p.z - sz), 0.0001f));
        occ += (sz <= sp.z - gAoP.z ? 1.0f : 0.0f) * rangeCheck;
    }
    float ao = 1.0f - saturate(occ / kCount * gAoP.y);
    return float4(ao, ao, ao, 1.0f);
}

// 深度の差が大きい所（物の縁）をまたがないぼかし。gTexel.zw が向き
float4 PS_AO_BLUR(VSOut i) : SV_TARGET
{
    float zc = SampleZ(i.uv);
    float sum = 0.0f;
    float wsum = 0.0f;
    for (int k = -3; k <= 3; ++k)
    {
        float2 uv = i.uv + gTexel.zw * gTexel.xy * k;
        float ao = gColor.SampleLevel(sLinear, uv, 0).r;
        float dz = abs(SampleZ(uv) - zc);
        float w = exp(-k * k * 0.18f) * exp(-dz * 4.0f);
        sum += ao * w;
        wsum += w;
    }
    float v = sum / max(wsum, 0.0001f);
    return float4(v, v, v, 1.0f);
}

// ---------------------------------------------------------------- ブルーム

// 明るい所だけ取り出して、半分の大きさに縮める
float4 PS_BLOOM_PREFILTER(VSOut i) : SV_TARGET
{
    float2 o = gTexel.xy;
    float3 c = gColor.SampleLevel(sLinear, i.uv + float2(-o.x, -o.y) * 0.5f, 0).rgb
        + gColor.SampleLevel(sLinear, i.uv + float2(o.x, -o.y) * 0.5f, 0).rgb
        + gColor.SampleLevel(sLinear, i.uv + float2(-o.x, o.y) * 0.5f, 0).rgb
        + gColor.SampleLevel(sLinear, i.uv + float2(o.x, o.y) * 0.5f, 0).rgb;
    c *= 0.25f;
    float lum = max(c.r, max(c.g, c.b));
    float knee = max(gBloomP.y, 0.0001f);
    float rq = clamp(lum - gBloomP.x + knee, 0.0f, 2.0f * knee);
    rq = rq * rq / (4.0f * knee + 0.0001f);
    float w = max(rq, lum - gBloomP.x) / max(lum, 0.0001f);
    return float4(c * max(w, 0.0f), 1.0f);
}

float4 PS_BLOOM_DOWN(VSOut i) : SV_TARGET
{
    float2 o = gTexel.xy;
    float3 c = gColor.SampleLevel(sLinear, i.uv + float2(-o.x, -o.y) * 0.5f, 0).rgb
        + gColor.SampleLevel(sLinear, i.uv + float2(o.x, -o.y) * 0.5f, 0).rgb
        + gColor.SampleLevel(sLinear, i.uv + float2(-o.x, o.y) * 0.5f, 0).rgb
        + gColor.SampleLevel(sLinear, i.uv + float2(o.x, o.y) * 0.5f, 0).rgb;
    return float4(c * 0.25f, 1.0f);
}

// 小さいほうを引き伸ばして、大きいほうへ足す（足す設定は PSO のブレンドで行う）。3x3のテントフィルタ
float4 PS_BLOOM_UP(VSOut i) : SV_TARGET
{
    float2 o = gTexel.xy;
    float3 c = gColor.SampleLevel(sLinear, i.uv + float2(-o.x, -o.y), 0).rgb * 1.0f
        + gColor.SampleLevel(sLinear, i.uv + float2(0.0f, -o.y), 0).rgb * 2.0f
        + gColor.SampleLevel(sLinear, i.uv + float2(o.x, -o.y), 0).rgb * 1.0f
        + gColor.SampleLevel(sLinear, i.uv + float2(-o.x, 0.0f), 0).rgb * 2.0f
        + gColor.SampleLevel(sLinear, i.uv, 0).rgb * 4.0f
        + gColor.SampleLevel(sLinear, i.uv + float2(o.x, 0.0f), 0).rgb * 2.0f
        + gColor.SampleLevel(sLinear, i.uv + float2(-o.x, o.y), 0).rgb * 1.0f
        + gColor.SampleLevel(sLinear, i.uv + float2(0.0f, o.y), 0).rgb * 2.0f
        + gColor.SampleLevel(sLinear, i.uv + float2(o.x, o.y), 0).rgb * 1.0f;
    return float4(c / 16.0f, 1.0f);
}

// ---------------------------------------------------------------- 仕上げ

float3 AcesFilm(float3 x)
{
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

// ---------------------------------------------------------------- 天気（雨・砂ぼこり）

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 ip = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = Hash21(ip);
    float b = Hash21(ip + float2(1.0f, 0.0f));
    float c = Hash21(ip + float2(0.0f, 1.0f));
    float d = Hash21(ip + float2(1.0f, 1.0f));
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

// 雨の筋1層。cols 本ぶんの列に、ランダムな位置の縦の筋を出し、下へ流す
float RainLayer(float2 uv, float t, float density, float slant, float cols, float speed)
{
    uv.x += uv.y * slant;
    float2 g = float2(uv.x * cols, uv.y * cols * 0.18f - t * speed);
    float2 id = floor(g);
    float2 f = frac(g);
    float h = Hash21(id);
    float on = step(1.0f - density, Hash21(id + 17.0f));
    float xoff = 0.5f + (h - 0.5f) * 0.5f;
    float lineMask = smoothstep(0.12f, 0.0f, abs(f.x - xoff));
    float lenMask = smoothstep(0.0f, 0.15f, f.y) * smoothstep(0.85f, 0.35f, f.y);
    return on * lineMask * lenMask;
}

float4 PS_COMPOSITE(VSOut i) : SV_TARGET
{
    float3 c = gColor.SampleLevel(sLinear, i.uv, 0).rgb;

    // ポストエフェクトを使わない場面は、そのまま出す（これまでと同じ見た目）
    if (gMode.y < 0.5f)
    {
        return float4(saturate(c), 1.0f);
    }

    if (gMode.z > 0.5f)
    {
        float ao = gAoTex.SampleLevel(sLinear, i.uv, 0).r;
        c *= lerp(1.0f, ao, saturate(gAoP.w));
    }
    if (gMode.w > 0.5f)
    {
        c += gBloomTex.SampleLevel(sLinear, i.uv, 0).rgb * gBloomP.z;
    }

    // 雨：細い筋を3層重ねて足す（遠いほど細かく・速く）。画面が少し暗くなる
    if (gWeather.x > 0.001f)
    {
        float rain = RainLayer(i.uv, gWeather.z, 0.30f, 0.10f + gWeather.w * 0.25f, 70.0f, 7.0f)
            + RainLayer(i.uv + 0.31f, gWeather.z, 0.35f, 0.10f + gWeather.w * 0.25f, 120.0f, 10.0f) * 0.8f
            + RainLayer(i.uv + 0.67f, gWeather.z, 0.40f, 0.10f + gWeather.w * 0.25f, 200.0f, 14.0f) * 0.6f;
        c *= 1.0f - 0.10f * gWeather.x;
        c += gRainCol.rgb * rain * gWeather.x * 0.9f;
    }
    // 砂ぼこり：流れるもやと細かい砂粒で、砂色にかすむ
    if (gWeather.y > 0.001f)
    {
        float2 p = i.uv * float2(3.0f, 2.0f);
        p.x -= gWeather.z * (0.25f + 0.5f * gWeather.w);
        float haze = saturate(gWeather.y * (0.25f + 0.75f * Fbm(p)));
        c = lerp(c, gDustCol.rgb, haze * 0.6f);
        float grain = Hash21(floor(i.pos.xy * 0.5f) + floor(gWeather.z * 30.0f));
        c += gDustCol.rgb * step(0.985f, grain) * gWeather.y * 0.15f;
    }

    c *= gGrade.x * gWarm.rgb;
    if (gMode.x > 0.5f)
    {
        c = AcesFilm(c);
    }
    else
    {
        c = saturate(c);
    }

    // 彩度・コントラスト（見た目の明るさの空間でかける）
    float l = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    c = saturate(lerp(float3(l, l, l), c, gGrade.y));
    float3 g = pow(c, 1.0f / 2.2f);
    g = lerp(g, g * g * (3.0f - 2.0f * g), saturate(gGrade.z));
    c = pow(g, 2.2f);

    // 周辺を少し暗く
    float2 d = i.uv - 0.5f;
    c *= 1.0f - gGrade.w * saturate(dot(d, d) * 2.2f);

    return float4(c, 1.0f);
}

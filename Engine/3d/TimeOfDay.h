#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../base/ShaderTypes.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "EnvironmentMap.h"
#include "PostProcess.h"

/// <summary>
/// 時間帯（朝・昼・夕焼け・夜）をまとめて決める仕組み。時刻（0〜24）を1つ渡すだけで、
///   ・太陽の向き / 色 / 強さ（影の向きも）
///   ・空の色（地平線・天頂・地面）と 太陽の円盤・夕焼けのにじみ
///   ・環境光・映り込みの色
///   ・霧の色と濃さ
///   ・画面の色調（露出・彩度・暖かさ・周辺の暗さ）
/// が滑らかにつながって決まる。どのゲームでも使える（シーンは Evaluate → Apply 〜 を呼ぶだけ）。
///
/// さらに、
///   ・時間の流れ（tod.autoAdvance = true; tod.dayLengthSeconds = 120; で1日が120秒）
///   ・天気（晴れ・くもり・霧・雨・砂嵐。SetWeather(Weather::Rainy(), 3.0f) で3秒かけて変わる）
///   ・空の演出（雲・星・月）
/// も面倒を見る。
///
/// 使い方（シーン側。毎フレーム、描画の前に）:
///   TimeOfDay tod;  tod.hour = 17.5f;                      // 夕方
///   tod.Tick(dt);                                           // 時間と天気を進める（RenderLook を使うなら不要）
///   const TimeOfDay::Result r = tod.Evaluate();
///   tod.ApplyToLight(r, *object3dCommon->GetDirectionalLight());   // 光・環境光・霧（3Dオブジェクト用）
///   tod.ApplyToSky(r, EnvironmentMap::Instance());                  // 背景の空
///   tod.ApplyToPost(r, baseSettings, postProcess->GetSettings());   // 画面の色調（任意）
/// 元に戻すとき: EnvironmentMap::Instance().SetSky(SkyParams{});  と、保存しておいた DirectionalLight を書き戻す。
/// </summary>
/// <summary>
/// 天気。0〜1 の量の組み合わせで表す（雨の日は rain と cloud が高い、など）
/// </summary>
struct Weather
{
    float cloud = 0.25f; // 雲の量
    float rain = 0.0f;   // 雨
    float fog = 0.0f;    // 霧
    float dust = 0.0f;   // 砂ぼこり（砂嵐）
    float wind = 0.3f;   // 風（雲の流れ・雨の傾き・砂の流れ）

    static Weather Make(float cloud, float rain, float fog, float dust, float wind)
    {
        Weather w;
        w.cloud = cloud;
        w.rain = rain;
        w.fog = fog;
        w.dust = dust;
        w.wind = wind;
        return w;
    }
    static Weather Clear() { return Make(0.25f, 0.0f, 0.0f, 0.0f, 0.3f); }
    static Weather Cloudy() { return Make(0.80f, 0.0f, 0.05f, 0.0f, 0.5f); }
    static Weather Foggy() { return Make(0.60f, 0.0f, 0.90f, 0.0f, 0.15f); }
    static Weather Rainy() { return Make(0.95f, 0.80f, 0.35f, 0.0f, 0.70f); }
    static Weather Dusty() { return Make(0.55f, 0.0f, 0.20f, 0.90f, 1.00f); }

    static Weather Lerp(const Weather& a, const Weather& b, float t)
    {
        return Make(a.cloud + (b.cloud - a.cloud) * t, a.rain + (b.rain - a.rain) * t, a.fog + (b.fog - a.fog) * t,
            a.dust + (b.dust - a.dust) * t, a.wind + (b.wind - a.wind) * t);
    }
};

class TimeOfDay
{
public:
    // Evaluate() の結果（1時刻ぶん）
    struct Result
    {
        Vector3 lightDirection;     // 光の進む向き（DirectionalLight::direction と同じ）
        Vector4 lightColor;
        float lightIntensity = 1.0f;

        Vector3 toSun;              // 太陽のある方向（空の描画用。地平線の下にもなる）
        float sunHeightDeg = 0.0f;  // 太陽の高さ（度。0で地平線、負で沈んでいる）

        Vector3 skyHorizon, skyZenith, skyGround;
        Vector3 discColor, glowColor;

        Vector3 fogColor;
        float fogDensity = 0.0f;

        // 空の演出
        float cloudCover = 0.0f;    // 雲の量
        float starAmount = 0.0f;    // 星の見え方（夜に1）
        Vector3 toMoon;             // 月のある方向
        Vector3 moonColor;          // 月の明るさ（HDR。昼は0）
        Vector3 cloudLit, cloudShade; // 雲の明るい側・暗い側の色
        float cloudOffsetX = 0.0f, cloudOffsetZ = 0.0f; // 雲の流れた量
        float clock = 0.0f;         // 経過秒（星のまたたき・雨の動き用）
        // 天気と夜
        float rain = 0.0f, dust = 0.0f, wind = 0.0f;
        Vector3 rainColor, dustColor;
        float darkness = 0.0f;      // 暗さ（0:明るい 1:夜）。夜の明かりを灯す目安
        float wetness = 0.0f;       // 地面や壁の濡れ具合（雨が降り続けると上がり、止むとゆっくり乾く）

        float ambientScale = 1.0f;  // 環境光の倍率（1が基準）
        float exposure = 1.0f;      // 以下は、画面の色調の「倍率」（1が基準）
        float saturation = 1.0f;
        float warmth = 0.0f;
        float vignette = 1.0f;
    };

    float hour = 17.5f;              // 時刻（0〜24）。12が昼、18が日没
    float azimuthAtNoon = 137.5f;    // 正午の太陽の向き（度。太陽の通り道の向き）。0で奥(+z)、90で右(+x)
    float maxElevation = 65.0f;      // 正午の太陽の高さの最大（度）
    float minLightElevation = 8.0f;  // 光の高さの下限（度）。低すぎて影が果てしなく伸びるのを防ぐ
    float fogScale = 1.0f;           // 霧の濃さの倍率
    float fogStart = 16.0f;          // 霧が始まるカメラからの距離

    // 時間の流れ
    bool autoAdvance = false;        // true なら Tick で時刻が進む
    float dayLengthSeconds = 120.0f; // 1日（24時間）が何秒か

    // 天気（いまの値。SetWeather で目標へ向かって変わる。つまみで直接書き換えてもよい）
    Weather weather;
    float clock = 0.0f;              // 経過秒

    // 濡れ具合：雨が降ると wetSeconds 秒で濡れ、止むと drySeconds 秒かけて乾く。つまみで直接書き換えてもよい
    float wetness = 0.0f;
    bool wetnessAuto = true;         // false なら自動では変わらない
    float wetSeconds = 25.0f;
    float drySeconds = 120.0f;

    float cloudOffsetX = 0.0f;       // 雲の流れた量
    float cloudOffsetZ = 0.0f;

    // 天気を、seconds 秒かけて目標へ変える（0なら一瞬）
    void SetWeather(const Weather& target, float seconds)
    {
        weatherFrom_ = weather;
        weatherTarget_ = target;
        weatherT_ = 0.0f;
        weatherDur_ = seconds;
        if (seconds <= 0.0f) {
            weather = target;
        }
    }
    // 天気の途中経過を止める（つまみで直接変えたいとき）
    void StopWeatherTransition() { weatherDur_ = 0.0f; }

    // 時間と天気を dt 秒ぶん進める（毎フレーム）
    void Tick(float dt)
    {
        clock += dt;
        if (autoAdvance && dayLengthSeconds > 0.1f) {
            hour += 24.0f / dayLengthSeconds * dt;
            if (hour >= 24.0f) { hour -= 24.0f; }
        }
        if (weatherDur_ > 0.0f) {
            weatherT_ += dt;
            const float t = (std::min)(weatherT_ / weatherDur_, 1.0f);
            weather = Weather::Lerp(weatherFrom_, weatherTarget_, t);
            if (t >= 1.0f) { weatherDur_ = 0.0f; }
        }
        if (wetnessAuto) {
            if (weather.rain > 0.02f) {
                const float target = (std::min)(1.0f, weather.rain * 1.5f);
                if (wetness < target) {
                    wetness = (std::min)(target, wetness + dt / (std::max)(wetSeconds, 0.1f));
                }
            }
            else {
                wetness = (std::max)(0.0f, wetness - dt / (std::max)(drySeconds, 0.1f));
            }
        }
        cloudOffsetX += (0.010f + weather.wind * 0.035f) * dt;
        cloudOffsetZ += 0.004f * dt;
    }

    // 現在の時刻の見た目を計算する
    Result Evaluate() const
    {
        constexpr float kPi = 3.14159265f;
        constexpr float kDeg = kPi / 180.0f;

        float h = std::fmod(hour, 24.0f);
        if (h < 0.0f) { h += 24.0f; }

        // 太陽の位置：6時に東の地平線、12時に真上、18時に西の地平線
        const float phase = (h - 6.0f) / 24.0f * 2.0f * kPi;
        const float el = maxElevation * std::sin(phase);               // 度
        const float az = (azimuthAtNoon + (h - 12.0f) * 15.0f) * kDeg; // ラジアン

        Result r;
        r.sunHeightDeg = el;
        r.toSun = Dir(el * kDeg, az);

        // 光の向き：太陽（夜は月の代わりに同じ通り道の低い位置）。低すぎない高さにそろえる
        const float lightEl = (std::max)(el, minLightElevation);
        const Vector3 toLight = Dir(lightEl * kDeg, az);
        r.lightDirection = { -toLight.x, -toLight.y, -toLight.z };

        // キー（時刻ごとの設定）の間をなめらかにつなぐ
        const Key k = Blend(h);
        r.ambientScale = k.ambient;
        r.exposure = k.exposure;
        r.warmth = k.warmth;
        r.vignette = k.vignette;

        // ---- 天気の影響
        const Weather& w = weather;
        const float cover = (std::max)({ w.cloud, w.rain * 0.95f, w.fog * 0.5f, w.dust * 0.6f });
        const float overcast = (std::min)((std::max)((cover - 0.3f) / 0.7f, 0.0f), 1.0f); // うっすらした雲では光は弱まらない
        const float desat = cover * 0.65f;
        const float darken = 0.35f * w.rain + 0.15f * cover;
        auto sky = [&](const float c[3]) {
            const float l = c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
            return Vector3{ (c[0] + (l - c[0]) * desat) * (1.0f - darken), (c[1] + (l - c[1]) * desat) * (1.0f - darken), (c[2] + (l - c[2]) * desat) * (1.0f - darken) };
        };
        r.skyHorizon = sky(k.horizon);
        r.skyZenith = sky(k.zenith);
        r.skyGround = sky(k.ground);

        // 光：雲と雨で弱まる。砂嵐ではオレンジがかる
        r.lightIntensity = k.sunI * (1.0f - 0.55f * overcast - 0.2f * w.rain);
        {
            const float d = 0.5f * w.dust;
            r.lightColor = { k.sun[0] + (1.00f - k.sun[0]) * d, k.sun[1] + (0.65f - k.sun[1]) * d, k.sun[2] + (0.35f - k.sun[2]) * d, 1.0f };
        }
        r.saturation = k.saturation * (1.0f - 0.25f * w.rain);

        // 霧：霧・雨・砂嵐で濃くなる。色は灰色（砂嵐は砂色）に寄る
        const float fogLum = k.fog[0] * 0.2126f + k.fog[1] * 0.7152f + k.fog[2] * 0.0722f;
        r.fogDensity = k.fogD * fogScale * (1.0f + 3.0f * w.fog + 1.5f * w.rain + 2.5f * w.dust);
        {
            const float g = (std::min)(w.fog * 0.7f + w.rain * 0.6f + cover * 0.2f, 1.0f);
            Vector3 fc = { k.fog[0] + (fogLum - k.fog[0]) * g, k.fog[1] + (fogLum - k.fog[1]) * g, k.fog[2] + (fogLum - k.fog[2]) * g };
            const float lumScale = (std::min)((std::max)(fogLum * 1.3f, 0.05f), 1.0f);
            const Vector3 dustCol = { 0.82f * lumScale, 0.62f * lumScale, 0.40f * lumScale };
            r.fogColor = { fc.x + (dustCol.x - fc.x) * w.dust, fc.y + (dustCol.y - fc.y) * w.dust, fc.z + (dustCol.z - fc.z) * w.dust };
            r.dustColor = dustCol;
        }

        // 太陽の円盤は地平線の少し下まで見える。夕焼けのにじみは沈んだあともしばらく残る。雲があるほど弱まる
        const float disc = SmoothStep(-3.0f, 0.5f, el) * (1.0f - overcast);
        const float glow = 0.35f * SmoothStep(-9.0f, 2.0f, el) * (1.0f - 0.7f * overcast);
        r.discColor = { k.sun[0] * 30.0f * disc, k.sun[1] * 30.0f * disc, k.sun[2] * 30.0f * disc };
        r.glowColor = { k.sun[0] * glow, k.sun[1] * glow, k.sun[2] * glow };

        // ---- 空の演出：月・星・雲
        r.toMoon = { -r.toSun.x, -r.toSun.y, -r.toSun.z };
        const float night = SmoothStep(0.0f, -8.0f, el);
        const float moonUp = SmoothStep(-0.02f, 0.12f, r.toMoon.y);
        const float moon = 3.0f * night * moonUp * (1.0f - cover * 0.9f);
        r.moonColor = { 0.75f * moon, 0.82f * moon, 1.00f * moon };
        r.starAmount = SmoothStep(-4.0f, -14.0f, el) * (1.0f - cover * 0.9f);
        r.cloudCover = cover;
        r.cloudOffsetX = cloudOffsetX;
        r.cloudOffsetZ = cloudOffsetZ;
        r.clock = clock;
        const float sunEnergy = k.sunI * 0.8f * (1.0f - 0.4f * w.rain);
        r.cloudLit = { k.sun[0] * sunEnergy, k.sun[1] * sunEnergy, k.sun[2] * sunEnergy };
        r.cloudShade = { (r.skyHorizon.x + r.skyZenith.x) * 0.35f + 0.01f, (r.skyHorizon.y + r.skyZenith.y) * 0.35f + 0.01f, (r.skyHorizon.z + r.skyZenith.z) * 0.35f + 0.02f };

        // ---- 天気の演出と夜の暗さ
        r.rain = w.rain;
        r.wetness = wetness;
        r.dust = w.dust;
        r.wind = w.wind;
        {
            const float l = r.skyHorizon.x * 0.2126f + r.skyHorizon.y * 0.7152f + r.skyHorizon.z * 0.0722f;
            r.rainColor = { (l * 0.8f + 0.03f) * 0.9f, (l * 0.8f + 0.03f) * 0.95f, (l * 0.8f + 0.03f) * 1.05f };
        }
        r.darkness = (std::min)(SmoothStep(8.0f, -6.0f, el) + 0.15f * cover + 0.2f * w.rain, 1.0f);
        return r;
    }

    // 3Dオブジェクト用の光・環境光・霧を DirectionalLight に書く（影の設定などはそのまま）
    // ambientUser は、シーン側のつまみ（環境光・映り込み）。基準は 0.5
    void ApplyToLight(const Result& r, DirectionalLight& light, float ambientUser = 0.5f, float puddleAmount = 0.5f) const
    {
        light.wetness = r.wetness;
        light.puddleAmount = puddleAmount;
        light.rainTime = r.clock;
        light.rainAmount = r.rain;
        light.direction = r.lightDirection;
        light.color = r.lightColor;
        light.intensity = r.lightIntensity;
        light.ambientSky = ambientUser * r.ambientScale;

        light.skyMode = 1;
        light.skyHorizon = r.skyHorizon;
        light.skyZenith = r.skyZenith;
        light.skyGround = r.skyGround;
        light.fogColor = r.fogColor;
        light.fogDensity = r.fogDensity;
        light.fogStart = fogStart;
    }

    // 背景の空に反映する
    void ApplyToSky(const Result& r, EnvironmentMap& env) const
    {
        SkyParams sky;
        sky.procedural = true;
        sky.horizon = r.skyHorizon;
        sky.zenith = r.skyZenith;
        sky.ground = r.skyGround;
        sky.toSun = r.toSun;
        sky.discColor = r.discColor;
        sky.glowColor = r.glowColor;
        sky.cloudCover = r.cloudCover;
        sky.starAmount = r.starAmount;
        sky.cloudOffsetX = r.cloudOffsetX;
        sky.cloudOffsetZ = r.cloudOffsetZ;
        sky.time = r.clock;
        sky.cloudLit = r.cloudLit;
        sky.cloudShade = r.cloudShade;
        sky.toMoon = r.toMoon;
        sky.moonColor = r.moonColor;
        env.SetSky(sky);
    }

    // 画面の色調に反映する。base はシーン側の基準（つまみの値）に、時間帯の倍率をかけて out へ書く
    void ApplyToPost(const Result& r, const PostProcess::Settings& base, PostProcess::Settings& out) const
    {
        out = base;
        out.exposure = base.exposure * r.exposure;
        out.saturation = base.saturation * r.saturation;
        out.vignette = base.vignette * r.vignette;
        out.warmth = (std::min)((std::max)(base.warmth * r.warmth, 0.0f), 1.0f);
        out.rain = r.rain;
        out.dust = r.dust;
        out.wind = r.wind;
        out.weatherTime = r.clock;
        out.rainColor[0] = r.rainColor.x; out.rainColor[1] = r.rainColor.y; out.rainColor[2] = r.rainColor.z;
        out.dustColor[0] = r.dustColor.x; out.dustColor[1] = r.dustColor.y; out.dustColor[2] = r.dustColor.z;
    }

    // 時間帯の名前（画面表示用）
    static const char* Label(float hour)
    {
        float h = std::fmod(hour, 24.0f);
        if (h < 0.0f) { h += 24.0f; }
        if (h < 4.5f || h >= 21.0f) { return "夜"; }
        if (h < 6.0f) { return "夜明け前"; }
        if (h < 7.5f) { return "朝焼け"; }
        if (h < 11.0f) { return "朝"; }
        if (h < 15.0f) { return "昼"; }
        if (h < 17.0f) { return "午後"; }
        if (h < 18.2f) { return "夕焼け（夕日）"; }
        if (h < 19.5f) { return "夕暮れの余韻"; }
        return "薄暮";
    }

private:
    Weather weatherFrom_, weatherTarget_;
    float weatherT_ = 0.0f;
    float weatherDur_ = 0.0f;

    // 時刻ごとの設定。色はHDR（空の明るさ）、倍率は 1 が基準
    struct Key
    {
        float hour;
        float horizon[3], zenith[3], ground[3];
        float sun[3];
        float sunI;
        float fog[3];
        float fogD;
        float exposure, saturation, warmth, vignette;
        float ambient;
    };
    static constexpr int kKeyFloats = 23;
    static_assert(sizeof(Key) == sizeof(float) * kKeyFloats, "Key must be a plain array of floats");

    static float SmoothStep(float a, float b, float x)
    {
        float t = (x - a) / (b - a);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }

    // 高さ elevation・向き azimuth（ラジアン）の単位ベクトル
    static Vector3 Dir(float elevation, float azimuth)
    {
        return { std::cos(elevation) * std::sin(azimuth), std::sin(elevation), std::cos(elevation) * std::cos(azimuth) };
    }

    static Key Blend(float h)
    {
        // hour, horizon, zenith, ground, sun, sunI, fog, fogD, exposure, saturation, warmth, vignette, ambient
        static const Key kKeys[] = {
            { 0.0f, {0.10f,0.13f,0.26f}, {0.02f,0.04f,0.12f}, {0.05f,0.05f,0.08f}, {0.55f,0.65f,1.00f}, 0.35f, {0.08f,0.11f,0.22f}, 0.020f, 1.60f, 0.85f, 0.0f, 1.4f, 1.0f },  // 夜
            { 5.0f, {0.30f,0.22f,0.35f}, {0.04f,0.06f,0.18f}, {0.08f,0.07f,0.09f}, {0.60f,0.55f,0.80f}, 0.35f, {0.30f,0.25f,0.36f}, 0.018f, 1.45f, 0.90f, 0.3f, 1.3f, 1.0f },  // 夜明け前
            { 6.3f, {1.00f,0.62f,0.38f}, {0.25f,0.32f,0.62f}, {0.25f,0.18f,0.15f}, {1.00f,0.62f,0.35f}, 0.85f, {0.95f,0.65f,0.50f}, 0.022f, 1.05f, 1.10f, 1.2f, 1.1f, 0.9f },  // 日の出
            { 8.0f, {0.85f,0.88f,0.95f}, {0.22f,0.42f,0.88f}, {0.30f,0.27f,0.24f}, {1.00f,0.94f,0.82f}, 1.15f, {0.78f,0.82f,0.90f}, 0.010f, 0.95f, 1.00f, 0.4f, 0.8f, 1.0f },  // 朝
            { 12.0f, {0.78f,0.86f,1.00f}, {0.16f,0.36f,0.85f}, {0.30f,0.27f,0.24f}, {1.00f,0.97f,0.90f}, 1.25f, {0.70f,0.80f,0.95f}, 0.008f, 0.90f, 1.00f, 0.0f, 0.6f, 1.0f },  // 昼
            { 16.0f, {0.90f,0.85f,0.85f}, {0.18f,0.36f,0.80f}, {0.30f,0.26f,0.22f}, {1.00f,0.90f,0.72f}, 1.20f, {0.85f,0.78f,0.75f}, 0.012f, 0.95f, 1.05f, 0.6f, 0.8f, 1.0f },  // 午後
            { 17.5f, {1.00f,0.55f,0.28f}, {0.20f,0.28f,0.60f}, {0.28f,0.18f,0.14f}, {1.00f,0.72f,0.42f}, 1.15f, {0.95f,0.60f,0.40f}, 0.022f, 1.00f, 1.10f, 1.4f, 1.1f, 0.9f },  // 夕日（既定）
            { 18.0f, {1.00f,0.42f,0.22f}, {0.16f,0.20f,0.50f}, {0.22f,0.13f,0.11f}, {1.00f,0.55f,0.28f}, 0.90f, {0.90f,0.45f,0.32f}, 0.026f, 1.05f, 1.10f, 1.6f, 1.2f, 0.9f },  // 日没
            { 18.8f, {0.75f,0.32f,0.30f}, {0.09f,0.11f,0.32f}, {0.12f,0.08f,0.09f}, {0.85f,0.45f,0.50f}, 0.50f, {0.55f,0.30f,0.32f}, 0.026f, 1.20f, 1.00f, 1.0f, 1.3f, 0.9f },  // 夕焼けの余韻
            { 19.7f, {0.45f,0.28f,0.40f}, {0.05f,0.07f,0.20f}, {0.08f,0.06f,0.07f}, {0.45f,0.50f,0.85f}, 0.35f, {0.25f,0.20f,0.32f}, 0.024f, 1.40f, 0.95f, 0.4f, 1.3f, 1.0f },  // 薄暮
            { 21.0f, {0.10f,0.13f,0.26f}, {0.02f,0.04f,0.12f}, {0.05f,0.05f,0.08f}, {0.55f,0.65f,1.00f}, 0.35f, {0.08f,0.11f,0.22f}, 0.020f, 1.60f, 0.85f, 0.0f, 1.4f, 1.0f },  // 夜
            { 24.0f, {0.10f,0.13f,0.26f}, {0.02f,0.04f,0.12f}, {0.05f,0.05f,0.08f}, {0.55f,0.65f,1.00f}, 0.35f, {0.08f,0.11f,0.22f}, 0.020f, 1.60f, 0.85f, 0.0f, 1.4f, 1.0f },  // 夜（0時と同じ）
        };
        constexpr int kCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

        int i = 0;
        while (i < kCount - 2 && h >= kKeys[i + 1].hour) {
            ++i;
        }
        const Key& a = kKeys[i];
        const Key& b = kKeys[i + 1];
        const float t = SmoothStep(a.hour, b.hour, h);

        Key out{};
        const float* pa = reinterpret_cast<const float*>(&a);
        const float* pb = reinterpret_cast<const float*>(&b);
        float* po = reinterpret_cast<float*>(&out);
        for (int n = 0; n < kKeyFloats; ++n) {
            po[n] = pa[n] + (pb[n] - pa[n]) * t;
        }
        out.hour = h;
        return out;
    }
};

#include "RenderLook.h"

#include <d3d12.h>

#include "../base/DirectXCommon.h"
#include "../scene/SceneContext.h"
#include "EnvironmentMap.h"
#include "Object3dCommon.h"
#include "ShadowMap.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

RenderLook::~RenderLook()
{
    rain_.Finalize();
}

void RenderLook::Begin(SceneContext* ctx)
{
    ctx_ = ctx;
    if (!saved_ && ctx_ && ctx_->object3dCommon) {
        savedLight_ = *ctx_->object3dCommon->GetDirectionalLight();
        saved_ = true;
    }
}

void RenderLook::End()
{
    if (!ctx_) {
        return;
    }
    if (saved_) {
        *ctx_->object3dCommon->GetDirectionalLight() = savedLight_;
        saved_ = false;
    }
    EnvironmentMap::Instance().SetSky(SkyParams{}); // 空は、キューブマップに戻す
    if (ctx_->postProcess) {
        ctx_->postProcess->GetSettings() = PostProcess::Settings{}; // ほかの場面では、ポストエフェクトを使わない
    }
}

void RenderLook::Update(const Matrix4x4& projection, const Vector3& shadowCenter, float dt)
{
    if (!ctx_ || !ctx_->object3dCommon) {
        return;
    }
    DirectionalLight* L = ctx_->object3dCommon->GetDirectionalLight();

    // 時間帯：太陽の向き・色・強さ、空、環境光、霧をまとめて決める（時間と天気も進める）
    tod.Tick(dt);
    const TimeOfDay::Result r = tod.Evaluate();
    tod.ApplyToLight(r, *L, skyAmbient, puddleAmount);
    tod.ApplyToSky(r, EnvironmentMap::Instance());

    L->shadowEnabled = shadowOn ? 1 : 0;
    L->shadowBias = shadowBias;
    L->shadowStrength = shadowStrength;
    L->shadowRange = shadowRange;
    L->shadowSoftness = shadowSoft;
    L->shadowQuality = shadowQuality;
    L->groundAo = groundAo;

    // 夜の明かり：暗さに応じて、見ている場所に近いものから点灯する
    if (lampsOn) {
        lamps.Apply(*L, r.darkness, r.clock, shadowCenter);
    }
    else {
        L->lightCount = 0;
    }

    // ポストエフェクト：つまみの値 × 時間帯の倍率
    if (ctx_->postProcess) {
        post.enabled = true;
        tod.ApplyToPost(r, post, ctx_->postProcess->GetSettings());
        if (rain3D) {
            ctx_->postProcess->GetSettings().rain = 0.0f; // 雨は世界に降らせるので、画面に重ねる雨は使わない
        }
        ctx_->postProcess->SetProjection(projection, nearZ, farZ);
    }

    // 3Dの雨の見た目（強さ・風・色・時間）
    rainParams.intensity = rain3D ? r.rain : 0.0f;
    rainParams.wind = r.wind;
    rainParams.color = { r.rainColor.x * rainBrightness, r.rainColor.y * rainBrightness, r.rainColor.z * rainBrightness };
    rainParams.time = r.clock;

    // 影の範囲
    ctx_->object3dCommon->SetLightViewProjection(ShadowMap::ComputeLightViewProjection(L->direction, shadowCenter, shadowRange));
}

void RenderLook::DrawWeather(const Matrix4x4& view, const Matrix4x4& projection, const Vector3& focus)
{
    if (!ctx_) {
        return;
    }
    if (rainParams.intensity <= 0.005f) {
        ctx_->dxCommon->GpuMark("objects"); // 雨がないときも、ここまでの時間は測る
        return;
    }
    if (!rainReady_) {
        rain_.Initialize(ctx_->dxCommon); // 初めて雨が降るときに作る
        rainReady_ = true;
    }
    ctx_->dxCommon->GpuMark("objects"); // ここまでが、空と3Dオブジェクト
    rain_.Draw(ctx_->dxCommon->GetCommandList(), view, projection, focus, rainParams);
    ctx_->dxCommon->GpuMark("rain");
}

bool RenderLook::BeginShadowPass()
{
    if (!ctx_ || !shadowOn) {
        shadowPassMs_ = 0.0f;
        return false;
    }
    shadowT0_ = std::chrono::steady_clock::now();
    ShadowMap::Instance().Begin(ctx_->dxCommon->GetCommandList());
    return true;
}

void RenderLook::EndShadowPass()
{
    ShadowMap::Instance().End(ctx_->dxCommon->GetCommandList());
    ctx_->dxCommon->GpuMark("shadow");
    shadowPassMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - shadowT0_).count();
}

void RenderLook::DrawImGui()
{
#ifdef USE_IMGUI
    // GPUの時間（1フレーム前のもの）。どの処理が重いかを見る
    if (ctx_) {
        float total = 0.0f;
        for (const DirectXCommon::GpuTiming& t : ctx_->dxCommon->GetGpuTimings()) {
            ImGui::Text("GPU %-10s %.2f ms", t.name, t.ms);
            total += t.ms;
        }
        ImGui::Text("GPU 合計 %.2f ms", total);
        ImGui::Separator();
    }
    ImGui::Text("影パスの命令 %.2f ms", shadowPassMs_);
    ImGui::Checkbox("影を描く", &shadowOn);
    ImGui::SliderInt("影の品質 (0:3x3 1:やわらかい)", &shadowQuality, 0, 1);
    ImGui::SliderFloat("影のやわらかさ", &shadowSoft, 0.0f, 0.25f, "%.3f");
    ImGui::SliderFloat("影の範囲", &shadowRange, 12.0f, 30.0f, "%.0f");
    ImGui::Separator();
    ImGui::SliderFloat("時刻", &tod.hour, 0.0f, 24.0f, "%.1f 時");
    ImGui::SameLine();
    ImGui::Text("%s", TimeOfDay::Label(tod.hour));
    if (ImGui::Button("朝")) { tod.hour = 8.0f; }
    ImGui::SameLine();
    if (ImGui::Button("昼")) { tod.hour = 12.0f; }
    ImGui::SameLine();
    if (ImGui::Button("夕日")) { tod.hour = 17.5f; }
    ImGui::SameLine();
    if (ImGui::Button("日没")) { tod.hour = 18.0f; }
    ImGui::SameLine();
    if (ImGui::Button("夜")) { tod.hour = 22.0f; }
    ImGui::Checkbox("時間を進める", &tod.autoAdvance);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("1日の長さ(秒)", &tod.dayLengthSeconds, 10.0f, 600.0f, "%.0f");
    ImGui::SliderFloat("太陽の通り道の向き", &tod.azimuthAtNoon, -180.0f, 180.0f, "%.0f");
    ImGui::Text("天気");
    if (ImGui::Button("晴れ")) { tod.SetWeather(Weather::Clear(), 3.0f); }
    ImGui::SameLine();
    if (ImGui::Button("くもり")) { tod.SetWeather(Weather::Cloudy(), 3.0f); }
    ImGui::SameLine();
    if (ImGui::Button("霧")) { tod.SetWeather(Weather::Foggy(), 3.0f); }
    ImGui::SameLine();
    if (ImGui::Button("雨")) { tod.SetWeather(Weather::Rainy(), 3.0f); }
    ImGui::SameLine();
    if (ImGui::Button("砂嵐")) { tod.SetWeather(Weather::Dusty(), 3.0f); }
    bool weatherEdited = false;
    weatherEdited |= ImGui::SliderFloat("雲の量", &tod.weather.cloud, 0.0f, 1.0f, "%.2f");
    weatherEdited |= ImGui::SliderFloat("雨", &tod.weather.rain, 0.0f, 1.0f, "%.2f");
    weatherEdited |= ImGui::SliderFloat("霧（天気）", &tod.weather.fog, 0.0f, 1.0f, "%.2f");
    weatherEdited |= ImGui::SliderFloat("砂ぼこり", &tod.weather.dust, 0.0f, 1.0f, "%.2f");
    weatherEdited |= ImGui::SliderFloat("風", &tod.weather.wind, 0.0f, 1.0f, "%.2f");
    if (weatherEdited) {
        tod.StopWeatherTransition(); // つまみで直接変えたら、途中経過は止める
    }
    ImGui::Checkbox("雨を世界に降らせる", &rain3D);
    ImGui::SliderFloat("雨の筋の明るさ", &rainBrightness, 0.3f, 4.0f, "%.2f");
    ImGui::SliderFloat("濡れ具合", &tod.wetness, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("濡れ具合を自動で変える", &tod.wetnessAuto);
    ImGui::SliderFloat("水たまり", &puddleAmount, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("夜の明かりを使う", &lampsOn);
    ImGui::SameLine();
    ImGui::Text("(登録 %zu)", lamps.Count());
    ImGui::SliderFloat("霧の濃さ", &tod.fogScale, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("霧の始まる距離", &tod.fogStart, 5.0f, 40.0f, "%.0f");
    ImGui::SliderFloat("環境光・映り込み", &skyAmbient, 0.0f, 1.5f, "%.2f");
    ImGui::SliderFloat("足もとの暗がり", &groundAo, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("ポストエフェクト");
    ImGui::Checkbox("SSAO（隅の暗がり）", &post.ssao);
    ImGui::SliderFloat("AOの半径", &post.aoRadius, 0.2f, 2.0f, "%.2f");
    ImGui::SliderFloat("AOの強さ", &post.aoIntensity, 0.2f, 3.0f, "%.2f");
    ImGui::SliderFloat("AOの濃さ", &post.aoStrength, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("ブルーム", &post.bloom);
    ImGui::SliderFloat("ブルームのしきい値", &post.bloomThreshold, 0.2f, 2.0f, "%.2f");
    ImGui::SliderFloat("ブルームの強さ", &post.bloomIntensity, 0.0f, 1.5f, "%.2f");
    ImGui::Checkbox("トーンマップ", &post.tonemap);
    ImGui::SliderFloat("露出", &post.exposure, 0.5f, 2.0f, "%.2f");
    ImGui::SliderFloat("彩度", &post.saturation, 0.5f, 1.6f, "%.2f");
    ImGui::SliderFloat("コントラスト", &post.contrast, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("周辺の暗さ", &post.vignette, 0.0f, 0.8f, "%.2f");
    ImGui::SliderFloat("夕方の色味", &post.warmth, 0.0f, 1.0f, "%.2f");
#endif // USE_IMGUI
}

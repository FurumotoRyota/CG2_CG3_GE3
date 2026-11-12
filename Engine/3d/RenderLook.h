#pragma once
#include <chrono>

#include "../base/ShaderTypes.h"
#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "PostProcess.h"
#include "RainSystem.h"
#include "TimeOfDay.h"
#include "TimedLights.h"

struct SceneContext;

/// <summary>
/// 見た目（画質）一式をまとめたもの。どのシーンでも、数行で「やわらかい影・PBRの映り込み・足もとの暗がり・
/// 時間帯（太陽/空/霧）・SSAO/ブルーム/トーンマップ」をまとめてONにできる。
///
/// 使い方（シーン側）:
///   // メンバ:  RenderLook look_;
///   // Initialize:  look_.Begin(ctx_);                    // 入る前の光源を保存して、見た目をON
///   //              look_.tod.hour = 17.5f;               // 時刻（任意。既定は夕日）
///   // Update（毎フレーム、各Object3dのUpdateより前）:
///   //              look_.Update(camera_.GetProjectionMatrix(), shadowCenter);
///   // Update の最後（影を描く）:
///   //              if (look_.BeginShadowPass()) { 影を落とすものの DrawShadow(commandList)...; look_.EndShadowPass(); }
///   // DrawImGui:   look_.DrawImGui();                    // 「画質」つまみ一式（任意）
///   // Finalize:    look_.End();                          // 光源・空・ポスト設定を元に戻す
///
/// 影を落とす/受ける物の設定（Object3d側）は今までどおり（castShadow / receiveShadow、PBRは Material の roughness / metallic）。
/// </summary>
class RenderLook
{
public:
    // ---- 影
    bool shadowOn = true;
    int shadowQuality = 1;         // 0:3x3のぼかし 1:やわらかい影（距離でぼける）
    float shadowSoft = 0.06f;      // 太陽の見かけの大きさ（影のぼけ具合）
    float shadowRange = 19.0f;     // 影を描く範囲の半径（ワールド）
    float shadowBias = 0.0015f;
    float shadowStrength = 0.85f;
    // ---- 光・空気
    float skyAmbient = 0.5f;       // 空の環境光（PBRの環境反射の倍率でもある。0.5で等倍）
    float groundAo = 0.55f;        // 壁の足もとの暗がり（0〜1）
    TimeOfDay tod;                 // 時間帯・時間の流れ・天気（既定は夕日）
    TimedLights lamps;             // 夜に灯る明かり（窓・街灯など）。シーンが Clear → Add で登録する
    bool lampsOn = true;           // 夜の明かりを使うか
    bool rain3D = true;            // 雨を世界に降らせる（false なら画面に重ねる簡易の雨）
    float puddleAmount = 0.5f;     // 水たまりのできやすさ（0で水たまりなし）
    float rainBrightness = 1.6f;   // 雨の筋の明るさの倍率
    RainSystem::Params rainParams; // 雨の見た目（落ちる速さ・筋の長さ・太さ・降る範囲など）。強さ・風・色・時間は Update が毎フレーム書く
    // ---- ポストエフェクト（時間帯の倍率はこの値にかかる）
    PostProcess::Settings post;
    float nearZ = 0.1f;            // SSAO用のカメラのnear/far
    float farZ = 300.0f;

    // 入る前の光源を保存して、見た目を有効にする（シーンの Initialize で）
    void Begin(SceneContext* ctx);
    // 保存した光源・キューブマップの空・ポスト設定を元に戻す（シーンの Finalize で）
    void End();

    // 毎フレーム：時間帯→光/空/霧/色調、影の範囲と向き、ポストエフェクトの射影を反映する。
    // shadowCenter は影を描く範囲の中心（普通は画面の真ん中あたりの地面）
    // dt は前のフレームからの秒数（時間の流れ・天気・雲の動きに使う）
    void Update(const Matrix4x4& projection, const Vector3& shadowCenter, float dt = 1.0f / 60.0f);

    // 雨を描く（3Dオブジェクトを描いたあとに呼ぶ）。focus は注目している地面の点（その周りに雨が降る）。
    // 天気が雨でなければ何もしない
    void DrawWeather(const Matrix4x4& view, const Matrix4x4& projection, const Vector3& focus);

    // 影パス。true が返ったときだけ、影を落とす物の DrawShadow を呼んで、最後に EndShadowPass を呼ぶ
    bool BeginShadowPass();
    void EndShadowPass();
    float ShadowPassMs() const { return shadowPassMs_; } // 影パスの命令を積むのにかかった時間（確認用）

    // 「画質」つまみ一式（ImGuiのウィンドウの中で呼ぶ。USE_IMGUI でなければ何もしない）
    void DrawImGui();

    // 手早く雰囲気を変えるプリセット（時刻だけ変える）
    void SetTime(float hour) { tod.hour = hour; }

public:
    RenderLook() = default;
    ~RenderLook();
    RenderLook(const RenderLook&) = delete;
    RenderLook& operator=(const RenderLook&) = delete;

private:
    SceneContext* ctx_ = nullptr;
    RainSystem rain_;
    bool rainReady_ = false;
    DirectionalLight savedLight_{};
    bool saved_ = false;
    float shadowPassMs_ = 0.0f;
    std::chrono::steady_clock::time_point shadowT0_{};
};

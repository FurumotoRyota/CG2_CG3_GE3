#include "StartupScene.h"
#include "SakuraStorm.h"
#include "scene/SceneManager.h"
#include "base/WinApp.h"
#include "2d/SpriteCommon.h"
#include "input/Input.h"
#include "editor/EditorLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace
{
    constexpr float kScreenW = static_cast<float>(WinApp::kGameWidth);
    constexpr float kScreenH = static_cast<float>(WinApp::kGameHeight);
    constexpr float kDt = 1.0f / 60.0f;

    // --- 墨の画像（rei_ink.png）：1コマ 400x400 を 10列 x 9行 に並べてある（全90コマ） ---
    constexpr float kInkCell = 400.0f;
    constexpr int kInkColumns = 10;
    constexpr int kInkFrames = 90;
    constexpr float kInkFps = 30.0f;     // 1秒に何コマ進めるか（書く速さ。小さいほどゆっくり）

    // --- 画面の中の置き場所（1280x720 の画面での、左上の座標と大きさ） ---
    constexpr float kInkX = 380.0f, kInkY = -10.0f, kInkSize = 520.0f;
    constexpr float kNameX = 190.0f, kNameY = 490.0f;                      // 画像は 900x200

    // --- 時間割（秒） ---
    constexpr float kInkStart = 0.6f;                                      // 書き始め
    constexpr int kSettleFrames = 6;                                       // 墨のコマの最後の、書き終わったあとの落ち着き（render の SETTLE と同じ）
    constexpr float kPullStart = kInkStart + (kInkFrames - kSettleFrames) / kInkFps;   // カメラが引き始める（最後の画を書き終えたとき）
    constexpr float kPullTime = 0.9f;                                      // 引くのにかかる時間
    constexpr float kNameStart = kPullStart + kPullTime + 0.1f;            // 名前が浮かぶ（全体が見えてから）
    constexpr float kNameFade = 0.6f;
    constexpr float kStormStart = kNameStart + kNameFade + 0.7f;           // 桜吹雪が吹き始める
    constexpr float kStormTime = 2.4f;                                     // 吹き荒れて画面を覆い、暗転するまで（タイトルへ進むのは、暗転しきったとき）
    constexpr float kStormPeakRate = 240.0f;                               // 吹き込む枚数のピーク（1秒あたり）
    constexpr float kStormMaxSize = 3.2f;                                  // 画面を覆うころの、花びらの大きさの倍率
    constexpr float kGiantPeakRate = 130.0f;                               // 手前を横切る巨大な花びらのピーク（1秒あたり。これで画面を覆いつくす）
    constexpr float kHazeMax = 0.9f;                                       // 花びらのすきまを埋める桜色の霞の濃さ（0〜1）
    constexpr float kBlackStart = 0.8f;                                    // 暗転を始める時刻（kStormTime に対する割合。画面が桜色に覆いつくされてから）
    constexpr float kFadeInTime = 0.5f;

    // --- 画面の揺れ：強く筆を入れる所・止める所で、ドンと揺らす ---
    //   frame は墨のコマ番号（書くときに強い画の入りと止め）、amp は揺れの大きさ（ピクセル）
    struct Impact { int frame; float amp; };
    constexpr Impact kImpacts[] = {
        { 13, 8.0f },
        { 24, 5.0f },
        { 45, 6.0f },
        { 68, 8.0f },
        { 74, 6.0f },
        { 78, 5.0f },
        { 83, 6.0f }
    };
    // --- カメラ：書いている間は筆先に寄って追いかけ、書き終わったら引いて全体を見せる ---
    constexpr float kCamZoom = 1.55f;      // 書いている間の拡大率（1.0 が全体。大きいほど寄る）
    constexpr float kCamFollow = 5.0f;     // 筆先を追う速さ（大きいほど素早く追う）
    constexpr float kCamFollowPull = 14.0f; // 引くときは、ぴったり中心に収まるよう追う速さを上げる
    constexpr float kViewCenterX = kScreenW * 0.5f;
    constexpr float kViewCenterY = kScreenH * 0.5f;
    // 墨の各コマ（30コマ/秒）の筆先の位置。墨の画像の範囲を 0..1 にした値（x, y）。render の書き出しで作ったもの
    constexpr float kTips[kInkFrames][2] = {
        { 0.1139f, 0.2334f },
        { 0.1113f, 0.2465f },
        { 0.0997f, 0.3084f },
        { 0.0804f, 0.3737f },
        { 0.0578f, 0.4390f },
        { 0.1287f, 0.3616f },
        { 0.1996f, 0.2843f },
        { 0.2841f, 0.2118f },
        { 0.3253f, 0.2341f },
        { 0.3442f, 0.2925f },
        { 0.3355f, 0.3149f },
        { 0.2743f, 0.1943f },
        { 0.2131f, 0.0851f },
        { 0.2132f, 0.0963f },
        { 0.2132f, 0.1158f },
        { 0.2131f, 0.1938f },
        { 0.2126f, 0.2943f },
        { 0.2119f, 0.3959f },
        { 0.2116f, 0.4964f },
        { 0.2112f, 0.5980f },
        { 0.2111f, 0.6985f },
        { 0.2109f, 0.7786f },
        { 0.2106f, 0.8227f },
        { 0.2105f, 0.8565f },
        { 0.2107f, 0.8832f },
        { 0.2922f, 0.6834f },
        { 0.3738f, 0.4836f },
        { 0.4553f, 0.2838f },
        { 0.5323f, 0.0921f },
        { 0.5273f, 0.1010f },
        { 0.5178f, 0.1181f },
        { 0.4951f, 0.1550f },
        { 0.4697f, 0.1899f },
        { 0.4427f, 0.2237f },
        { 0.4153f, 0.2571f },
        { 0.3688f, 0.3148f },
        { 0.2584f, 0.4295f },
        { 0.4181f, 0.2762f },
        { 0.6010f, 0.1643f },
        { 0.6266f, 0.2040f },
        { 0.6612f, 0.2376f },
        { 0.7269f, 0.2868f },
        { 0.8075f, 0.3220f },
        { 0.8293f, 0.3272f },
        { 0.8506f, 0.3293f },
        { 0.8754f, 0.3314f },
        { 0.7940f, 0.3282f },
        { 0.7126f, 0.3249f },
        { 0.6473f, 0.3317f },
        { 0.6710f, 0.3556f },
        { 0.7098f, 0.3821f },
        { 0.5604f, 0.3888f },
        { 0.4221f, 0.3944f },
        { 0.4715f, 0.3937f },
        { 0.5569f, 0.3918f },
        { 0.6412f, 0.3869f },
        { 0.6946f, 0.3839f },
        { 0.7152f, 0.3831f },
        { 0.6168f, 0.4159f },
        { 0.5184f, 0.4487f },
        { 0.4201f, 0.4815f },
        { 0.3525f, 0.5139f },
        { 0.4038f, 0.5129f },
        { 0.4971f, 0.5108f },
        { 0.5914f, 0.5072f },
        { 0.6849f, 0.5062f },
        { 0.7405f, 0.5156f },
        { 0.7549f, 0.5510f },
        { 0.7536f, 0.5975f },
        { 0.7506f, 0.6650f },
        { 0.7467f, 0.7328f },
        { 0.7273f, 0.7969f },
        { 0.6844f, 0.8280f },
        { 0.6424f, 0.7906f },
        { 0.6090f, 0.7379f },
        { 0.5754f, 0.6702f },
        { 0.5418f, 0.6025f },
        { 0.5077f, 0.5431f },
        { 0.5075f, 0.5749f },
        { 0.5062f, 0.6551f },
        { 0.5061f, 0.7362f },
        { 0.5069f, 0.8174f },
        { 0.5077f, 0.8616f },
        { 0.5075f, 0.8852f },
        { 0.5075f, 0.8852f },
        { 0.5075f, 0.8852f },
        { 0.5075f, 0.8852f },
        { 0.5075f, 0.8852f },
        { 0.5075f, 0.8852f },
        { 0.5075f, 0.8852f }
    };

    constexpr float kShakeLife = 0.18f;    // 1回の揺れが続く時間（秒）

    void SetTint(Sprite& sprite, float r, float g, float b, float alpha)
    {
        sprite.GetMaterial()->color = { r, g, b, alpha };
    }

    std::unique_ptr<Sprite> MakeSprite(SpriteCommon* common, const char* path, float w, float h)
    {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(common, path, { w, h });
        return sprite;
    }
}

void StartupScene::Initialize()
{
    SpriteCommon* common = ctx_->spriteCommon;
    paper_ = MakeSprite(common, "Engine/resources/startup/rei_paper.png", kScreenW, kScreenH);
    ink_ = MakeSprite(common, "Engine/resources/startup/rei_ink.png", kInkCell, kInkCell);
    name_ = MakeSprite(common, "Engine/resources/startup/rei_name.png", 900.0f, 200.0f);
    fade_ = MakeSprite(common, "Engine/resources/startup/startup_white.png", 8.0f, 8.0f);
    fade_->GetTransform().Scale = { kScreenW / 8.0f, kScreenH / 8.0f, 1.0f };
    fade_->GetTransform().Translate = { 0.0f, 0.0f, 0.0f };
    wash_ = MakeSprite(common, "Engine/resources/startup/startup_white.png", 8.0f, 8.0f);
    wash_->GetTransform().Scale = { kScreenW / 8.0f, kScreenH / 8.0f, 1.0f };
    wash_->GetTransform().Translate = { 0.0f, 0.0f, 0.0f };
    storm_ = std::make_unique<SakuraStorm>();
    storm_->Initialize(common);
    time_ = 0.0f;
    loadTime_ = 0.0f;
    loadWait_ = 0;
    leaving_ = false;
    camInit_ = false;
}

void StartupScene::Update()
{
    // 演出を流しながら、ゲーム側の読み込みを1フレームに1段階ずつ進める（ゲームが preloadStep を設定したときだけ）
    if (!IsReady() && s_hooks.preloadStep) {
        s_hooks.preloadStep(ctx_);
    }
    loadTime_ += kDt;

    time_ += kDt;
    const float t = time_;

    // 画面の揺れ（衝撃のたびに、すぐ収まる揺れを足し合わせる）
    float shakeX = 0.0f;
    float shakeY = 0.0f;
    {
        auto addShake = [&](float impactTime, float amp, int seed) {
            const float u = (t - impactTime) / kShakeLife;
            if (u >= 0.0f && u < 1.0f) {
                const float d = (1.0f - u) * (1.0f - u) * amp;
                shakeX += std::sin(t * 131.0f + seed * 1.7f) * d;
                shakeY += std::cos(t * 113.0f + seed * 2.3f) * d * 0.8f;
            }
        };
        int seed = 0;
        for (const Impact& im : kImpacts) {
            addShake(kInkStart + im.frame / kInkFps, im.amp, seed++);
        }
    }

    const float tau = (t - kInkStart) * kInkFps;           // 墨のコマ数で数えた時間

    // --- カメラ ---
    {
        // いま筆先がある位置（コマとコマの間はなめらかにつなぐ）
        const float tf = std::clamp(tau, 0.0f, static_cast<float>(kInkFrames - 1));
        const int c0 = static_cast<int>(std::floor(tf));
        const int c1 = (std::min)(c0 + 1, kInkFrames - 1);
        const float cu = tf - static_cast<float>(c0);
        const float tipX = kInkX + (kTips[c0][0] + (kTips[c1][0] - kTips[c0][0]) * cu) * kInkSize;
        const float tipY = kInkY + (kTips[c0][1] + (kTips[c1][1] - kTips[c0][1]) * cu) * kInkSize;

        // 書き終わったら、全体が見える位置・大きさへ引く
        const float pull = std::clamp((t - kPullStart) / kPullTime, 0.0f, 1.0f);
        const float w = pull * pull * (3.0f - 2.0f * pull);
        const float targetZ = kCamZoom + (1.0f - kCamZoom) * w;
        const float targetX = tipX + (kViewCenterX - tipX) * w;
        const float targetY = tipY + (kViewCenterY - tipY) * w;

        if (!camInit_) {
            camX_ = targetX;
            camY_ = targetY;
            camInit_ = true;
        }
        else {
            const float follow = kCamFollow + (kCamFollowPull - kCamFollow) * w;
            const float k = 1.0f - std::exp(-follow * kDt);
            camX_ += (targetX - camX_) * k;
            camY_ += (targetY - camY_) * k;
        }
        camZ_ = targetZ;
        // 和紙の外が映らないように、カメラの中心を和紙の範囲の中に収める（和紙は画面より 4% 大きい）
        const float halfW = kScreenW * 0.5f / camZ_;
        const float halfH = kScreenH * 0.5f / camZ_;
        camX_ = std::clamp(camX_, -kScreenW * 0.02f + halfW, kScreenW * 1.02f - halfW);
        camY_ = std::clamp(camY_, -kScreenH * 0.02f + halfH, kScreenH * 1.02f - halfH);
    }
    // 画面の座標（拡大前）にあるものを、カメラ越しの位置・大きさにして貼る
    auto place = [&](Sprite& sprite, float baseX, float baseY, float baseScale) {
        sprite.GetTransform().Scale = { baseScale * camZ_, baseScale * camZ_, 1.0f };
        sprite.GetTransform().Translate = { (baseX - camX_) * camZ_ + kViewCenterX + shakeX, (baseY - camY_) * camZ_ + kViewCenterY + shakeY, 0.0f };
    };

    // 和紙（揺れても端が見えないよう、少し大きくして中心を合わせる）
    place(*paper_, -kScreenW * 0.02f, -kScreenH * 0.02f, 1.04f);
    SetTint(*paper_, 1.0f, 1.0f, 1.0f, 1.0f);
    paper_->Update();

    // 墨：時間に合わせてコマを進める（書き終わったら最後のコマのまま）
    const bool inkShown = tau >= 0.0f;
    const int frame = std::clamp(static_cast<int>(std::floor(tau)), 0, kInkFrames - 1);
    const float inkScale = kInkSize / kInkCell;
    ink_->SetTextureRect({ (frame % kInkColumns) * kInkCell, (frame / kInkColumns) * kInkCell }, { kInkCell, kInkCell });
    place(*ink_, kInkX, kInkY, inkScale);
    SetTint(*ink_, 1.0f, 1.0f, 1.0f, inkShown ? 1.0f : 0.0f);
    ink_->Update();

    // 名前：すっと浮かぶ
    const float nameA = std::clamp((t - kNameStart) / kNameFade, 0.0f, 1.0f);
    place(*name_, kNameX, kNameY + 8.0f * (1.0f - nameA), 1.0f);
    SetTint(*name_, 1.0f, 1.0f, 1.0f, nameA);
    name_->Update();

    // 桜吹雪：吹き込む枚数と花びらの大きさを増やして、画面を覆いつくす。覆ったところで暗転する
    const float u = std::clamp((t - kStormStart) / kStormTime, 0.0f, 1.0f);
    const float rampIn = std::clamp(u / 0.5f, 0.0f, 1.0f);
    const float ramp = rampIn * rampIn * (3.0f - 2.0f * rampIn);
    storm_->SetEmitRate(u > 0.0f ? kStormPeakRate * ramp : 0.0f);
    storm_->SetSizeScale(1.0f + (kStormMaxSize - 1.0f) * ramp);
    const float giantIn = std::clamp((u - 0.15f) / 0.35f, 0.0f, 1.0f);   // 巨大な花びらは早めから増やす
    storm_->SetGiantRate(kGiantPeakRate * giantIn * giantIn * (3.0f - 2.0f * giantIn));
    storm_->Update(kDt);
    // 花びらのすきまを埋める薄い桜色の霞（暗転の前に、画面が桜色に覆われるように）
    const float hazeT = std::clamp((u - 0.25f) / 0.4f, 0.0f, 1.0f);      // 暗転の前に、桜色の霞で覆いきる
    SetTint(*wash_, 1.0f, 0.86f, 0.9f, kHazeMax * hazeT * hazeT * (3.0f - 2.0f * hazeT));
    wash_->Update();

    // 最初は黒からのフェードイン。桜吹雪が覆いつくしたあとは、暗転せずに桜色で画面を満たしてタイトルへ続ける
    const float fadeIn = 1.0f - std::clamp(t / kFadeInTime, 0.0f, 1.0f);
    const float pinkT = std::clamp((u - kBlackStart) / (1.0f - kBlackStart), 0.0f, 1.0f);
    const float pinkA = pinkT * pinkT * (3.0f - 2.0f * pinkT);
    if (fadeIn > 0.0f) {
        SetTint(*fade_, 0.0f, 0.0f, 0.0f, fadeIn);
    } else {
        SetTint(*fade_, 1.0f, 0.86f, 0.9f, pinkA);
    }
    fade_->Update();

    // Enter / Space / Esc / クリック / ゲームパッドAでスキップ、または最後まで見たら次のシーンへ
    bool click = false;
#ifdef USE_IMGUI
    click = EditorLayout::GetViewportInput().clicked;
#endif // USE_IMGUI
    Input& input = *ctx_->input;
    const bool skip = input.TriggerKey(DIK_RETURN) || input.TriggerKey(DIK_SPACE) || input.TriggerKey(DIK_ESCAPE)
        || input.TriggerGamepadButton(0) || click;
    // 読み込みが終わるまでは次へ進まない（演出が終わっても、NOW LOADING を出して待つ）
    if (!leaving_ && IsReady() && (skip || t >= kStormStart + kStormTime)) {
        leaving_ = true;
        if (s_hooks.onFinish) {
            s_hooks.onFinish(ctx_); // 次のシーンへの切り替えはゲーム側が決める
        }
    }
}

void StartupScene::Draw()
{
    ctx_->spriteCommon->PreDraw();
    paper_->Draw();
    ink_->Draw();
    name_->Draw();
    wash_->Draw();
    storm_->Draw();
    fade_->Draw();
}

void StartupScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("Scene: Startup (Rei Engine)");
    ImGui::Separator();
    ImGui::Text("Enter / Space / Esc / Click / Pad A: skip");
    ImGui::End();

    // NOW LOADING：読み込みが終わるまで、右下に出す
    if (s_hooks.drawLoading) {
        s_hooks.drawLoading(kScreenW, kScreenH);
    }
#endif // USE_IMGUI
}

void StartupScene::Finalize()
{
    paper_.reset();
    ink_.reset();
    name_.reset();
    wash_.reset();
    if (storm_) {
        storm_->Finalize();
        storm_.reset();
    }
    fade_.reset();
}

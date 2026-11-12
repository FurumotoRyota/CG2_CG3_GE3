#pragma once
// 画面のフェード（暗転・明転）。シーンの切り替えの前後に使う（ヘッダーだけで使える）
//
//   ScreenFade fade_;
//   fade_.StartFadeIn(0.6f);                 // シーンの最初：黒から明るくなる
//
//   // 毎フレームの Update で
//   fade_.Update(dt);
//   if (goNext && !fade_.IsBusy()) {
//       fade_.StartFadeOut(0.5f);            // 画面を暗くしていく
//   }
//   if (fade_.IsFadedOut()) {                // 真っ暗になったらシーンを切り替える
//       sceneManager_.ChangeScene<NextScene>();
//   }
//
//   // Draw の最後に（他のUIより手前に出したいので最後に呼ぶ）
//   fade_.Draw(ui_);
#include <algorithm>

#include "UiCommon.h"
#include "UiRenderer.h"
#include "base/WinApp.h"

class ScreenFade
{
public:
    // 色を決める（0〜255）。初期値は黒
    void SetColor(int r, int g, int b)
    {
        r_ = r;
        g_ = g;
        b_ = b;
    }

    // 画面をだんだん色で覆う（透明 → 不透明）
    void StartFadeOut(float seconds)
    {
        Start(alpha_, 1.0f, seconds);
    }

    // 覆っていた色がだんだん消える（不透明 → 透明）
    void StartFadeIn(float seconds)
    {
        alpha_ = 1.0f; // 最初から覆われた状態から始める
        Start(1.0f, 0.0f, seconds);
    }

    // すぐに切り替える（alpha: 0=透明 / 1=完全に覆う）
    void Set(float alpha)
    {
        alpha_ = (std::min)((std::max)(alpha, 0.0f), 1.0f);
        busy_ = false;
    }

    // 毎フレーム1回
    void Update(float dt)
    {
        if (!busy_) {
            return;
        }
        time_ += dt;
        const float t = (std::min)(time_ / duration_, 1.0f);
        alpha_ = from_ + (to_ - from_) * t;
        if (t >= 1.0f) {
            alpha_ = to_;
            busy_ = false;
        }
    }

    bool IsBusy() const { return busy_; }                         // フェード中か
    bool IsFadedOut() const { return !busy_ && alpha_ >= 1.0f; }  // 完全に覆われているか
    bool IsClear() const { return !busy_ && alpha_ <= 0.0f; }     // 完全に透明か
    float Alpha() const { return alpha_; }

    void Draw(UiRenderer& ui) const
    {
        if (alpha_ > 0.0f) {
            const float w = static_cast<float>(WinApp::kGameWidth);
            const float h = static_cast<float>(WinApp::kGameHeight);
            ui.DrawRect({ w * 0.5f, h * 0.5f }, { w, h }, Ui::WithAlpha(Ui::Rgba(r_, g_, b_), alpha_));
        }
    }

private:
    void Start(float from, float to, float seconds)
    {
        from_ = from;
        to_ = to;
        duration_ = (std::max)(0.001f, seconds);
        time_ = 0.0f;
        busy_ = true;
    }

    int r_ = 0;
    int g_ = 0;
    int b_ = 0;
    float alpha_ = 0.0f;
    float from_ = 0.0f;
    float to_ = 0.0f;
    float duration_ = 1.0f;
    float time_ = 0.0f;
    bool busy_ = false;
};

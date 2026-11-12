#pragma once
// ゲージ（体力バーなど）。ダメージを受けると、赤いバーが遅れて追いかけてくる（ヘッダーだけで使える）
//
//   // 初期化
//   UiGauge hp_;
//   hp_.SetLayout({ 200.0f, 40.0f }, { 320.0f, 24.0f });
//   hp_.SetValue(100.0f, 100.0f, true);        // true: 演出なしで、すぐこの値にする
//
//   // 値が変わったとき / 毎フレーム
//   hp_.SetValue(player.hp, player.maxHp);     // 現在値, 最大値
//   hp_.Update(dt);                            // 毎フレーム1回
//
//   // Draw で（UiRenderer に描いてもらう）
//   hp_.Draw(ui_, "HP");                      // ラベルは省略できる
#include <algorithm>

#include "UiCommon.h"
#include "UiRenderer.h"

class UiGauge
{
public:
    void SetLayout(const Vector2& center, const Vector2& size)
    {
        center_ = center;
        size_ = size;
    }

    // fill: 本体の色 / trail: ダメージで遅れて減る部分の色 / back: 下地の色
    void SetColors(unsigned int fill, unsigned int trail, unsigned int back)
    {
        fill_ = fill;
        trail_ = trail;
        back_ = back;
    }

    // 現在値と最大値を渡す。instant が true なら、遅れて減る演出なしで、すぐこの値にする
    void SetValue(float current, float maximum, bool instant = false)
    {
        const float r = maximum > 0.0f ? (std::min)((std::max)(current / maximum, 0.0f), 1.0f) : 0.0f;
        if (instant || r >= trailRatio_) {
            trailRatio_ = r;     // 増えたときは、遅れる部分も一緒に増やす
            holdTime_ = 0.0f;
        }
        else if (r < ratio_) {
            holdTime_ = holdSeconds; // 減ったとき：少し待ってから、遅れる部分が減り始める
        }
        ratio_ = r;
    }

    // 毎フレーム1回
    void Update(float dt)
    {
        if (holdTime_ > 0.0f) {
            holdTime_ -= dt;
            return;
        }
        if (trailRatio_ > ratio_) {
            trailRatio_ = (std::max)(ratio_, trailRatio_ - shrinkSpeed * dt);
        }
    }

    float Ratio() const { return ratio_; }           // 本体の割合（0〜1）
    float TrailRatio() const { return trailRatio_; } // 遅れて減る部分の割合（0〜1）

    void Draw(UiRenderer& ui, const char* label = nullptr) const
    {
        // 下地（黒い枠つき）→ 遅れて減る部分 → 本体 の順に重ねる
        ui.DrawRect(center_, { size_.x + 4.0f, size_.y + 4.0f }, Ui::Rgba(0, 0, 0, 200));
        ui.DrawRect(center_, size_, back_);
        DrawFill(ui, trailRatio_, trail_);
        DrawFill(ui, ratio_, fill_);
        if (label != nullptr) {
            ui.DrawLabel(center_, label, size_.y * 0.8f, Ui::Rgba(255, 255, 255));
        }
    }

    // 調整できる値
    float holdSeconds = 0.45f;  // ダメージを受けてから、遅れる部分が減り始めるまでの時間
    float shrinkSpeed = 0.8f;   // 遅れる部分が減る速さ（1秒あたり、ゲージ全体の割合）

private:
    // 左端から ratio の長さだけ塗る
    void DrawFill(UiRenderer& ui, float ratio, unsigned int color) const
    {
        if (ratio <= 0.0f) {
            return;
        }
        const float w = size_.x * ratio;
        ui.DrawRect({ center_.x - size_.x * 0.5f + w * 0.5f, center_.y }, { w, size_.y }, color);
    }

    Vector2 center_{ 0.0f, 0.0f };
    Vector2 size_{ 300.0f, 24.0f };
    unsigned int fill_ = Ui::Rgba(90, 200, 90);
    unsigned int trail_ = Ui::Rgba(220, 60, 50);
    unsigned int back_ = Ui::Rgba(30, 30, 30, 230);
    float ratio_ = 1.0f;
    float trailRatio_ = 1.0f;
    float holdTime_ = 0.0f;
};

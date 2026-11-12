#pragma once
// 時間を測る道具（ヘッダーだけで使える）。毎フレーム Update(dt) を呼ぶ。dt は1フレームの秒数
//
//   Timer    : 「○秒たったら終わり」を数える（演出・出現間隔など）
//     Timer t;  t.Start(2.0f);            // 2秒のタイマーを開始
//     t.Update(dt);
//     if (t.Finished()) { ... }           // 終わっている間ずっと true
//     float p = t.Progress();             // 0〜1（Easing に渡すと動きに緩急がつく）
//
//   Cooldown : 「使ったあと○秒は使えない」（射撃の間隔・スキルの再使用）
//     Cooldown shot(0.3f);
//     shot.Update(dt);
//     if (shot.TryUse()) { /* 撃つ */ }   // 使えたら true になって、また待ち時間が始まる
#include <algorithm>
#include <cmath>

class Timer
{
public:
    // duration 秒のタイマーを始める。loop が true なら、終わったら自動でまた最初から
    void Start(float duration, bool loop = false)
    {
        duration_ = (std::max)(0.0001f, duration);
        elapsed_ = 0.0f;
        loop_ = loop;
        running_ = true;
        looped_ = false;
    }

    void Stop() { running_ = false; }
    void Reset() { elapsed_ = 0.0f; looped_ = false; }

    void Update(float dt)
    {
        looped_ = false;
        if (!running_) {
            return;
        }
        elapsed_ += dt;
        if (elapsed_ >= duration_) {
            if (loop_) {
                elapsed_ = std::fmod(elapsed_, duration_);
                looped_ = true; // このフレームに1周した
            }
            else {
                elapsed_ = duration_;
                running_ = false;
            }
        }
    }

    bool IsRunning() const { return running_; }
    bool Finished() const { return !running_ && elapsed_ >= duration_; }  // ループなしで終わった後は true のまま
    bool JustLooped() const { return looped_; }                           // ループありで、このフレームに1周した
    float Elapsed() const { return elapsed_; }
    float Remaining() const { return duration_ - elapsed_; }
    float Duration() const { return duration_; }
    float Progress() const { return duration_ > 0.0f ? elapsed_ / duration_ : 1.0f; } // 0〜1

private:
    float duration_ = 1.0f;
    float elapsed_ = 0.0f;
    bool running_ = false;
    bool loop_ = false;
    bool looped_ = false;
};

class Cooldown
{
public:
    Cooldown() = default;
    explicit Cooldown(float interval) : interval_(interval) {}

    void SetInterval(float interval) { interval_ = interval; }

    void Update(float dt) { remaining_ = (std::max)(0.0f, remaining_ - dt); }

    bool Ready() const { return remaining_ <= 0.0f; }

    // 使えるなら true を返して、待ち時間を始める
    bool TryUse()
    {
        if (!Ready()) {
            return false;
        }
        remaining_ = interval_;
        return true;
    }

    void Reset() { remaining_ = 0.0f; }                      // すぐ使える状態に戻す
    float Remaining() const { return remaining_; }
    float Ratio() const { return interval_ > 0.0f ? remaining_ / interval_ : 0.0f; } // 1→0（ゲージ表示用）

private:
    float interval_ = 1.0f;
    float remaining_ = 0.0f;
};

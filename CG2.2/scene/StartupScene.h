#pragma once
#include <memory>
#include <random>
#include <vector>

#include "scene/Scene.h"
#include "2d/Sprite.h"

/// <summary>
/// 起動直後のスタートアップ演出「Divergence Engine」
/// 前半：オレンジに光る7セグの数字が高速で切り替わり、左から1本ずつ止まって数字が決まる。やがて数字がゆらいで画面が乱れる（世界線が動くイメージ）
/// 後半：一瞬の暗転のあと、「DIVERGENCE ENGINE」が、文字ごとにでたらめな文字から解読されるように決まっていく。終わったらタイトルへ進む
/// 表示する数字は kReadoutText で変える
/// </summary>
class StartupScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
    void Finalize() override;

private:
    // 前半に出す数字（8文字。「.」も使える）
    static constexpr const char* kReadoutText = "2.007326";
    static constexpr int kReadoutLength = 8;
    // 後半に出す名前（大文字・数字・「.」・空白が使える）
    static constexpr const char* kTitleText = "DIVERGENCE ENGINE";
    static constexpr int kTitleLength = 17;
    // 名前の下に出す小さな文字（大文字・数字・「.」・空白が使える）
    static constexpr const char* kSubText = "ORE GA TSUKURIMASITA";
    static constexpr int kSubLength = 20;

    // 文字の画像（アトラス）の何番目か。見つからなければ -1
    static int GlyphIndex(char c);
    // 文字を1つ表示する（番号、画面上の中心、拡大率、明るさ、アルファ）
    void ShowGlyph(Sprite& sprite, int glyph, float cx, float cy, float scale, float brightness, float alpha, float r = 1.0f, float g = 1.0f, float b = 1.0f);

    std::unique_ptr<Sprite> background_;
    std::unique_ptr<Sprite> scan_;                // 走査線のかぶせ
    std::unique_ptr<Sprite> readout_[kReadoutLength];
    std::unique_ptr<Sprite> title_[kTitleLength];
    std::unique_ptr<Sprite> sub_[kSubLength];     // 名前の下の小さな文字
    std::unique_ptr<Sprite> underline_;           // 名前の下の線
    std::vector<std::unique_ptr<Sprite>> bars_;   // 乱れのときの横線
    std::unique_ptr<Sprite> flash_;               // 切り替わりの白いフラッシュ

    // 画面ノイズ・走査まわり
    std::unique_ptr<Sprite> noise_;               // 砂嵐（毎フレーム違う場所を切り出す）
    std::unique_ptr<Sprite> band_;                // ゆっくり下へ流れる明るい帯
    std::unique_ptr<Sprite> bandLine_;            // 帯の先頭の細い線
    std::unique_ptr<Sprite> intro_;               // 起動直後に横へ開く光の線
    std::unique_ptr<Sprite> ghostR_[kTitleLength]; // 乱れのときの赤いずれ
    std::unique_ptr<Sprite> ghostB_[kTitleLength]; // 乱れのときの青いずれ
    bool ghostOn_[kTitleLength] = {};
    float noiseAlpha_ = 0.0f;

    // 電気のビリビリ（青白い稲妻）。短い時間だけ出て、細かくちらつく
    static constexpr int kBoltCount = 14;
    struct Bolt {
        std::unique_ptr<Sprite> sprite;
        int life = 0;       // 残りフレーム（0なら非表示）
        int maxLife = 1;
        float x = 0, y = 0, angle = 0, length = 0, thick = 1;
        int variant = 0;
    };
    Bolt bolts_[kBoltCount];
    // 稲妻を1本出す（始点、角度[rad]、長さ[px]、太さ倍率、表示フレーム数）
    void SpawnBolt(float x, float y, float angle, float length, float thick, int life);
    void UpdateBolts();

    std::mt19937 rng_{ 20241026 };
    int frameCount_ = 0;
    float time_ = 0.0f;                           // 経過時間（秒）
    int randomGlyph_[kTitleLength] = {};          // 切り替え中に出している文字
    int shownReadout_[kReadoutLength] = {};       // 今出している文字（-1は消灯）
    int shownTitle_[kTitleLength] = {};           // 同上
    int shownSub_[kSubLength] = {};               // 同上
    int randomSub_[kSubLength] = {};
    float screenAlpha_ = 0.0f;                    // 全体の明るさ（フェード用）

    // 前半（数字）の時刻（秒）
    static constexpr float kReadoutWarm = 0.5f;    // 光が点いていく
    static constexpr float kRandomStart = 0.7f;    // 数字が高速で切り替わり始める
    static constexpr float kLockStart = 1.5f;      // 左から1本ずつ数字が決まり始める
    static constexpr float kLockGap = 0.15f;       // 次の文字が決まるまでの間隔
    static constexpr float kFluxStart = 3.2f;      // 数字がゆらいで乱れ始める
    static constexpr float kReadoutEnd = 4.0f;     // ここで一瞬暗転する

    // 後半（名前）の時刻（秒）
    static constexpr float kTitleStart = 4.4f;     // 名前の解読が始まる
    static constexpr float kTitleGap = 0.07f;      // 次の文字の解読が始まるまでの間隔
    static constexpr float kScrambleTime = 0.5f;   // 1文字が決まるまで（その間はでたらめな文字）
    static constexpr float kLineStart = 6.1f;      // 線が伸び始める
    static constexpr float kLineTime = 0.7f;
    static constexpr float kSubStart = 6.4f;       // 下の小さな文字の解読が始まる
    static constexpr float kSubGap = 0.04f;
    static constexpr float kSubScramble = 0.35f;
    static constexpr float kFadeOutStart = 8.4f;   // 全体が暗くなり始める
    static constexpr float kFadeOut = 0.7f;
};

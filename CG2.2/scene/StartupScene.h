#pragma once
#include <memory>

#include "scene/Scene.h"
#include "2d/Sprite.h"
#include "SakuraStorm.h"

/// <summary>
/// 起動直後のスタートアップ演出「怜エンジン」
/// 和紙の上に、墨で「怜」の字が書き順どおりに書かれていく。書き終わると「エンジン」の文字が浮かぶ。
/// 墨の動きは resources/rei_ink.png（90コマを 10列 x 9行 に並べた画像）を順番に見せている。
/// 書いている間はカメラが筆先に寄って追いかけ、書き終わったら全体が見えるところまで引く。
/// 終わりは桜吹雪が吹き荒れて画面を覆いつくし、暗転する。
/// 終わったらタイトルへ進む。Enter / Space / Esc / クリック / ゲームパッドAでスキップ
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
    std::unique_ptr<Sprite> paper_;   // 和紙の背景
    std::unique_ptr<Sprite> ink_;     // 墨で書かれていく「怜」
    std::unique_ptr<Sprite> name_;    // 「エンジン」
    std::unique_ptr<Sprite> wash_;    // 桜吹雪のすきまを埋める薄い桜色の板
    std::unique_ptr<Sprite> fade_;    // 最初と、桜吹雪が覆いつくしたあとの黒
    std::unique_ptr<SakuraStorm> storm_;  // 終わりの桜吹雪

    float time_ = 0.0f;
    bool leaving_ = false;

    // カメラ（画面の座標でのどこを中心に、どれだけ拡大して見ているか）
    float camX_ = 0.0f;
    float camY_ = 0.0f;
    float camZ_ = 1.0f;
    bool camInit_ = false;
};

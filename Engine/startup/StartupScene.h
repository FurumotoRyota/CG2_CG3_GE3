#pragma once
#include <functional>
#include <memory>

#include "scene/Scene.h"
#include "2d/Sprite.h"
#include "SakuraStorm.h"

struct SceneContext;

/// <summary>
/// 起動直後のスタートアップ演出「怜エンジン」
/// 和紙の上に、墨で「怜」の字が書き順どおりに書かれていく。書き終わると「エンジン」の文字が浮かぶ。
/// 墨の動きは Engine/resources/startup/rei_ink.png（90コマを 10列 x 9行 に並べた画像）を順番に見せている。
/// 書いている間はカメラが筆先に寄って追いかけ、書き終わったら全体が見えるところまで引く。
/// 終わりは桜吹雪が吹き荒れて画面を覆いつくし、暗転する。
/// 終わったら onFinish（ゲーム側で設定する次のシーンへの切り替え）を呼ぶ。Enter / Space / Esc / クリック / ゲームパッドAでスキップ
/// </summary>
class StartupScene : public Scene
{
public:
    /// <summary>
    /// ゲーム側が設定する「演出の前後でやること」（どれも未設定でよい）
    /// 例: Game::Initialize で StartupScene::s_hooks.onFinish を設定して、終わったらタイトルへ進める
    /// </summary>
    struct Hooks
    {
        std::function<bool()> isReady;                  // 読み込みが終わったか（未設定なら、常に終わっている扱い）
        std::function<void(SceneContext*)> preloadStep; // 演出中に1フレームに1回呼ばれる（重い読み込みを少しずつ進める用）
        std::function<void(float, float)> drawLoading;  // 読み込み中の表示（引数は画面の幅と高さ）。ImGuiの描画中に呼ばれる
        std::function<void(SceneContext*)> onFinish;    // 演出が終わった（またはスキップされた）ときに呼ばれる
    };
    inline static Hooks s_hooks;

    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
    void Finalize() override;

private:
    bool IsReady() const { return !s_hooks.isReady || s_hooks.isReady(); }

    std::unique_ptr<Sprite> paper_;   // 和紙の背景
    std::unique_ptr<Sprite> ink_;     // 墨で書かれていく「怜」
    std::unique_ptr<Sprite> name_;    // 「エンジン」
    std::unique_ptr<Sprite> wash_;    // 桜吹雪のすきまを埋める薄い桜色の板
    std::unique_ptr<Sprite> fade_;    // 最初と、桜吹雪が覆いつくしたあとの黒
    std::unique_ptr<SakuraStorm> storm_;  // 終わりの桜吹雪

    float time_ = 0.0f;
    float loadTime_ = 0.0f;   // ロード表示用の時計（ドットの動き・フェード）
    int loadWait_ = 0;        // 読み込みを始める前に、最初の絵を何フレームか見せるための数え
    bool leaving_ = false;

    // カメラ（画面の座標でのどこを中心に、どれだけ拡大して見ているか）
    float camX_ = 0.0f;
    float camY_ = 0.0f;
    float camZ_ = 1.0f;
    bool camInit_ = false;
};

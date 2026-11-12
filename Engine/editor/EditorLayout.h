#pragma once

// エディタ風のパネル配置（ImGui使用時のみ有効）
//
//  +--------------------------------------------------------+
//  | メニューバー (File / Scene / FPS)                        |
//  +-----------+---------------------------+----------------+
//  | Hierarchy |         Viewport          |   Inspector    |
//  |  (左)     |   (ゲーム画面をここに表示)    |     (右)       |
//  +-----------+---------------------------+----------------+
//  | System (下：サウンド・ゲームパッド等)                      |
//  +--------------------------------------------------------+
#ifdef USE_IMGUI
#include <d3d12.h>

#include "../math/Vector2.h"

namespace EditorLayout
{
    enum class Panel { Hierarchy, Inspector, System };

    // 決められた位置・サイズ(固定)でウィンドウを開始する。呼び出し側で必ず ImGui::End() を呼ぶこと
    bool Begin(const char* title, Panel panel);

    // ゲーム画面(オフスクリーンのテクスチャ)を中央のViewportパネルに、アスペクト比を保って表示する
    void DrawViewport(D3D12_GPU_DESCRIPTOR_HANDLE gameTexture);

    // Viewport上のマウス状態（DrawViewport が毎フレーム更新する）
    struct ViewportInput
    {
        bool valid = false;   // Viewportが描画されているか
        bool clicked = false; // このフレームにViewport上で左クリックされた
        bool down = false;    // 左ボタンを押している（ドラッグ中はViewport外でもtrue）
        bool shift = false;   // Shiftキーを押している
        bool rightClicked = false; // このフレームにViewport上で右クリックされた（カメラ操作の開始）
        bool middleClicked = false; // wheel button pressed on the viewport this frame
        bool middleDown = false;    // wheel button held
        bool rightDown = false;    // 右ボタンを押している（ホールド中はViewport外でもtrue）
        Vector2 mouseDelta{ 0.0f, 0.0f }; // このフレームのマウス移動量（画面のピクセル）
        float wheel = 0.0f;   // Viewport上でのマウスホイール量（奥へ回すと正）。タッチパッドの2本指スライド・ピンチも含む
        Vector2 mouse{ 0.0f, 0.0f }; // マウス位置（ゲーム画面のピクセル座標。Viewport外だと範囲外の値）
    };
    const ViewportInput& GetViewportInput();

    // Viewportの上に線を重ねて描く（選択枠など）。pointsはゲーム画面のピクセル座標
    void DrawViewportOverlay(const Vector2* points, int count, bool closed);

    // 拡大縮小用のハンドル（小さな四角）を描く。pointsはゲーム画面のピクセル座標
    void DrawViewportHandles(const Vector2* points, int count);

    // Draw a position marker (crosshair + label) at a game-screen pixel position.
    // If the position is outside the viewport, an arrow is drawn on the nearest edge instead.
    void DrawViewportMarker(const Vector2& position, const char* label, unsigned int color, bool selected);

    // Viewportの上に、画面全体を暗くする板を重ねる（alpha 0..1。黒）
    void DrawViewportDim(float alpha);
    // 指定した色（0..255）でビューポート全体を覆う。alpha は 0..1
    void DrawViewportTint(int r, int g, int b, float alpha);
    // ゲーム画面の座標で、体力ゲージのような横棒を描く（center は中心、frac は 0..1 の満たし具合）
    void DrawViewportBar(const Vector2& center, const Vector2& size, float frac, unsigned int color);

    // 枠なしの塗りつぶし四角を描く。center は中心、size は大きさ（ゲーム画面のピクセル）、color は Ui::Rgba などで作った色
    // （DrawViewportBar は呼ぶたびに黒い枠を描くので、重ねて使うときはこちらを使う）
    void DrawViewportRect(const Vector2& center, const Vector2& size, unsigned int color);

    // Viewportの上に文字を描く。center はゲーム画面のピクセル座標（文字の中心）、sizePx は文字の高さ（ゲーム画面のピクセル）、color は IM_COL32 形式
    void DrawViewportText(const Vector2& center, const char* text, float sizePx, unsigned int color);
}
#endif // USE_IMGUI

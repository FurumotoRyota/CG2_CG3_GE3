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
}
#endif // USE_IMGUI

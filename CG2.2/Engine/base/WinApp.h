#pragma once
#include <Windows.h>
#include <cstdint>

/// <summary>
/// ウィンドウ生成・メッセージ処理・COM初期化を担当する
/// </summary>
class WinApp
{
public:
    // ゲーム画面の解像度（カメラのアスペクト比・スプライトの座標系・オフスクリーン描画のサイズ）
    static constexpr int32_t kGameWidth = 1280;
    static constexpr int32_t kGameHeight = 720;

    // ウィンドウ（スワップチェーン）のサイズ
    // ImGui使用時はエディタ画面として広く取り、ゲーム画面はその中のViewportパネルに表示する
#ifdef USE_IMGUI
    static constexpr int32_t kClientWidth = 1600;
    static constexpr int32_t kClientHeight = 900;
#else
    static constexpr int32_t kClientWidth = kGameWidth;
    static constexpr int32_t kClientHeight = kGameHeight;
#endif

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void Initialize(const wchar_t* title = L"CG2");
    void Finalize();

    // 溜まっているメッセージを処理する。WM_QUITを受け取ったら true を返す
    bool ProcessMessage();

    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return wc_.hInstance; }

private:
    HWND hwnd_ = nullptr;
    WNDCLASSW wc_{};
};

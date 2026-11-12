#pragma once
// ポーズ画面（エンジンの部品）
//   Esc / P / ゲームパッドの Start で開閉する。開いている間は、画面を暗くして項目を並べる
//   項目は 上下キー / W S / 十字キー で選び、Enter / Space / ゲームパッドA / マウスクリックで決める
//   使い方（シーンの Update で毎フレーム）:
//     const int r = pause.Update(input, canToggle);
//     if (pause.IsOpen()) { ...r に応じた処理...; return; }   // 開いている間はゲームを進めない
//   描く（シーンの DrawImGui で）: pause.Draw();
#include <string>
#include <vector>

#include "math/Vector2.h"

class Input;

class PauseMenu
{
public:
    static constexpr int kNone = -1;   // Update の戻り値：何も起きていない
    static constexpr int kClosed = -2; // Update の戻り値：開閉キーで閉じた（再開と同じ扱いにする）

    void SetTitle(const std::string& title) { title_ = title; }
    void SetItems(const std::vector<std::string>& items) { items_ = items; selected_ = 0; }

    void Open();
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }

    // canToggle が false の間は、開閉キーを無視する（演出中など）。戻り値は kNone / kClosed / 選ばれた項目の番号
    int Update(const Input& input, bool canToggle);

    // 暗くして、タイトルと項目を描く（開いていなければ何もしない）。ImGui の描画中に呼ぶ
    void Draw() const;

private:
    std::string title_ = "PAUSED";
    std::vector<std::string> items_;
    int selected_ = 0;
    bool open_ = false;
    Vector2 lastMouse_{ -1.0f, -1.0f }; // 前のフレームのマウス位置
    int wait_ = 0; // 開いた直後は、そのフレームのクリックを拾わない

    static bool TogglePressed(const Input& input);
};

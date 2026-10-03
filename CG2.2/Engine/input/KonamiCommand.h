#pragma once
#include <cstddef>

class Input;

/// <summary>
/// コナミコマンド（↑↑↓↓←→←→BA）の入力判定
/// キーボードの矢印/B/Aキーと、ゲームパッドの十字キー/ボタン(B=1, A=0想定)の両方に対応する
/// </summary>
class KonamiCommand
{
public:
    // 毎フレーム1回呼ぶ。コマンドが成立したフレームだけ true を返す
    bool Update(const Input& input);

private:
    enum class Button { Up, Down, Left, Right, ButtonB, ButtonA };

    static bool IsTriggered(Button button, const Input& input);

    size_t progress_ = 0; // 何個目まで正しく入力できたか
};

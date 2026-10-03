#include "KonamiCommand.h"
#include "Input.h"

bool KonamiCommand::IsTriggered(Button button, const Input& input)
{
    switch (button) {
    case Button::Up:      return input.TriggerKey(DIK_UP) || input.TriggerPov(0);
    case Button::Down:    return input.TriggerKey(DIK_DOWN) || input.TriggerPov(18000);
    case Button::Left:    return input.TriggerKey(DIK_LEFT) || input.TriggerPov(27000);
    case Button::Right:   return input.TriggerKey(DIK_RIGHT) || input.TriggerPov(9000);
    case Button::ButtonB: return input.TriggerKey(DIK_B) || input.TriggerGamepadButton(1); // 1=Bボタン想定
    case Button::ButtonA: return input.TriggerKey(DIK_A) || input.TriggerGamepadButton(0); // 0=Aボタン想定
    }
    return false;
}

bool KonamiCommand::Update(const Input& input)
{
    static constexpr Button kSequence[] = {
        Button::Up, Button::Up, Button::Down, Button::Down,
        Button::Left, Button::Right, Button::Left, Button::Right,
        Button::ButtonB, Button::ButtonA
    };
    static constexpr Button kAllButtons[] = {
        Button::Up, Button::Down, Button::Left, Button::Right,
        Button::ButtonB, Button::ButtonA
    };
    constexpr size_t kSequenceLength = sizeof(kSequence) / sizeof(kSequence[0]);

    for (Button button : kAllButtons) {
        if (!IsTriggered(button, input)) {
            continue; // このフレームで発生した入力ではない
        }

        // このフレームで発生した入力(通常1つ)が見つかった
        if (button == kSequence[progress_]) {
            ++progress_;
            if (progress_ >= kSequenceLength) {
                progress_ = 0;
                return true; // コナミコマンド成立
            }
        }
        else {
            // 間違った入力。それがコマンドの先頭(UP)ならそこから再スタート、それ以外は最初からやり直し
            progress_ = (button == kSequence[0]) ? 1 : 0;
        }
        break; // 1フレームにつき1入力ぶん処理すれば十分
    }

    return false;
}

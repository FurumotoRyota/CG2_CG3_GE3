#include "GameInput.h"

#include <algorithm>
#include <cmath>

#include "Input.h"

namespace
{
    // スティックの値（-32768～32767）を -1～1 にして、遊び（デッドゾーン）を引く。向きはそのまま、大きさだけ 0～1 に伸ばす
    Vector2 ReadStick(LONG rawX, LONG rawY, float deadZone)
    {
        float x = static_cast<float>(rawX) / 32768.0f;
        float y = static_cast<float>(rawY) / 32768.0f;
        const float length = std::sqrt(x * x + y * y);
        if (length < deadZone) {
            return { 0.0f, 0.0f };
        }
        const float scaled = (std::min)((length - deadZone) / (1.0f - deadZone), 1.0f);
        x = x / length * scaled;
        y = y / length * scaled;
        return { x, y };
    }

    // 十字キー（POV）の向き。入っていなければ (0,0)。上が +y
    Vector2 ReadPov(DWORD pov)
    {
        if (pov == 0xFFFFFFFF) {
            return { 0.0f, 0.0f };
        }
        const float rad = static_cast<float>(pov) / 100.0f * 3.14159265f / 180.0f; // 0度が上、時計回り
        const float x = std::sin(rad);
        const float y = std::cos(rad);
        return { std::fabs(x) > 0.5f ? (x > 0 ? 1.0f : -1.0f) : 0.0f, std::fabs(y) > 0.5f ? (y > 0 ? 1.0f : -1.0f) : 0.0f };
    }
}

void GameInput::SetMouse(const Vector2& gamePixel, bool inside, bool leftDown)
{
    mouse_ = gamePixel;
    mouseInside_ = inside;
    mouseLeft_ = leftDown && inside; // 画面の外（エディタのパネルなど）のクリックは数えない
}

void GameInput::Update(const Input& input)
{
    //--------------------------------------------------
    // キーボード
    //--------------------------------------------------
    Vector2 keyMove{ 0.0f, 0.0f };
    if (input.PushKey(DIK_A) || input.PushKey(DIK_LEFT)) { keyMove.x -= 1.0f; }
    if (input.PushKey(DIK_D) || input.PushKey(DIK_RIGHT)) { keyMove.x += 1.0f; }
    if (input.PushKey(DIK_W) || input.PushKey(DIK_UP)) { keyMove.y += 1.0f; }
    if (input.PushKey(DIK_S) || input.PushKey(DIK_DOWN)) { keyMove.y -= 1.0f; }
    {
        // 斜めでも速さが変わらないように、長さを1にそろえる
        const float length = std::sqrt(keyMove.x * keyMove.x + keyMove.y * keyMove.y);
        if (length > 1.0f) {
            keyMove.x /= length;
            keyMove.y /= length;
        }
    }
    const bool keyDeadEye = input.PushKey(DIK_LSHIFT) || input.PushKey(DIK_Q);
    const bool keyJump = input.PushKey(DIK_SPACE);
    const bool keyJumpPressed = input.TriggerKey(DIK_SPACE);
    const bool keyFire = mouseLeft_ || input.PushKey(DIK_J);
    const bool keyFirePressed = (mouseLeft_ && !prevMouseLeft_) || input.TriggerKey(DIK_J);
    const bool mouseMoved = mouseInside_ && (std::fabs(mouse_.x - prevMouse_.x) > 0.5f || std::fabs(mouse_.y - prevMouse_.y) > 0.5f);
    const bool keyboardUsed = keyMove.x != 0.0f || keyMove.y != 0.0f || keyJump || keyFire || mouseMoved;

    //--------------------------------------------------
    // ゲームパッド
    //--------------------------------------------------
    Vector2 padMove{ 0.0f, 0.0f };
    Vector2 padAim{ 0.0f, 0.0f };
    bool padJump = false;
    bool padJumpPressed = false;
    bool padFire = false;
    bool padFirePressed = false;
    bool padDeadEye = false;
    if (input.HasGamepad()) {
        const DIJOYSTATE2& js = input.GetJoyState();
        const Vector2 left = ReadStick(js.lX, js.lY, deadZone);
        padMove = { left.x, -left.y }; // DirectInput は下が +y なので上下をひっくり返す
        const Vector2 pov = ReadPov(js.rgdwPOV[0]);
        if (padMove.x == 0.0f && padMove.y == 0.0f) {
            padMove = pov; // スティックを倒していなければ十字キー
        }

        Vector2 right{ 0.0f, 0.0f };
        if (rightStick == RightStick::XboxRxRy) {
            right = ReadStick(js.lRx, js.lRy, deadZone);
        }
        else {
            right = ReadStick(js.lZ, js.lRz, deadZone);
        }
        padAim = { right.x, -right.y };

        padDeadEye = input.PushGamepadButton(4);                // LB
        padJump = input.PushGamepadButton(0);                   // A
        padJumpPressed = input.TriggerGamepadButton(0);
        const bool rt = (rightStick == RightStick::XboxRxRy) && js.lZ < -16000; // Xbox のRTは Z 軸がマイナスに振れる
        padFire = input.PushGamepadButton(5) || rt;             // RB か RT
        padFirePressed = input.TriggerGamepadButton(5) || (rt && !(prevRt_));
        prevRt_ = rt;
    }
    const bool gamepadUsed = padMove.x != 0.0f || padMove.y != 0.0f || padAim.x != 0.0f || padAim.y != 0.0f || padJump || padFire;

    //--------------------------------------------------
    // 使っている側を決める（最後に操作したほう。同時なら今のまま）
    //--------------------------------------------------
    if (gamepadUsed && !keyboardUsed) {
        device_ = Device::Gamepad;
    }
    else if (keyboardUsed && !gamepadUsed) {
        device_ = Device::Keyboard;
    }

    //--------------------------------------------------
    // 窓口の値を決める
    //--------------------------------------------------
    // 移動・ジャンプ・撃つは、どちらの操作でも使えるようにする（キーボードとパッドを同時に使っても動く）
    move_ = (keyMove.x != 0.0f || keyMove.y != 0.0f) ? keyMove : padMove;
    deadEye_ = keyDeadEye || padDeadEye;
    jump_ = keyJump || padJump;
    jumpPressed_ = keyJumpPressed || padJumpPressed;
    fire_ = keyFire || padFire;
    firePressed_ = keyFirePressed || padFirePressed;

    // 狙いは、使っている側で取り方が違う
    hasAim_ = false;
    if (device_ == Device::Gamepad) {
        const float length = std::sqrt(padAim.x * padAim.x + padAim.y * padAim.y);
        if (length > 0.0f) {
            aim_ = { padAim.x / length, padAim.y / length };
            hasAim_ = true;
        }
    }
    else if (mouseInside_) {
        // 自機から見たマウスの向き（画面は下が +y なので、上下をひっくり返す）
        const float dx = mouse_.x - aimOrigin_.x;
        const float dy = -(mouse_.y - aimOrigin_.y);
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 1.0f) {
            aim_ = { dx / length, dy / length };
            hasAim_ = true;
        }
    }

    prevMouse_ = mouse_;
    prevMouseLeft_ = mouseLeft_;
}

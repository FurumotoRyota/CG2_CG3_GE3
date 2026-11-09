#pragma once
// ゲーム用の「入力の窓口」
// ゲームのコードは、キーボードやゲームパッドを直接見ずに、この窓口から値だけを受け取る。
//   Move()  : 移動の向き（-1～1）。WASD / 矢印キー / 左スティック / 十字キー
//   Aim()   : 狙いの向き（長さ1）。マウス（自機から見たマウスの向き） / 右スティック
//   Fire()  : 撃つ。マウス左 / Jキー / ゲームパッドのRB・RT
//   DeadEye(): デッドアイ。Shift / Qキー / ゲームパッドのLB（押している間）
//   Jump()  : ジャンプ。Space / ゲームパッドのAボタン
// ベクトルはどれも「右が +x、上が +y」。
// 最後に操作したのがキーボード側かパッド側かを自動で見分けて、狙いの取り方を切り替える。
#include "../math/Vector2.h"

class Input;

class GameInput
{
public:
    enum class Device { Keyboard, Gamepad };

    // 右スティックの軸の割り当て（コントローラーによって違う）
    enum class RightStick { XboxRxRy, GenericZRz };

    // 毎フレーム1回。マウスは SetMouse で先に渡しておく
    void Update(const Input& input);

    // マウスの位置（ゲーム画面のピクセル座標）、画面内にあるか、左ボタンを押しているか
    void SetMouse(const Vector2& gamePixel, bool inside, bool leftDown);

    // 自機の画面上の位置（ゲーム画面のピクセル座標）。マウスで狙う向きの基準になる
    void SetAimOrigin(const Vector2& screenPos) { aimOrigin_ = screenPos; }

    // 移動・狙いをゲーム側で置き換える（カメラの向きに合わせたい場合など。Update() のあとに呼ぶ）
    void Override(const Vector2& move, const Vector2& aim, bool hasAim) { move_ = move; aim_ = aim; hasAim_ = hasAim; }

    // ---- ゲームから使う ----
    Vector2 Move() const { return move_; }
    Vector2 Aim() const { return aim_; }
    bool HasAim() const { return hasAim_; }       // 狙いの向きが決まっているか（スティックを倒している / マウスが画面内）
    bool Fire() const { return fire_; }           // 押している間
    bool FirePressed() const { return firePressed_; } // 押した瞬間
    bool DeadEye() const { return deadEye_; }     // 押している間
    bool Jump() const { return jump_; }
    bool JumpPressed() const { return jumpPressed_; }
    Device GetDevice() const { return device_; }

    // ---- 調整 ----
    float deadZone = 0.25f;                       // スティックの遊び（0～1）
    RightStick rightStick = RightStick::XboxRxRy;

private:
    Vector2 move_{ 0.0f, 0.0f };
    Vector2 aim_{ 1.0f, 0.0f };
    bool hasAim_ = false;
    bool fire_ = false;
    bool firePressed_ = false;
    bool deadEye_ = false;
    bool jump_ = false;
    bool jumpPressed_ = false;
    Device device_ = Device::Keyboard;

    Vector2 mouse_{ 0.0f, 0.0f };
    Vector2 prevMouse_{ 0.0f, 0.0f };
    bool mouseInside_ = false;
    bool mouseLeft_ = false;
    bool prevMouseLeft_ = false;
    Vector2 aimOrigin_{ 640.0f, 360.0f };
    bool prevRt_ = false; // 前のフレームでRTを押していたか
};

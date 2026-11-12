#pragma once
// 自由に飛び回れるカメラ（デバッグ用。シェーダーや光の見え方を、好きな角度から確かめる）
//   移動: W S A D（前後・左右）  Space（上） C（下）  Z 押しながら＝速い  X 押しながら＝ゆっくり
//   向き: 矢印キー  マウス右ボタンを押しながらドラッグ
//   ゲームパッド: 左スティック＝移動  右スティック＝向き
#include "Camera.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"

class Input;

class FreeCamera : public Camera
{
public:
    // いまのカメラ（ゲームのカメラ）と同じ位置・向き・画角から始める
    void StartFrom(const Camera& source);

    // lookDelta はマウスのドラッグ量（ピクセル。右ボタンを押していないときは 0）、dt は経過秒
    void Update(const Input& input, const Vector2& lookDelta, float dt);

    Vector3 GetPosition() const { return position_; }

private:
    Vector3 position_{ 0.0f, 10.0f, -10.0f };
    float yaw_ = 0.0f;   // 左右の向き（ラジアン。0 で +z 向き）
    float pitch_ = 0.0f; // 上下の向き（上を向くと正）

    void Apply();
};

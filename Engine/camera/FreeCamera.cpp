#include "FreeCamera.h"

#include <algorithm>
#include <cmath>

#include "../input/Input.h"
#include <dinput.h>

void FreeCamera::StartFrom(const Camera& source)
{
    projectionMatrix_ = source.GetProjectionMatrix();
    // ビュー行列の逆行列＝カメラの置き場。3行目が前向き、4行目が位置
    const Matrix4x4 world = Inverse(source.GetViewMatrix());
    position_ = { world.m[3][0], world.m[3][1], world.m[3][2] };
    Vector3 f{ world.m[2][0], world.m[2][1], world.m[2][2] };
    const float len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    if (len > 0.0001f) {
        f = { f.x / len, f.y / len, f.z / len };
    }
    else {
        f = { 0.0f, 0.0f, 1.0f };
    }
    yaw_ = std::atan2(f.x, f.z);
    pitch_ = std::asin((std::min)((std::max)(f.y, -1.0f), 1.0f));
    Apply();
}

void FreeCamera::Update(const Input& input, const Vector2& lookDelta, float dt)
{
    const BYTE* key = input.GetKeys();

    // 向き
    const float rotSpeed = 1.6f * dt; // 矢印キー：1秒に約1.6ラジアン
    if (key[DIK_LEFT]) { yaw_ -= rotSpeed; }
    if (key[DIK_RIGHT]) { yaw_ += rotSpeed; }
    if (key[DIK_UP]) { pitch_ += rotSpeed; }
    if (key[DIK_DOWN]) { pitch_ -= rotSpeed; }
    yaw_ += lookDelta.x * 0.003f;
    pitch_ -= lookDelta.y * 0.003f; // 下へドラッグ＝下を向く

    // 動き
    float forward = 0.0f, right = 0.0f, up = 0.0f;
    if (key[DIK_W]) { forward += 1.0f; }
    if (key[DIK_S]) { forward -= 1.0f; }
    if (key[DIK_D]) { right += 1.0f; }
    if (key[DIK_A]) { right -= 1.0f; }
    if (key[DIK_SPACE]) { up += 1.0f; }
    if (key[DIK_C]) { up -= 1.0f; }

    if (input.HasGamepad()) {
        const DIJOYSTATE2& js = input.GetJoyState();
        const float dead = 3000.0f;
        auto axis = [dead](LONG v) { return (v > dead || v < -dead) ? static_cast<float>(v) / 32768.0f : 0.0f; };
        right += axis(js.lX);
        forward -= axis(js.lY); // 上に倒すと負
        yaw_ += axis(js.lRx) * rotSpeed;
        pitch_ -= axis(js.lRy) * rotSpeed;
    }

    float speed = 10.0f; // 1秒に進む距離
    if (key[DIK_Z]) { speed *= 3.0f; }
    if (key[DIK_X]) { speed *= 0.3f; }

    pitch_ = (std::min)((std::max)(pitch_, -1.5f), 1.5f);
    const float cy = std::cos(yaw_), sy = std::sin(yaw_);
    const float cp = std::cos(pitch_), sp = std::sin(pitch_);
    // 前＝見ている向き、右＝水平な右、上＝世界の上
    position_.x += (sy * cp * forward + cy * right) * speed * dt;
    position_.y += (sp * forward + up) * speed * dt;
    position_.z += (cy * cp * forward - sy * right) * speed * dt;
    Apply();
}

void FreeCamera::Apply()
{
    const float cp = std::cos(pitch_);
    const Vector3 dir{ std::sin(yaw_) * cp, std::sin(pitch_), std::cos(yaw_) * cp };
    SetLookAt(position_, { position_.x + dir.x, position_.y + dir.y, position_.z + dir.z }, { 0.0f, 1.0f, 0.0f });
}

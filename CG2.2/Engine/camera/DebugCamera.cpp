#include "DebugCamera.h"
#include "../input/Input.h"
#include <dinput.h>

namespace
{
    Vector3 TransformDirection(const Vector3& v, const Matrix4x4& m) {
        return {
            v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
            v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]
        };
    }
}

void DebugCamera::Initialize(float aspectRatio)
{
    translation_ = { 0.0f, 0.0f, -50.0f };
    matRot_ = Matrix4x4::MakeIdentity4x4();
    projectionMatrix_ = Matrix4x4::MakePerspectiveFovMatrix(0.45f, aspectRatio, 0.1f, 100.0f);
    UpdateViewMatrix();
}

void DebugCamera::Update(const uint8_t key[256], const uint8_t keyPre[256], const DIJOYSTATE2* joyState)
{
    Move(key, keyPre, joyState);
    UpdateViewMatrix();
}

void DebugCamera::Update(const Input& input)
{
    // 無効のときはキーボードを何も押していない扱いにする（ゲームパッドは常に有効）
    static const uint8_t kNoKeys[256] = {};
    const uint8_t* keys = controlEnabled_ ? input.GetKeys() : kNoKeys;
    Update(keys, input.GetPrevKeys(), input.HasGamepad() ? &input.GetJoyState() : nullptr);
}

void DebugCamera::Move(const uint8_t key[256], const uint8_t prevKey[256], const DIJOYSTATE2* joyState)
{
    const float kRotSpeed = 0.02f;
    const float kMoveSpeed = 0.5f;

    //============================================
    // 回転
    //============================================
    Matrix4x4 matRotDelta = Matrix4x4::MakeIdentity4x4();

    if (key[DIK_UP])
    {
        matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateXMatrix(-kRotSpeed));
    }

    if (key[DIK_DOWN])
    {
        matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateXMatrix(kRotSpeed));
    }

    if (key[DIK_LEFT])
    {
        matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateYMatrix(-kRotSpeed));
    }

    if (key[DIK_RIGHT])
    {
        matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateYMatrix(kRotSpeed));
    }

    //============================================
    // マウスでの視点回転（右クリックホールド中）
    //============================================
    const float kMouseSensitivity = 0.003f; // 1pxあたりの回転量(ラジアン)
    if (mouseDeltaX_ != 0.0f)
    {
        matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateYMatrix(mouseDeltaX_ * kMouseSensitivity));
    }
    if (mouseDeltaY_ != 0.0f)
    {
        matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateXMatrix(mouseDeltaY_ * kMouseSensitivity));
    }
    mouseDeltaX_ = 0.0f;
    mouseDeltaY_ = 0.0f;

    //============================================
    // ゲームパッド：右スティックで視点回転（接続されている場合のみ）
    // ※コントローラーによって右スティックの軸名(lX/lY/lZ/lRx/lRy/lRz)が変わることがあるため、
    //   もし回転方向が合わない/反応しない場合はここのlRx/lRyを他の軸に差し替えてみてください
    //============================================
    const float kStickDeadZone = 3000.0f;  // -32768～32767に対するデッドゾーン
    const float kStickDivisor = 32768.0f;

    if (joyState != nullptr)
    {
        float rotX = static_cast<float>(joyState->lRx);
        float rotY = static_cast<float>(joyState->lRy);

        if (rotX > kStickDeadZone || rotX < -kStickDeadZone) {
            matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateYMatrix((rotX / kStickDivisor) * kRotSpeed));
        }
        if (rotY > kStickDeadZone || rotY < -kStickDeadZone) {
            matRotDelta = Multiply(matRotDelta, Matrix4x4::MakeRotateXMatrix((rotY / kStickDivisor) * kRotSpeed));
        }
    }

    matRot_ = Multiply(matRotDelta, matRot_);

    //==================================================
    // 平行移動（画面端へのドラッグによる自動スクロール）
    // A/D・Space/LShiftと同じ速度・同じ処理（panRight_/panUpは-1,0,1の方向指定）
    //==================================================
    if (panRight_ != 0.0f || panUp_ != 0.0f)
    {
        translation_ = Add(translation_, TransformDirection({ panRight_ * kMoveSpeed, panUp_ * kMoveSpeed, 0.0f }, matRot_));
        panRight_ = 0.0f;
        panUp_ = 0.0f;
    }

    //==================================================
    // ズーム（マウスホイール / タッチパッドの2本指スライド）：視線方向に前進・後退する
    //==================================================
    if (zoomWheel_ != 0.0f)
    {
        const float kZoomSpeed = 2.0f; // ホイール1目盛りあたりの移動量
        translation_ = Add(translation_, TransformDirection({ 0.0f, 0.0f, zoomWheel_ * kZoomSpeed }, matRot_));
        zoomWheel_ = 0.0f;
    }

    //==================================================
    // 前後移動（W/S）
    //==================================================
    if (key[DIK_W]) {
        translation_ = Add(translation_, TransformDirection({ 0.0f, 0.0f, kMoveSpeed }, matRot_));
    }
    if (key[DIK_S]) {
        translation_ = Add(translation_, TransformDirection({ 0.0f, 0.0f, -kMoveSpeed }, matRot_));
    }

    //==================================================
    // 左右移動（A/D）
    //==================================================
    if (key[DIK_D]) {
        translation_ = Add(translation_, TransformDirection({ kMoveSpeed, 0.0f, 0.0f }, matRot_));
    }
    if (key[DIK_A]) {
        translation_ = Add(translation_, TransformDirection({ -kMoveSpeed, 0.0f, 0.0f }, matRot_));
    }

    //==================================================
    // 上下移動（Space/LShift）
    //==================================================
    if (key[DIK_SPACE]) {
        translation_ = Add(translation_, TransformDirection({ 0.0f, kMoveSpeed, 0.0f }, matRot_));
    }
    if (key[DIK_LSHIFT]) {
        translation_ = Add(translation_, TransformDirection({ 0.0f, -kMoveSpeed, 0.0f }, matRot_));
    }

    //==================================================
    // ゲームパッド：左スティックで前後左右移動（接続されている場合のみ）
    //==================================================
    if (joyState != nullptr)
    {
        float moveX = static_cast<float>(joyState->lX);
        float moveY = static_cast<float>(joyState->lY); // 生値は上に倒すと負になることが多い

        if (moveX > kStickDeadZone || moveX < -kStickDeadZone) {
            translation_ = Add(translation_, TransformDirection({ (moveX / kStickDivisor) * kMoveSpeed, 0.0f, 0.0f }, matRot_));
        }
        if (moveY > kStickDeadZone || moveY < -kStickDeadZone) {
            translation_ = Add(translation_, TransformDirection({ 0.0f, 0.0f, (-moveY / kStickDivisor) * kMoveSpeed }, matRot_));
        }
    }
}

void DebugCamera::UpdateViewMatrix() {
    // 座標から平行移動行列を計算する
    Matrix4x4 translateMatrix = Matrix4x4::MakeTranslateMatrix(translation_);
    // 累積回転行列と平行移動行列からワールド行列を計算する
    Matrix4x4 worldMatrix = Multiply(matRot_, translateMatrix);
    // ワールド行列の逆行列をビュー行列に代入する
    viewMatrix_ = Inverse(worldMatrix);
}
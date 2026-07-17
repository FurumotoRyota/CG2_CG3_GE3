#include "DebugCamera.h"
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

void DebugCamera::Update(const uint8_t key[256], const uint8_t keyPre[256])
{
	Move(key, keyPre);
	UpdateViewMatrix();
}

void DebugCamera::Move(const uint8_t key[256], const uint8_t prevKey[256])
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

	matRot_ = Multiply(matRotDelta, matRot_);


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
}

void DebugCamera::UpdateViewMatrix() {
    // 座標から平行移動行列を計算する
    Matrix4x4 translateMatrix = Matrix4x4::MakeTranslateMatrix(translation_);
    // 累積回転行列と平行移動行列からワールド行列を計算する
    Matrix4x4 worldMatrix = Multiply(matRot_, translateMatrix);
    // ワールド行列の逆行列をビュー行列に代入する
    viewMatrix_ = Inverse(worldMatrix);
}

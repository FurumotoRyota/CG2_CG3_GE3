#pragma once
#include "Vector3.h"
#include"Matrix4x4.h"
#include <cstdint>

// DirectInputのジョイスティック状態構造体（dinput.hをここではincludeせず前方宣言のみ。
// 実体を使うDebugCamera.cpp側で<dinput.h>をincludeする）
struct DIJOYSTATE2;

/// <summary>
///	デバッグカメラ
/// </summary>


class DebugCamera
{
public:
	void Initialize(float aspectRatio);

	// joyState: ゲームパッドが接続されていればそのフレームの状態を渡す。未接続ならnullptrでよい
	void Update(const uint8_t key[256], const uint8_t prevKey[256], const DIJOYSTATE2* joyState = nullptr);

	//ビュー行列を取得
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	//射影行列を取得
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

private:
	//ローカル座標
	Vector3 translation_ = { 0.0f, 0.0f, -5.0f };

	//累積回転角
	Matrix4x4 matRot_ = Matrix4x4::MakeIdentity4x4();

	//ビュー行列
	Matrix4x4 viewMatrix_ = Matrix4x4::MakeIdentity4x4();
	//射影行列
	Matrix4x4 projectionMatrix_ = Matrix4x4::MakeIdentity4x4();

	void Move(const uint8_t key[256], const uint8_t prevKey[256], const DIJOYSTATE2* joyState);

	void UpdateViewMatrix();
};
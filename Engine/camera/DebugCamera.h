#pragma once
#include "../math/Vector3.h"
#include "../math/Matrix4x4.h"
#include "Camera.h"
#include <cstdint>

// DirectInputのジョイスティック状態構造体（dinput.hをここではincludeせず前方宣言のみ。
// 実体を使うDebugCamera.cpp側で<dinput.h>をincludeする）
struct DIJOYSTATE2;

/// <summary>
///	デバッグカメラ
/// </summary>


class DebugCamera : public Camera
{
public:
	void Initialize(float aspectRatio);

	// joyState: ゲームパッドが接続されていればそのフレームの状態を渡す。未接続ならnullptrでよい
	void Update(const uint8_t key[256], const uint8_t prevKey[256], const DIJOYSTATE2* joyState = nullptr);

	// Camera基底のインターフェース（Inputからキー/ゲームパッド状態を取り出して上のUpdateを呼ぶ）
	void Update(const Input& input) override;
	// ビュー行列・射影行列は Camera::GetViewMatrix / GetProjectionMatrix で取得する

	// キーボード操作(WASD・Space/Shift・矢印)の有効/無効。エディタでは右クリックホールド中だけ有効にする
	// ※ゲームパッドは常に有効
	void SetControlEnabled(bool enabled) { controlEnabled_ = enabled; }

	// ズーム（マウスホイール量を毎フレーム渡す。奥へ回す(正)と前進、手前へ回す(負)と後退）
	void SetZoom(float wheel) { zoomWheel_ = wheel; }

	// 平行移動（カメラの向きに対して 右(+x)・上(+y)。ドラッグで画面端に寄せたときの自動スクロール用）
	// 毎フレーム渡す。1フレームで動く量[ワールド単位]
	void SetPan(float right, float up) { panRight_ = right; panUp_ = up; }

	// マウスでの視点回転（右クリックホールド中の移動量[px]を毎フレーム渡す）
	void SetMouseLook(float deltaX, float deltaY) { mouseDeltaX_ = deltaX; mouseDeltaY_ = deltaY; }

private:
	//ローカル座標
	Vector3 translation_ = { 0.0f, 0.0f, -5.0f };

	//累積回転角
	Matrix4x4 matRot_ = Matrix4x4::MakeIdentity4x4();

	bool controlEnabled_ = true;
	float mouseDeltaX_ = 0.0f;
	float mouseDeltaY_ = 0.0f;
	float zoomWheel_ = 0.0f;
	float panRight_ = 0.0f;
	float panUp_ = 0.0f;

	void Move(const uint8_t key[256], const uint8_t prevKey[256], const DIJOYSTATE2* joyState);

	void UpdateViewMatrix();
};
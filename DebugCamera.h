#pragma 
#include "Vector3.h"
#include"Matrix4x4.h"
#include <cstdint>

/// <summary>
///	デバッグカメラ
/// </summary>


class DebugCamera
{
public:
	void Initialize(float aspectRatio);

	void Update(const uint8_t key[256], const uint8_t prevKey[256]);

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

	void Move(const uint8_t key[256], const uint8_t prevKey[256]);

	void UpdateViewMatrix();
};


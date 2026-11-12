#pragma once
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"

class Input;

/// <summary>
/// カメラの基底クラス
/// 基底そのものは「Transformで位置・向きを指定する固定カメラ」として使える。
/// DebugCameraなど操作できるカメラは Update() をオーバーライドして作る
/// </summary>
class Camera
{
public:
    Camera();
    virtual ~Camera() = default;

    // 毎フレーム1回呼ぶ。基底版はTransformからビュー行列を作り直すだけ
    virtual void Update(const Input& input);

    void SetPerspective(float fovY, float aspectRatio, float nearClip, float farClip);

    // 見る位置・見る先・上の向きでビュー行列を作る（追従カメラなどに使う）。Update() を呼ぶとTransformの内容で上書きされるので、このあとは呼ばない
    void SetLookAt(const Vector3& eye, const Vector3& target, const Vector3& up);

    Transform& GetTransform() { return transform_; }
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

protected:
    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };
    Matrix4x4 viewMatrix_ = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 projectionMatrix_ = Matrix4x4::MakeIdentity4x4();
};

#include "Camera.h"
#include "../base/WinApp.h"

Camera::Camera()
{
    SetPerspective(0.45f, static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight, 0.1f, 100.0f);
    viewMatrix_ = Inverse(Matrix4x4::MakeAffineMatrix(transform_.Scale, transform_.Rotate, transform_.Translate));
}

void Camera::Update(const Input&)
{
    viewMatrix_ = Inverse(Matrix4x4::MakeAffineMatrix(transform_.Scale, transform_.Rotate, transform_.Translate));
}

void Camera::SetPerspective(float fovY, float aspectRatio, float nearClip, float farClip)
{
    projectionMatrix_ = Matrix4x4::MakePerspectiveFovMatrix(fovY, aspectRatio, nearClip, farClip);
}

void Camera::SetLookAt(const Vector3& eye, const Vector3& target, const Vector3& up)
{
    const Vector3 z = target.Subtract(eye).Normalize();       // 前
    const Vector3 x = up.Cross(z).Normalize();                // 右
    const Vector3 y = z.Cross(x);                             // 上
    Matrix4x4 v = Matrix4x4::MakeIdentity4x4();
    v.m[0][0] = x.x; v.m[0][1] = y.x; v.m[0][2] = z.x;
    v.m[1][0] = x.y; v.m[1][1] = y.y; v.m[1][2] = z.y;
    v.m[2][0] = x.z; v.m[2][1] = y.z; v.m[2][2] = z.z;
    v.m[3][0] = -x.Dot(eye);
    v.m[3][1] = -y.Dot(eye);
    v.m[3][2] = -z.Dot(eye);
    viewMatrix_ = v;
    transform_.Translate = eye;
}

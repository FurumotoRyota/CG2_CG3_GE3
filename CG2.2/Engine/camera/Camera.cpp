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

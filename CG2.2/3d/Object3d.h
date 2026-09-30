#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>

#include "../base/BlendMode.h"
#include "../base/ShaderTypes.h"
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"

class Object3dCommon;
class Model;

/// <summary>
/// 3Dオブジェクト1体ぶん（Transform・マテリアル・WVP）
/// 形状はModel(共有)を参照する
/// </summary>
class Object3d
{
public:
    void Initialize(Object3dCommon* common, const std::string& modelName);

    // モデルを差し替える（同名モデルは読み込み済みのものを再利用する）
    void SetModel(const std::string& modelName);

    // 自分のTransformでWVPとUV変換行列を更新する
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);
    // 描画用に加工したTransform（演出で拡縮・回転を上乗せした値など）でWVPを更新する
    void Update(const Transform& renderTransform, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);

    void Draw();

    Transform& GetTransform() { return transform_; }
    Transform& GetUvTransform() { return uvTransform_; }
    Material* GetMaterial() { return materialData_; }
    const std::string& GetModelName() const { return modelName_; }
    const Model* GetModel() const { return model_; }

    BlendMode GetBlendMode() const { return blendMode_; }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }

    // 両面描画（裏面も描く）。薄い板・フェンスなど向け。既定はオフ
    bool IsDoubleSided() const { return doubleSided_; }
    void SetDoubleSided(bool doubleSided) { doubleSided_ = doubleSided; }

private:
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;
    std::string modelName_;
    BlendMode blendMode_ = BlendMode::None; // 既定は不透明（混ぜない）
    bool doubleSided_ = false;

    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform uvTransform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
};

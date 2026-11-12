#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <string>

#include "../base/BlendMode.h"
#include "../base/ShaderTypes.h"
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"

class Object3dCommon;
class Model;
class SkinInstance;

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
    // 影の描画パス用（影を落とす設定のときだけ、形を光から見た位置で描く）
    void DrawShadow(ID3D12GraphicsCommandList* commandList);

    // --- 骨つきモデル（FBX など）のとき ---
    // 骨つきモデルを読むと、SetModel / Initialize の時点で、この個体専用の頂点バッファを持つ。
    // Update(view, proj) とは別に、毎フレーム UpdateSkin(dt) を呼ぶと、アニメーションが進む。
    bool IsSkinned() const { return skin_ != nullptr; }
    // アニメーションを切り替える（モデルに無い名前は false）。同じアニメがループ再生中なら何もしない
    //   blend：前の姿勢から混ぜる秒数　speed：再生の速さ（1が元の速さ）
    //   restart：false なら、ループしないアニメでも、すでに同じものが流れていれば何もしない（倒れたままにするときなど）
    bool PlayAnimation(const std::string& clip, bool loop = true, float blend = 0.15f, float speed = 1.0f, bool restart = true);
    void UpdateSkin(float dt);
    bool HasAnimation(const std::string& clip) const;
    bool IsAnimationFinished() const; // ループしないアニメが最後まで進んだか
    // 骨に付いている点が、いまワールドのどこにあるか（restPoint は静止姿勢でのモデル座標の点）。手の位置に武器を置くときなど
    bool GetBonePointWorld(const std::string& boneName, const Vector3& restPoint, Vector3& out) const;

    Transform& GetTransform() { return transform_; }
    Transform& GetUvTransform() { return uvTransform_; }
    Material* GetMaterial() { return materialData_; }
    const std::string& GetModelName() const { return modelName_; }
    const Model* GetModel() const { return model_; }

    BlendMode GetBlendMode() const { return blendMode_; }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }

    // Use a texture that is not managed by TextureManager (e.g. a video frame) for the whole model
    void SetExternalTexture(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) { externalSrv_ = srvHandle; useExternalTexture_ = true; }
    void ClearExternalTexture() { useExternalTexture_ = false; }

    // 影を落とすか（既定はオン）。影を受けるかは GetMaterial()->receiveShadow
    bool GetCastShadow() const { return castShadow_; }
    void SetCastShadow(bool cast) { castShadow_ = cast; }

    // 両面描画（裏面も描く）。薄い板・フェンスなど向け。既定はオフ
    bool IsDoubleSided() const { return doubleSided_; }
    void SetDoubleSided(bool doubleSided) { doubleSided_ = doubleSided; }

private:
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;
    std::string modelName_;
    BlendMode blendMode_ = BlendMode::None; // 既定は不透明（混ぜない）
    bool doubleSided_ = false;
    bool castShadow_ = true;
    bool useExternalTexture_ = false;
    D3D12_GPU_DESCRIPTOR_HANDLE externalSrv_{};

    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform uvTransform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    std::shared_ptr<SkinInstance> skin_; // 骨つきモデルのときだけ

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
};

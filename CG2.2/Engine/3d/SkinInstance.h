#pragma once
// 【部品】骨つきモデル1体ぶんの「いまの姿勢」
//   Model（形・骨・アニメーションは共有）に対して、Object3d 1個ごとに1つ持つ。
//   ・Play() でアニメーションを選ぶ（前のアニメから、少しずつ混ぜて切り替える）
//   ・Update(dt) で時間を進めて、骨を動かし、頂点を計算して専用の頂点バッファに書く
//   ・頂点バッファは3つ持って順番に使う（GPU が前のフレームで読んでいるものを、CPU が上書きしないように）
#include <d3d12.h>
#include <wrl/client.h>

#include <cstring>
#include <string>
#include <vector>

#include "../base/DirectXCommon.h"
#include "../3d/SkinData.h"

class SkinInstance
{
public:
    void Initialize(DirectXCommon* dxCommon, const SkinData* data)
    {
        data_ = data;
        const size_t meshCount = data_->meshes.size();
        for (int f = 0; f < kFrames; ++f) {
            resources_[f].assign(meshCount, Microsoft::WRL::ComPtr<ID3D12Resource>());
            mapped_[f].assign(meshCount, static_cast<VertexData*>(nullptr));
            views_[f].assign(meshCount, D3D12_VERTEX_BUFFER_VIEW{});
            for (size_t m = 0; m < meshCount; ++m) {
                const std::vector<VertexData>& base = data_->meshes[m].baseVertices;
                if (base.empty()) {
                    continue; // 頂点0のバッファは作れない（Model と同じ）
                }
                const size_t bytes = sizeof(VertexData) * base.size();
                resources_[f][m] = dxCommon->CreateBufferResource(bytes);
                resources_[f][m]->Map(0, nullptr, reinterpret_cast<void**>(&mapped_[f][m]));
                std::memcpy(mapped_[f][m], base.data(), bytes); // 最初は静止姿勢
                views_[f][m].BufferLocation = resources_[f][m]->GetGPUVirtualAddress();
                views_[f][m].SizeInBytes = static_cast<UINT>(bytes);
                views_[f][m].StrideInBytes = sizeof(VertexData);
            }
        }
        current_ = 0;
        ComputeGlobalsNow();
    }

    // アニメーションを切り替える。同じアニメが（ループで）再生中なら何もしない。
    // blend：前の姿勢から混ぜる時間（秒）。speed：再生の速さ（1が元の速さ）
    // restart：false なら、同じアニメがすでに流れていれば（ループしなくても）何もしない。true なら最初からやり直す
    bool Play(const std::string& clip, bool loop = true, float blend = 0.15f, float speed = 1.0f, bool restart = true)
    {
        const int index = data_->FindClip(clip);
        if (index < 0) {
            return false;
        }
        speed_ = speed;
        if (index == clip_ && loop_ == loop && (loop || !restart)) {
            return true; // すでに再生中（位相をリセットしない）
        }
        // 前の姿勢を覚えておき、新しいアニメへ混ぜていく
        if (clip_ >= 0 && blend > 0.0f) {
            prevClip_ = clip_;
            prevTime_ = time_;
            blendTime_ = blend;
            blendElapsed_ = 0.0f;
        }
        else {
            prevClip_ = -1;
        }
        clip_ = index;
        loop_ = loop;
        time_ = 0.0f;
        finished_ = false;
        return true;
    }

    void SetSpeed(float speed) { speed_ = speed; }

    // 時間を進めて、頂点を作り直す（描画の前に、毎フレーム1回）
    void Update(float dt)
    {
        if (clip_ >= 0) {
            const SkinClip& c = data_->clips[clip_];
            time_ += dt * speed_;
            if (c.duration > 0.0f) {
                if (loop_) {
                    while (time_ >= c.duration) {
                        time_ -= c.duration;
                    }
                }
                else if (time_ >= c.duration) {
                    time_ = c.duration;
                    finished_ = true;
                }
            }
        }
        float weightNew = 1.0f; // 新しいアニメの割合
        if (prevClip_ >= 0) {
            blendElapsed_ += dt;
            prevTime_ += dt * speed_;
            const SkinClip& p = data_->clips[prevClip_];
            if (p.duration > 0.0f) {
                while (prevTime_ >= p.duration) {
                    prevTime_ -= p.duration; // 前のアニメは、ループしたつもりで進める（混ぜる間だけ）
                }
            }
            weightNew = blendTime_ > 0.0f ? blendElapsed_ / blendTime_ : 1.0f;
            if (weightNew >= 1.0f) {
                weightNew = 1.0f;
                prevClip_ = -1;
            }
        }
        // A=前のアニメ、B=新しいアニメ。weightB が新しい方の割合
        if (prevClip_ >= 0) {
            SkinMath::ComputeGlobals(*data_, &data_->clips[prevClip_], prevTime_, clip_ >= 0 ? &data_->clips[clip_] : nullptr, time_, weightNew, globals_);
        }
        else {
            SkinMath::ComputeGlobals(*data_, clip_ >= 0 ? &data_->clips[clip_] : nullptr, time_, nullptr, 0.0f, 0.0f, globals_);
        }

        // 使っていない頂点バッファへ書く
        current_ = (current_ + 1) % kFrames;
        for (size_t m = 0; m < data_->meshes.size(); ++m) {
            const SkinMesh& mesh = data_->meshes[m];
            if (!mapped_[current_][m] || !mesh.skinned) {
                continue; // 骨のないメッシュは最初の形のまま
            }
            SkinMath::SkinVertices(mesh, globals_, mapped_[current_][m]);
        }
    }

    // 描画に使う頂点バッファ（メッシュの数だけ並んでいる）
    const D3D12_VERTEX_BUFFER_VIEW* Views() const { return views_[current_].data(); }

    bool HasClip(const std::string& clip) const { return data_->FindClip(clip) >= 0; }
    bool IsFinished() const { return finished_; }
    const std::string& CurrentClip() const
    {
        static const std::string none;
        return clip_ >= 0 ? data_->clips[clip_].name : none;
    }
    float Time() const { return time_; }
    float Duration() const { return clip_ >= 0 ? data_->clips[clip_].duration : 0.0f; }

    // 静止姿勢の点（モデルの座標。x は反転後）が、いまの姿勢でどこに動いたか。boneName の骨に付いているものとして動かす
    //   例：手の位置に銃を置く（静止姿勢での手の位置と、前方の点の2つを動かせば、銃の向きも分かる）
    bool SkinPoint(const std::string& boneName, const Vector3& restPoint, Vector3& out) const
    {
        const int joint = data_->FindJoint(boneName);
        if (joint < 0) {
            return false;
        }
        for (const SkinMesh& mesh : data_->meshes) {
            for (size_t b = 0; b < mesh.boneJoint.size(); ++b) {
                if (mesh.boneJoint[b] != joint) {
                    continue;
                }
                const Matrix4x4 m = SkinMath::BoneMatrix(mesh, static_cast<int>(b), globals_);
                const float px = -restPoint.x, py = restPoint.y, pz = restPoint.z; // x 反転を戻して assimp 座標へ
                const float x = px * m.m[0][0] + py * m.m[1][0] + pz * m.m[2][0] + m.m[3][0];
                const float y = px * m.m[0][1] + py * m.m[1][1] + pz * m.m[2][1] + m.m[3][1];
                const float z = px * m.m[0][2] + py * m.m[1][2] + pz * m.m[2][2] + m.m[3][2];
                out = { -x, y, z };
                return true;
            }
        }
        return false;
    }

private:
    static constexpr int kFrames = 3;

    void ComputeGlobalsNow() { SkinMath::ComputeGlobals(*data_, nullptr, 0.0f, nullptr, 0.0f, 0.0f, globals_); }

    const SkinData* data_ = nullptr;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources_[kFrames];
    std::vector<VertexData*> mapped_[kFrames];
    std::vector<D3D12_VERTEX_BUFFER_VIEW> views_[kFrames];
    int current_ = 0;

    int clip_ = -1;
    float time_ = 0.0f;
    bool loop_ = true;
    float speed_ = 1.0f;
    bool finished_ = false;
    int prevClip_ = -1;
    float prevTime_ = 0.0f;
    float blendTime_ = 0.0f;
    float blendElapsed_ = 0.0f;
    std::vector<Matrix4x4> globals_;
};

#pragma once
// 【部品】GameObjectの使い回し（プール）
//   弾やエフェクトのように「たくさん出て、すぐ消える」ものを、毎回作らずに使い回す。
//   GameObject(Object3d)は作るたびにGPUのバッファを確保するため、毎フレーム作ると重くなる。
//   最初に capacity 個ぶんをまとめて作っておき、Acquire で借りて、使い終わったら Release で返す。
//
// 使い方:
//   pool.Initialize(ctx_->object3dCommon, "teapot.obj", 64);   // 最初に1回
//   GameObject* b = pool.Acquire({ x, y, z });                  // 借りる（満杯なら nullptr）
//   if (b) { b->GetObject3d().GetTransform().Scale = ...; }    // 借りた直後は Scale=1, Rotate=0, 色=白 に戻っている
//   pool.Release(b);                                            // 使い終わったら返す
//   pool.ForEachActive([&](GameObject& o) { ... });             // 使用中だけを回す（中で Release してよい）
//   pool.Update(view, proj);  pool.Draw();                      // 使用中だけを更新・描画する
//
// 守ること:
//   ・返した（Release した）あとのポインタは、もう使わない（別の弾に使い回される）
//   ・同じものを2回 Release しても害はない（2回目は何もしない）
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "GameObject.h"
#include "math/Matrix4x4.h"
#include "math/Vector3.h"

class GameObjectPool
{
public:
    // capacity 個の GameObject を先に作る（同時に出せる最大数）
    void Initialize(Object3dCommon* common, const std::string& modelName, size_t capacity)
    {
        assert(common);
        slots_.clear();
        freeList_.clear();
        slots_.resize(capacity);
        freeList_.reserve(capacity);
        for (size_t i = 0; i < capacity; ++i) {
            slots_[i].obj = std::make_unique<GameObject>();
            slots_[i].obj->Initialize(common, modelName, { 0.0f, 0.0f, 0.0f });
            slots_[i].active = false;
        }
        // 若い番号から借りられるように、後ろから積む
        for (size_t i = capacity; i > 0; --i) {
            freeList_.push_back(i - 1);
        }
        activeCount_ = 0;
    }

    // 空きを1つ借りる。使い切っていたら nullptr（呼び出し側で「出さない」を選ぶ）
    GameObject* Acquire(const Vector3& translate)
    {
        if (freeList_.empty()) {
            return nullptr;
        }
        const size_t index = freeList_.back();
        freeList_.pop_back();

        Slot& slot = slots_[index];
        slot.active = true;
        ++activeCount_;

        // 前に使ったときの変更が残らないよう、見た目を初期状態に戻す
        Object3d& obj = slot.obj->GetObject3d();
        obj.GetTransform() = Transform{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, translate };
        if (Material* m = obj.GetMaterial()) {
            m->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }
        return slot.obj.get();
    }

    // 使い終わったものを返す。このプールのものでない・すでに返したものは何もしない
    void Release(GameObject* obj)
    {
        if (!obj) {
            return;
        }
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].obj.get() == obj) {
                if (slots_[i].active) {
                    slots_[i].active = false;
                    freeList_.push_back(i);
                    --activeCount_;
                }
                return;
            }
        }
        assert(false && "このプールのGameObjectではありません");
    }

    // 全部返す（ステージのやり直しなど）
    void ReleaseAll()
    {
        freeList_.clear();
        for (size_t i = slots_.size(); i > 0; --i) {
            slots_[i - 1].active = false;
            freeList_.push_back(i - 1);
        }
        activeCount_ = 0;
    }

    // 使用中のものだけを順に渡す。func の中でそのオブジェクトを Release してもよい
    template <class Func>
    void ForEachActive(Func&& func)
    {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].active) {
                func(*slots_[i].obj);
            }
        }
    }

    // 使用中のものだけ、見た目の行列を更新する（描く前に毎フレーム）
    void Update(const Matrix4x4& view, const Matrix4x4& proj)
    {
        for (Slot& slot : slots_) {
            if (slot.active) {
                slot.obj->GetObject3d().Update(view, proj);
            }
        }
    }

    // 使用中のものだけ描く（Object3dCommon::PreDraw のあとに呼ぶ）
    void Draw()
    {
        for (Slot& slot : slots_) {
            if (slot.active) {
                slot.obj->Draw();
            }
        }
    }

    // 使用中のものだけ、影の深度を描く（ShadowMap::Begin と End の間で呼ぶ）
    void DrawShadow(ID3D12GraphicsCommandList* commandList)
    {
        for (Slot& slot : slots_) {
            if (slot.active) {
                slot.obj->GetObject3d().DrawShadow(commandList);
            }
        }
    }

    // 使っていない枠も含めて全部に渡す（作った直後に、材質など最初の設定をまとめて入れる用）
    template <class Func>
    void ForEachSlot(Func&& func)
    {
        for (Slot& slot : slots_) {
            func(*slot.obj);
        }
    }

    size_t Capacity() const { return slots_.size(); }
    size_t ActiveCount() const { return activeCount_; }
    bool IsFull() const { return freeList_.empty(); }

private:
    struct Slot
    {
        std::unique_ptr<GameObject> obj;
        bool active = false;
    };

    std::vector<Slot> slots_;
    std::vector<size_t> freeList_; // 空いている番号（後ろから借りる）
    size_t activeCount_ = 0;
};

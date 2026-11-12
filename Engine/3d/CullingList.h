#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

#include "../math/Matrix4x4.h"
#include "Object3d.h"

/// <summary>
/// 画面に映る範囲（地面の x,z の四角）。見下ろし視点で、範囲の外を描かないために使う
/// </summary>
struct VisibleRect
{
    float minX = -1.0e9f;
    float minZ = -1.0e9f;
    float maxX = 1.0e9f;
    float maxZ = 1.0e9f;

    // 中心 (x,z)・半径 r の丸が、範囲にかかるか
    bool Overlaps(float x, float z, float r) const
    {
        return x + r >= minX && x - r <= maxX && z + r >= minZ && z - r <= maxZ;
    }
};

/// <summary>
/// 画面外のオブジェクトを、更新も描画もしない仕組み（見下ろし視点向け）。
/// 使い方:
///   CullingList list;
///   list.Add(&object3d, x, z, radius);                 // 場所と大きさ（半径）を教えて登録
///   list.Update(rect, view, proj);                      // 毎フレーム：範囲の中だけ Object3d::Update
///   list.Draw();  list.DrawShadow(cmd, cx, cz, range);  // 範囲の中だけ描く（影は影の範囲でさらに絞る）
/// 登録した Object3d は、リストが消えるまで生きていること（所有はしない）
/// </summary>
class CullingList
{
public:
    struct Entry
    {
        Object3d* obj = nullptr;
        float x = 0.0f;
        float z = 0.0f;
        float radius = 0.0f;
        bool visible = true;
        int tag = -1; // 好きな目印（グループ番号など）
    };

    void Clear() { entries_.clear(); }
    void Add(Object3d* obj, float x, float z, float radius, int tag = -1)
    {
        Entry e;
        e.obj = obj;
        e.x = x;
        e.z = z;
        e.radius = radius;
        e.tag = tag;
        entries_.push_back(e);
    }

    // 範囲の中だけ更新する。更新した数を返す
    size_t Update(const VisibleRect& rect, const Matrix4x4& view, const Matrix4x4& proj)
    {
        size_t count = 0;
        for (Entry& e : entries_) {
            e.visible = rect.Overlaps(e.x, e.z, e.radius);
            if (e.visible) {
                e.obj->Update(view, proj);
                ++count;
            }
        }
        return count;
    }

    void Draw()
    {
        for (Entry& e : entries_) {
            if (e.visible) {
                e.obj->Draw();
            }
        }
    }

    // 影の深度パス用。中心 (cx,cz) から range より遠いものは描かない
    void DrawShadow(ID3D12GraphicsCommandList* commandList, float cx, float cz, float range)
    {
        for (Entry& e : entries_) {
            if (!e.visible) {
                continue;
            }
            if (std::fabs(e.x - cx) - e.radius > range || std::fabs(e.z - cz) - e.radius > range) {
                continue;
            }
            e.obj->DrawShadow(commandList);
        }
    }

    std::vector<Entry>& Entries() { return entries_; }
    size_t Size() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

#include "StaticBatcher.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "../math/Transform.h"
#include "Model.h"
#include "ModelData.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"

// まとめる単位
struct StaticBatcher::Key
{
    int tx = 0, tz = 0, group = -1;
    bool cast = true;
    int mat = 0;                  // 粗さ・金属度を整数にしたもの
    unsigned color = 0xFFFFFFFFu; // 0xFFFFFFFF なら白（色は頂点のUVなどで付いている）
    std::string tex;
    bool operator<(const Key& o) const
    {
        return std::tie(tx, tz, group, cast, mat, color, tex) < std::tie(o.tx, o.tz, o.group, o.cast, o.mat, o.color, o.tex);
    }
};

struct StaticBatcher::Acc
{
    std::vector<VertexData> v;
    float minX = 1.0e9f, maxX = -1.0e9f, minZ = 1.0e9f, maxZ = -1.0e9f;
};

void StaticBatcher::Initialize(Object3dCommon* common, ModelManager* models, const std::string& namePrefix)
{
    common_ = common;
    models_ = models;
    prefix_ = namePrefix;
}

void StaticBatcher::Clear()
{
    pieces_.clear();
    list_.Clear();
    used_ = 0;
}

void StaticBatcher::AddPiece(const Piece& piece)
{
    if (piece.vertices && !piece.vertices->empty()) {
        pieces_.push_back(piece);
    }
}

bool StaticBatcher::AddObject(Object3d& obj, int group)
{
    const Model* model = obj.GetModel();
    if (!model || model->IsSkinned() || model->GetCpuMeshes().empty()) {
        return false;
    }
    const Transform& t = obj.GetTransform();
    const Material* m = obj.GetMaterial();
    const Vector4 col = m ? m->color : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    const bool white = col.x > 0.999f && col.y > 0.999f && col.z > 0.999f;

    for (const Model::CpuMesh& mesh : model->GetCpuMeshes()) {
        Piece p;
        p.vertices = &mesh.vertices;
        p.texture = mesh.texturePath;
        p.world = Matrix4x4::MakeAffineMatrix(t.Scale, t.Rotate, t.Translate);
        p.x = t.Translate.x;
        p.z = t.Translate.z;
        p.group = group;
        p.castShadow = obj.GetCastShadow();
        p.exactColor = !white;
        p.color = col;
        if (m) {
            p.roughness = m->roughness;
            p.metallic = m->metallic;
        }
        AddPiece(p);
    }
    return true;
}

void StaticBatcher::Build()
{
    std::map<Key, Acc> accs;

    for (const Piece& p : pieces_) {
        const Matrix4x4 nm = Transpose(Inverse(p.world)); // 法線用（非均一スケールでも面に垂直なまま）

        Key key;
        key.group = p.group;
        key.tx = p.group >= 0 ? 0 : static_cast<int>(std::floor(p.x / tileSize));
        key.tz = p.group >= 0 ? 0 : static_cast<int>(std::floor(p.z / tileSize));
        key.cast = p.castShadow;
        key.mat = static_cast<int>(p.roughness * 100.0f + 0.5f) * 1000 + static_cast<int>(p.metallic * 100.0f + 0.5f);
        if (p.exactColor) {
            auto b8 = [](float c) { return static_cast<unsigned>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
            key.color = (b8(p.color.x) << 16) | (b8(p.color.y) << 8) | b8(p.color.z);
        }
        key.tex = p.texture;

        Acc& a = accs[key];
        a.v.reserve(a.v.size() + p.vertices->size());
        for (const VertexData& src : *p.vertices) {
            const Vector3 pos = Matrix4x4::Transform({ src.position.x, src.position.y, src.position.z }, p.world);
            Vector3 n = {
                src.normal.x * nm.m[0][0] + src.normal.y * nm.m[1][0] + src.normal.z * nm.m[2][0],
                src.normal.x * nm.m[0][1] + src.normal.y * nm.m[1][1] + src.normal.z * nm.m[2][1],
                src.normal.x * nm.m[0][2] + src.normal.y * nm.m[1][2] + src.normal.z * nm.m[2][2] };
            const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len > 1.0e-6f) {
                n = { n.x / len, n.y / len, n.z / len };
            }
            VertexData d{};
            d.position = { pos.x, pos.y, pos.z, 1.0f };
            d.texcoord = p.overrideUv ? p.uv : src.texcoord;
            d.normal = n;
            a.v.push_back(d);
            a.minX = (std::min)(a.minX, pos.x);
            a.maxX = (std::max)(a.maxX, pos.x);
            a.minZ = (std::min)(a.minZ, pos.z);
            a.maxZ = (std::max)(a.maxZ, pos.z);
        }
    }
    pieces_.clear();

    // まとめた頂点から、大きなメッシュを作る
    for (auto& kv : accs) {
        const Key& key = kv.first;
        Acc& a = kv.second;
        if (a.v.empty()) {
            continue;
        }
        const std::string name = prefix_ + "_" + std::to_string(serial_++);
        ModelData md;
        MeshData mesh;
        mesh.vertices = std::move(a.v);
        mesh.materialName = "m";
        md.meshes.push_back(std::move(mesh));
        md.materials["m"].textureFilePath = key.tex;
        models_->Register(name, md);

        Object3d* obj = nullptr;
        if (used_ < objects_.size()) {
            obj = objects_[used_].get();
            obj->SetModel(name);
        }
        else {
            auto p = std::make_unique<Object3d>();
            p->Initialize(common_, name);
            obj = p.get();
            objects_.push_back(std::move(p));
        }
        ++used_;

        obj->GetTransform() = Transform{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
        obj->SetBlendMode(BlendMode::None);
        obj->SetCastShadow(key.cast);
        if (Material* m = obj->GetMaterial()) {
            if (key.color != 0xFFFFFFFFu) {
                m->color = Vector4{ ((key.color >> 16) & 255u) / 255.0f, ((key.color >> 8) & 255u) / 255.0f, (key.color & 255u) / 255.0f, 1.0f };
            }
            else {
                m->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
            }
            if (usePbr) {
                m->specularMode = 3;
                m->roughness = static_cast<float>(key.mat / 1000) / 100.0f;
                m->metallic = static_cast<float>(key.mat % 1000) / 100.0f;
            }
        }

        const float w = a.maxX - a.minX;
        const float d = a.maxZ - a.minZ;
        list_.Add(obj, (a.minX + a.maxX) * 0.5f, (a.minZ + a.maxZ) * 0.5f, 0.5f * std::sqrt(w * w + d * d) + 0.5f, key.group);
    }
}

size_t StaticBatcher::Update(const Matrix4x4& view, const Matrix4x4& proj)
{
    return list_.Update(rect_, view, proj);
}

void StaticBatcher::SetGroupAlpha(int group, float alpha)
{
    const bool translucent = alpha < 0.995f;
    for (CullingList::Entry& e : list_.Entries()) {
        if (e.tag != group) {
            continue;
        }
        if (Material* m = e.obj->GetMaterial()) {
            m->color.w = alpha;
        }
        e.obj->SetBlendMode(translucent ? BlendMode::Normal : BlendMode::None);
    }
}

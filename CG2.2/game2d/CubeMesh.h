#pragma once
// 立方体のモデルデータを作る（中心原点・1辺1）。エディタの「cube」と2Dアクションの地形で共通に使う
#include <string>

#include "3d/ModelData.h"
#include "math/Vector3.h"

namespace CubeMesh
{
    // 立方体の1面（外から見て時計回りの2三角形）。center は面の中心、n は法線、up は面の上方向
    inline void AddFace(MeshData& mesh, const Vector3& center, const Vector3& n, const Vector3& up, float u, float v)
    {
        const Vector3 fwd{ -n.x, -n.y, -n.z };
        // right = up x forward（左手系）
        const Vector3 right{ up.y * fwd.z - up.z * fwd.y, up.z * fwd.x - up.x * fwd.z, up.x * fwd.y - up.y * fwd.x };
        auto corner = [&](float a, float b) {
            return Vector3{ center.x + right.x * a * 0.5f + up.x * b * 0.5f,
                            center.y + right.y * a * 0.5f + up.y * b * 0.5f,
                            center.z + right.z * a * 0.5f + up.z * b * 0.5f };
            };
        const Vector3 tl = corner(-1.0f, 1.0f);
        const Vector3 tr = corner(1.0f, 1.0f);
        const Vector3 br = corner(1.0f, -1.0f);
        const Vector3 bl = corner(-1.0f, -1.0f);
        auto push = [&](const Vector3& p) {
            VertexData vd{};
            vd.position = { p.x, p.y, p.z, 1.0f };
            vd.texcoord = { u, v };
            vd.normal = n;
            mesh.vertices.push_back(vd);
            };
        push(tl); push(tr); push(br);
        push(tl); push(br); push(bl);
    }

    // 1面ぶんのUVを (0,0)-(1,1) で貼る版（テクスチャを面ごとに貼る）
    inline void AddFaceFullUv(MeshData& mesh, const Vector3& center, const Vector3& n, const Vector3& up)
    {
        const Vector3 fwd{ -n.x, -n.y, -n.z };
        const Vector3 right{ up.y * fwd.z - up.z * fwd.y, up.z * fwd.x - up.x * fwd.z, up.x * fwd.y - up.y * fwd.x };
        auto corner = [&](float a, float b) {
            return Vector3{ center.x + right.x * a * 0.5f + up.x * b * 0.5f,
                            center.y + right.y * a * 0.5f + up.y * b * 0.5f,
                            center.z + right.z * a * 0.5f + up.z * b * 0.5f };
            };
        auto push = [&](const Vector3& p, float u, float v) {
            VertexData vd{};
            vd.position = { p.x, p.y, p.z, 1.0f };
            vd.texcoord = { u, v };
            vd.normal = n;
            mesh.vertices.push_back(vd);
            };
        const Vector3 tl = corner(-1.0f, 1.0f);
        const Vector3 tr = corner(1.0f, 1.0f);
        const Vector3 br = corner(1.0f, -1.0f);
        const Vector3 bl = corner(-1.0f, -1.0f);
        push(tl, 0.0f, 0.0f); push(tr, 1.0f, 0.0f); push(br, 1.0f, 1.0f);
        push(tl, 0.0f, 0.0f); push(br, 1.0f, 1.0f); push(bl, 0.0f, 1.0f);
    }

    // 面ごとに (0,0)-(1,1) のUVを貼った立方体。UV変換を 0 倍にすれば、テクスチャの1点だけを使う単色にもできる
    inline ModelData MakeCubeFullUv(const std::string& texture)
    {
        ModelData data;
        data.materials["m"].textureFilePath = texture;
        MeshData mesh;
        mesh.materialName = "m";
        AddFaceFullUv(mesh, { 0.5f, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 });
        AddFaceFullUv(mesh, { -0.5f, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 });
        AddFaceFullUv(mesh, { 0, 0.5f, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
        AddFaceFullUv(mesh, { 0, -0.5f, 0 }, { 0, -1, 0 }, { 0, 0, 1 });
        AddFaceFullUv(mesh, { 0, 0, 0.5f }, { 0, 0, 1 }, { 0, 1, 0 });
        AddFaceFullUv(mesh, { 0, 0, -0.5f }, { 0, 0, -1 }, { 0, 1, 0 });
        data.meshes.push_back(std::move(mesh));
        return data;
    }

    // テクスチャの (u, v) の1点だけを使う単色の立方体（texture に色パレットを渡し、白のマスのUVを指定する）
    inline ModelData MakeCube(const std::string& texture, float u, float v)
    {
        ModelData data;
        data.materials["m"].textureFilePath = texture;
        MeshData mesh;
        mesh.materialName = "m";
        AddFace(mesh, { 0.5f, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, u, v);
        AddFace(mesh, { -0.5f, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, u, v);
        AddFace(mesh, { 0, 0.5f, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, u, v);
        AddFace(mesh, { 0, -0.5f, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, u, v);
        AddFace(mesh, { 0, 0, 0.5f }, { 0, 0, 1 }, { 0, 1, 0 }, u, v);
        AddFace(mesh, { 0, 0, -0.5f }, { 0, 0, -1 }, { 0, 1, 0 }, u, v);
        data.meshes.push_back(std::move(mesh));
        return data;
    }
}

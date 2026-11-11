#pragma once
// 【部品】骨つきモデルのデータと、骨の動きから頂点の位置を求める計算
//   assimp（FBX など）から読んだ骨・アニメーションを持つ。GPU には何も渡さず、CPU で頂点を動かす方式。
//   （頂点の形・シェーダー・パイプラインは変えなくてよい。人型のような少ない頂点向け）
//
//   行列は、エンジンと同じ「行ベクトル × 行列」の向き（ベクトル * 行列 = 変換後）。
//   骨の行列 = (骨のオフセット行列) * (骨の全体行列)。assimp 座標で動かしたあと、最後に x を反転して左手系にする。
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../3d/VertexData.h"
#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"

// 頂点1つぶんの骨の影響（最大4本。bone はメッシュ内の骨の番号）
struct VertexSkin
{
    uint16_t bone[4]{ 0, 0, 0, 0 };
    float weight[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
};

// 骨の階層（ノードを全部入れる。親は必ず自分より前の番号）
struct SkinJoint
{
    std::string name;
    int parent = -1;
    Vector3 bindScale{ 1.0f, 1.0f, 1.0f };
    float bindRot[4]{ 0.0f, 0.0f, 0.0f, 1.0f }; // クォータニオン x, y, z, w
    Vector3 bindPos{ 0.0f, 0.0f, 0.0f };
};

struct SkinKeyVec
{
    float time; // 秒
    Vector3 value;
};

struct SkinKeyQuat
{
    float time; // 秒
    float q[4]; // x, y, z, w
};

// 1つの骨のキー（空なら、その要素は元の姿勢のまま）
struct SkinChannel
{
    std::vector<SkinKeyVec> position;
    std::vector<SkinKeyQuat> rotation;
    std::vector<SkinKeyVec> scale;
};

// アニメーション1本
struct SkinClip
{
    std::string name;
    float duration = 0.0f;              // 秒
    std::vector<SkinChannel> channels;  // joints と同じ数
};

// メッシュ1つぶん
struct SkinMesh
{
    bool skinned = false;                 // false なら動かさない（baseVertices をそのまま使う）
    std::vector<VertexData> baseVertices; // 元の頂点（UV など。位置・法線は動かすたびに上書きする。初期値は静止姿勢の結果）
    std::vector<Vector3> bindPosition;    // 静止姿勢の位置（assimp 座標。x 反転前）
    std::vector<Vector3> bindNormal;
    std::vector<VertexSkin> weights;
    std::vector<int> boneJoint;           // メッシュ内の骨の番号 -> joints の番号
    std::vector<Matrix4x4> boneOffset;    // メッシュ内の骨の番号 -> オフセット行列（静止姿勢の頂点を骨の座標へ）
};

struct SkinData
{
    std::vector<SkinJoint> joints;
    std::vector<SkinClip> clips;
    std::vector<SkinMesh> meshes; // ModelData.meshes と同じ並び

    int FindJoint(const std::string& name) const
    {
        for (size_t i = 0; i < joints.size(); ++i) {
            if (joints[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // "Walk" で "Rig|Walk" も見つかる（FBX は「骨組み名|アニメ名」になることがある）
    int FindClip(const std::string& name) const
    {
        for (size_t i = 0; i < clips.size(); ++i) {
            const std::string& c = clips[i].name;
            if (c == name) {
                return static_cast<int>(i);
            }
        }
        for (size_t i = 0; i < clips.size(); ++i) {
            const std::string& c = clips[i].name;
            if (c.size() > name.size() && c.compare(c.size() - name.size(), name.size(), name) == 0 && c[c.size() - name.size() - 1] == '|') {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

namespace SkinMath
{
struct Quat
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

// 拡大・回転・移動から、行ベクトル向きの行列を作る（ベクトル * 拡大 * 回転 * 移動）
inline Matrix4x4 MakeLocal(const Vector3& s, const Quat& q, const Vector3& t)
{
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    // 列ベクトル向きの回転行列 r[行][列]
    const float r[3][3] = {
        { 1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy) },
        { 2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx) },
        { 2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy) } };
    const float sc[3] = { s.x, s.y, s.z };
    Matrix4x4 m{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m.m[i][j] = sc[i] * r[j][i]; // 転置して行ベクトル向きにする
        }
    }
    m.m[3][0] = t.x;
    m.m[3][1] = t.y;
    m.m[3][2] = t.z;
    m.m[3][3] = 1.0f;
    return m;
}

inline Vector3 LerpVec(const Vector3& a, const Vector3& b, float t)
{
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

inline Quat NLerp(const Quat& a, Quat b, float t)
{
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
        b = { -b.x, -b.y, -b.z, -b.w };
    }
    Quat r{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
    const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (len > 1e-6f) {
        r.x /= len; r.y /= len; r.z /= len; r.w /= len;
    }
    else {
        r = { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    return r;
}

// キー列から時刻 t の値を求める（前後のキーの間を直線で補う）
template <class Key>
inline void FindKeys(const std::vector<Key>& keys, float t, size_t& i0, size_t& i1, float& f)
{
    i0 = i1 = 0;
    f = 0.0f;
    if (keys.size() <= 1 || t <= keys.front().time) {
        return;
    }
    if (t >= keys.back().time) {
        i0 = i1 = keys.size() - 1;
        return;
    }
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].time <= t) {
        ++i;
    }
    i0 = i;
    i1 = i + 1;
    const float span = keys[i1].time - keys[i0].time;
    f = span > 1e-6f ? (t - keys[i0].time) / span : 0.0f;
}

// 1つの骨の、時刻 t での拡大・回転・移動（キーが無い要素は元の姿勢）
inline void SampleJoint(const SkinData& data, const SkinClip* clip, int joint, float t, Vector3& pos, Quat& rot, Vector3& scl)
{
    const SkinJoint& j = data.joints[joint];
    pos = j.bindPos;
    scl = j.bindScale;
    rot = { j.bindRot[0], j.bindRot[1], j.bindRot[2], j.bindRot[3] };
    if (!clip || static_cast<size_t>(joint) >= clip->channels.size()) {
        return;
    }
    const SkinChannel& ch = clip->channels[joint];
    size_t i0, i1;
    float f;
    if (!ch.position.empty()) {
        FindKeys(ch.position, t, i0, i1, f);
        pos = LerpVec(ch.position[i0].value, ch.position[i1].value, f);
    }
    if (!ch.rotation.empty()) {
        FindKeys(ch.rotation, t, i0, i1, f);
        const SkinKeyQuat& a = ch.rotation[i0];
        const SkinKeyQuat& b = ch.rotation[i1];
        rot = NLerp({ a.q[0], a.q[1], a.q[2], a.q[3] }, { b.q[0], b.q[1], b.q[2], b.q[3] }, f);
    }
    if (!ch.scale.empty()) {
        FindKeys(ch.scale, t, i0, i1, f);
        scl = LerpVec(ch.scale[i0].value, ch.scale[i1].value, f);
    }
}

// 全部の骨の「全体行列」（根元から順にかけ合わせたもの）を求める
//   clipB が空でなければ、clipA(timeA) から clipB(timeB) へ weightB の割合で混ぜる（アニメの切り替えをなめらかにする）
inline void ComputeGlobals(const SkinData& data, const SkinClip* clipA, float timeA, const SkinClip* clipB, float timeB, float weightB,
    std::vector<Matrix4x4>& globals)
{
    globals.resize(data.joints.size());
    for (size_t j = 0; j < data.joints.size(); ++j) {
        Vector3 p, s;
        Quat q;
        SampleJoint(data, clipA, static_cast<int>(j), timeA, p, q, s);
        if (clipB && weightB > 0.0f) {
            Vector3 p2, s2;
            Quat q2;
            SampleJoint(data, clipB, static_cast<int>(j), timeB, p2, q2, s2);
            p = LerpVec(p, p2, weightB);
            s = LerpVec(s, s2, weightB);
            q = NLerp(q, q2, weightB);
        }
        const Matrix4x4 local = MakeLocal(s, q, p);
        const int parent = data.joints[j].parent;
        globals[j] = parent >= 0 ? local.Multiply(globals[parent]) : local;
    }
}

// 骨の行列（頂点 * オフセット * 全体）
inline Matrix4x4 BoneMatrix(const SkinMesh& mesh, int bone, const std::vector<Matrix4x4>& globals)
{
    return mesh.boneOffset[bone].Multiply(globals[mesh.boneJoint[bone]]);
}

// 頂点を動かして out に書く（out は baseVertices と同じ数。位置と法線を上書きし、最後に x を反転する）
inline void SkinVertices(const SkinMesh& mesh, const std::vector<Matrix4x4>& globals, VertexData* out)
{
    const size_t boneCount = mesh.boneJoint.size();
    // 骨の行列（毎フレーム呼ばれるので、メモリの確保を避けて固定の置き場を使う。骨が多いモデルだけ確保する）
    Matrix4x4 fixedMatrices[32];
    std::vector<Matrix4x4> dynamicMatrices;
    Matrix4x4* bm = fixedMatrices;
    if (boneCount > 32) {
        dynamicMatrices.resize(boneCount);
        bm = dynamicMatrices.data();
    }
    for (size_t b = 0; b < boneCount; ++b) {
        bm[b] = BoneMatrix(mesh, static_cast<int>(b), globals);
    }
    for (size_t v = 0; v < mesh.baseVertices.size(); ++v) {
        const Vector3& p = mesh.bindPosition[v];
        const Vector3& n = mesh.bindNormal[v];
        const VertexSkin& w = mesh.weights[v];
        float px = 0.0f, py = 0.0f, pz = 0.0f, nx = 0.0f, ny = 0.0f, nz = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const float wt = w.weight[k];
            if (wt <= 0.0f) {
                continue;
            }
            const Matrix4x4& m = bm[w.bone[k]];
            px += wt * (p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0]);
            py += wt * (p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1]);
            pz += wt * (p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2]);
            nx += wt * (n.x * m.m[0][0] + n.y * m.m[1][0] + n.z * m.m[2][0]);
            ny += wt * (n.x * m.m[0][1] + n.y * m.m[1][1] + n.z * m.m[2][1]);
            nz += wt * (n.x * m.m[0][2] + n.y * m.m[1][2] + n.z * m.m[2][2]);
        }
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl > 1e-6f) {
            nx /= nl; ny /= nl; nz /= nl;
        }
        out[v] = mesh.baseVertices[v];
        out[v].position = { -px, py, pz, 1.0f }; // 右手系 -> 左手系：x 反転（静的なモデルと同じ）
        out[v].normal = { -nx, ny, nz };
    }
}
}

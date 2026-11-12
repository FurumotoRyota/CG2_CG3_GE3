#include "ModelLoader.h"

#include <cassert>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "../base/Logger.h"
#include "../base/StringUtility.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

// assimp が externals/assimp に配置されている場合のみ有効になる
#if __has_include(<assimp/Importer.hpp>)
#define USE_ASSIMP 1
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#ifdef _DEBUG
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif
#endif

namespace
{
//==================================================
// マテリアルテンプレートファイル読み込み関数
// 1つのmtlファイルに複数のnewmtl(マテリアル)が定義されている場合に対応する
//==================================================

std::unordered_map<std::string, MaterialData> LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
    std::unordered_map<std::string, MaterialData> materials;
    std::string currentMaterialName;
    std::string line;
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "newmtl") {
            s >> currentMaterialName;
            materials[currentMaterialName] = MaterialData{};
        }
        else if (identifier == "map_Kd" && !currentMaterialName.empty()) {
            std::string textureFilename;
            s >> textureFilename;
            materials[currentMaterialName].textureFilePath = directoryPath + "/" + textureFilename;
        }
    }

    return materials;
}
}

namespace ModelLoader
{
//==================================================
//objをよみこむ関数
//==================================================
ModelData LoadObj(const std::string& directoryPath, const std::string& fileName)
{
    ModelData modelData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    std::ifstream file(directoryPath + "/" + fileName);
    assert(file.is_open());

    std::string currentMaterialName;
    MeshData* currentMesh = nullptr;
    bool needNewMesh = true; // trueの間はまだメッシュを確定させていない（空メッシュを作らないための遅延生成フラグ）

    // 実際に面(f)が来たときだけ新しいメッシュを確定させる
    // （o/g/usemtlが連続で出てきても、間に面が無ければ空メッシュを作らない）
    auto EnsureMesh = [&]() {
        if (needNewMesh) {
            modelData.meshes.push_back(MeshData{});
            currentMesh = &modelData.meshes.back();
            currentMesh->materialName = currentMaterialName;
            needNewMesh = false;
        }
        };

    while (std::getline(file, line))
    {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "mtllib")
        {
            std::string materialFilename;
            s >> materialFilename;
            std::unordered_map<std::string, MaterialData> loaded = LoadMaterialTemplateFile(directoryPath, materialFilename);
            for (auto& [name, mat] : loaded) {
                modelData.materials[name] = mat;
            }
        }
        else if (identifier == "usemtl")
        {
            s >> currentMaterialName;
            // マテリアルが変わったら新しいメッシュとして区切る（実際の作成は次のfで行う）
            needNewMesh = true;
        }
        else if (identifier == "o" || identifier == "g")
        {
            // objファイル内で明示的にオブジェクト/グループが区切られている場合も新しいメッシュにする
            // （o/gが連続しても、面が無ければメッシュは作らない）
            needNewMesh = true;
        }
        else if (identifier == "v")
        {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.x *= -1.0f;
            position.w = 1.0f;
            positions.push_back(position);
        }
        else if (identifier == "vt")
        {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        }
        else if (identifier == "vn")
        {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normal.x *= -1.0f;
            normals.push_back(normal);
        }
        else if (identifier == "f")
        {
            EnsureMesh();

            VertexData triangle[3]{};

            // objの面インデックスは絶対値(1始まり)と相対値(負の値、末尾からの相対位置)の
            // 両方があり得るため、両方を解決できるようにする
            auto ResolveIndex = [](int64_t rawIndex, size_t count) -> uint32_t {
                if (rawIndex < 0) {
                    // 相対index。-1は「直前(最後)に定義された要素」を指す
                    return static_cast<uint32_t>(static_cast<int64_t>(count) + rawIndex);
                }
                // 絶対index(1始まり) -> 0始まりに変換
                return static_cast<uint32_t>(rawIndex - 1);
                };

            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex)
            {
                std::string vertexDefinition;
                s >> vertexDefinition;

                std::istringstream v(vertexDefinition);
                std::string indexStr;

                // 位置番号（必須）
                std::getline(v, indexStr, '/');
                int64_t posIndexRaw = std::stoll(indexStr);
                uint32_t posIndex = ResolveIndex(posIndexRaw, positions.size());

                // UV番号（無いobjもある: "v//vn" 形式）
                bool hasTexcoord = false;
                uint32_t texIndex = 0;
                if (std::getline(v, indexStr, '/') && !indexStr.empty()) {
                    texIndex = ResolveIndex(std::stoll(indexStr), texcoords.size());
                    hasTexcoord = true;
                }

                // 法線番号（無い場合も一応ケア）
                bool hasNormal = false;
                uint32_t normIndex = 0;
                if (std::getline(v, indexStr, '/') && !indexStr.empty()) {
                    normIndex = ResolveIndex(std::stoll(indexStr), normals.size());
                    hasNormal = true;
                }

                // 範囲外インデックスへの保険（壊れたobjでもクラッシュさせない）
                assert(posIndex < positions.size());
                Vector4 position = posIndex < positions.size() ? positions[posIndex] : Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };
                Vector2 texcoord = (hasTexcoord && texIndex < texcoords.size()) ? texcoords[texIndex] : Vector2{ 0.0f, 0.0f };
                Vector3 normal = (hasNormal && normIndex < normals.size()) ? normals[normIndex] : Vector3{ 0.0f, 1.0f, 0.0f };

                triangle[faceVertex] = { position, texcoord, normal };
            }

            currentMesh->vertices.push_back(triangle[2]);
            currentMesh->vertices.push_back(triangle[1]);
            currentMesh->vertices.push_back(triangle[0]);
        }
    }

    // 面が1つも無い異常なobjへの保険
    if (modelData.meshes.empty()) {
        modelData.meshes.push_back(MeshData{});
    }

    // テクスチャが存在しないメッシュ（UVが無い/マテリアル未指定/mtllibが無い等）はuvCheckerで代用する
    for (auto& mesh : modelData.meshes) {
        MaterialData& mat = modelData.materials[mesh.materialName]; // 未登録ならデフォルト構築される
        if (mat.textureFilePath.empty()) {
            mat.textureFilePath = "resources/uvChecker.png";
        }
    }

    return modelData;
}

//==================================================
// assimpでモデルを読み込む関数
//==================================================
#ifdef USE_ASSIMP
namespace
{
Matrix4x4 ToEngineMatrix(const aiMatrix4x4& a)
{
    // assimpは列ベクトル規約なので転置してエンジン(行ベクトル規約)に合わせる
    Matrix4x4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a[j][i];
        }
    }
    return r;
}

Node ReadNode(const aiNode* node)
{
    Node result;
    result.localMatrix = ToEngineMatrix(node->mTransformation);
    result.name = node->mName.C_Str();
    result.children.resize(node->mNumChildren);
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        result.children[i] = ReadNode(node->mChildren[i]);
    }
    return result;
}

// ノードを全部たどって、骨の階層（親が先）に並べる
void FlattenJoints(const aiNode* node, int parent, SkinData& skin, std::unordered_map<std::string, int>& index)
{
    SkinJoint joint;
    joint.name = node->mName.C_Str();
    joint.parent = parent;
    aiVector3D scale, position;
    aiQuaternion rotation;
    node->mTransformation.Decompose(scale, rotation, position);
    joint.bindScale = { scale.x, scale.y, scale.z };
    joint.bindRot[0] = rotation.x;
    joint.bindRot[1] = rotation.y;
    joint.bindRot[2] = rotation.z;
    joint.bindRot[3] = rotation.w;
    joint.bindPos = { position.x, position.y, position.z };
    const int me = static_cast<int>(skin.joints.size());
    skin.joints.push_back(joint);
    index[joint.name] = me;
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        FlattenJoints(node->mChildren[i], me, skin, index);
    }
}

// アニメーションを読む（キーの時刻は、秒にそろえる）
void ReadClips(const aiScene* scene, SkinData& skin, const std::unordered_map<std::string, int>& index)
{
    for (unsigned a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* anim = scene->mAnimations[a];
        const float tps = anim->mTicksPerSecond > 0.0 ? static_cast<float>(anim->mTicksPerSecond) : 25.0f;
        SkinClip clip;
        clip.name = anim->mName.C_Str();
        clip.duration = static_cast<float>(anim->mDuration) / tps;
        clip.channels.resize(skin.joints.size());
        for (unsigned c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim* ch = anim->mChannels[c];
            auto it = index.find(ch->mNodeName.C_Str());
            if (it == index.end()) {
                continue;
            }
            SkinChannel& out = clip.channels[it->second];
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                const aiVectorKey& key = ch->mPositionKeys[k];
                out.position.push_back({ static_cast<float>(key.mTime) / tps, { key.mValue.x, key.mValue.y, key.mValue.z } });
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const aiQuatKey& key = ch->mRotationKeys[k];
                out.rotation.push_back({ static_cast<float>(key.mTime) / tps, { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w } });
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                const aiVectorKey& key = ch->mScalingKeys[k];
                out.scale.push_back({ static_cast<float>(key.mTime) / tps, { key.mValue.x, key.mValue.y, key.mValue.z } });
            }
        }
        skin.clips.push_back(std::move(clip));
    }
}

// 骨つきメッシュ：頂点ごとの骨の影響（上位4本）を求める
void ReadWeights(const aiMesh* mesh, const std::unordered_map<std::string, int>& index, SkinMesh& out, std::vector<VertexSkin>& perVertex)
{
    perVertex.assign(mesh->mNumVertices, VertexSkin{});
    std::vector<int> count(mesh->mNumVertices, 0);
    for (unsigned b = 0; b < mesh->mNumBones; ++b) {
        const aiBone* bone = mesh->mBones[b];
        auto it = index.find(bone->mName.C_Str());
        const int boneIndex = static_cast<int>(out.boneJoint.size());
        out.boneJoint.push_back(it != index.end() ? it->second : 0);
        out.boneOffset.push_back(ToEngineMatrix(bone->mOffsetMatrix));
        for (unsigned w = 0; w < bone->mNumWeights; ++w) {
            const unsigned v = bone->mWeights[w].mVertexId;
            const float weight = bone->mWeights[w].mWeight;
            VertexSkin& vs = perVertex[v];
            if (count[v] < 4) {
                vs.bone[count[v]] = static_cast<uint16_t>(boneIndex);
                vs.weight[count[v]] = weight;
                ++count[v];
            }
            else {
                int smallest = 0;
                for (int k = 1; k < 4; ++k) {
                    if (vs.weight[k] < vs.weight[smallest]) {
                        smallest = k;
                    }
                }
                if (weight > vs.weight[smallest]) {
                    vs.bone[smallest] = static_cast<uint16_t>(boneIndex);
                    vs.weight[smallest] = weight;
                }
            }
        }
    }
    // 合計を1にそろえる（影響する骨が無い頂点は、最初の骨に100%）
    for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
        VertexSkin& vs = perVertex[v];
        const float sum = vs.weight[0] + vs.weight[1] + vs.weight[2] + vs.weight[3];
        if (sum > 1e-6f) {
            for (int k = 0; k < 4; ++k) {
                vs.weight[k] /= sum;
            }
        }
        else {
            vs.bone[0] = 0;
            vs.weight[0] = 1.0f;
        }
    }
}

// ノードを辿り、メッシュに親からの累積変換を焼き込んで追加する
//   骨つきメッシュは、ノードの変換を焼き込まない（骨の動きで位置が決まる）。skin が nullptr なら骨は読まない
void BuildMeshes(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parent, ModelData& modelData,
    SkinData* skin, const std::unordered_map<std::string, int>& jointIndex)
{
    const aiMatrix4x4 world = parent * node->mTransformation;
    aiMatrix3x3 normalMatrix(world);
    normalMatrix.Inverse().Transpose();

    for (unsigned n = 0; n < node->mNumMeshes; ++n) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[n]];
        if (!mesh->HasNormals()) {
            continue;
        }
        MeshData meshData;
        meshData.materialName = "mat_" + std::to_string(mesh->mMaterialIndex);

        const bool skinned = skin && mesh->HasBones();
        SkinMesh skinMesh;
        std::vector<VertexSkin> perVertexSkin;
        if (skinned) {
            skinMesh.skinned = true;
            ReadWeights(mesh, jointIndex, skinMesh, perVertexSkin);
        }
        const aiMatrix4x4 meshWorld = skinned ? aiMatrix4x4() : world;
        aiMatrix3x3 meshNormalMatrix(meshWorld);
        meshNormalMatrix.Inverse().Transpose();

        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) {
                continue; // aiProcess_Triangulateで三角形のみのはず
            }
            for (unsigned e = 0; e < 3; ++e) {
                const unsigned idx = face.mIndices[e];
                aiVector3D p = meshWorld * mesh->mVertices[idx];
                aiVector3D nrm = meshNormalMatrix * mesh->mNormals[idx];
                nrm.Normalize();
                aiVector3D uv(0, 0, 0);
                if (mesh->HasTextureCoords(0)) {
                    uv = mesh->mTextureCoords[0][idx];
                }
                VertexData v{};
                // 右手系 -> 左手系: x反転 (+ aiProcess_FlipWindingOrder)
                v.position = { -p.x, p.y, p.z, 1.0f };
                v.normal = { -nrm.x, nrm.y, nrm.z };
                v.texcoord = { uv.x, uv.y };
                meshData.vertices.push_back(v);
                if (skinned) {
                    skinMesh.bindPosition.push_back({ p.x, p.y, p.z });
                    skinMesh.bindNormal.push_back({ nrm.x, nrm.y, nrm.z });
                    skinMesh.weights.push_back(perVertexSkin[idx]);
                }
            }
        }
        if (skin) {
            skinMesh.baseVertices = meshData.vertices; // 静止姿勢の結果は、あとで入れ直す
            skin->meshes.push_back(std::move(skinMesh));
        }
        modelData.meshes.push_back(std::move(meshData));
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        BuildMeshes(scene, node->mChildren[i], world, modelData, skin, jointIndex);
    }
}
}
#endif

bool LoadModelFile(const std::string& directoryPath, const std::string& fileName, ModelData& out)
{
#ifdef USE_ASSIMP
    const std::string filePath = directoryPath + "/" + fileName;
    // fileNameにサブフォルダが含まれる場合、テクスチャはそのフォルダ基準
    std::string baseDir = directoryPath;
    const size_t slash = fileName.find_last_of("/\\");
    if (slash != std::string::npos) {
        baseDir += "/" + fileName.substr(0, slash);
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath.c_str(),
        aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_LimitBoneWeights);
    if (!scene || !scene->HasMeshes()) {
        // 読み込めなかった理由をログに出す（モデルが出ないときの手がかり）
        Logger::Log("ModelLoader: failed to load " + filePath + " : " + importer.GetErrorString());
        return false;
    }

    ModelData modelData;

    // マテリアル解析（Diffuse / glTFのBaseColorテクスチャ）
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* material = scene->mMaterials[i];
        MaterialData data;
        aiString texPath;
        bool found = false;
        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            found = material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS;
        }
        if (!found && material->GetTextureCount(aiTextureType_BASE_COLOR) > 0) {
            found = material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS;
        }
        // 埋め込みテクスチャ("*0"形式)は未対応 -> 既定テクスチャを使う
        if (found && texPath.C_Str()[0] != '*') {
            // 読めるテクスチャだけ採用する（見つからない/WIC非対応形式で落ちるのを防ぐ）
            namespace fs = std::filesystem;
            std::error_code ec;
            const std::string raw = texPath.C_Str();
            const size_t s = raw.find_last_of("/\\");
            const std::string fileOnly = (s == std::string::npos) ? raw : raw.substr(s + 1);
            for (const std::string& candidate : { baseDir + "/" + raw, baseDir + "/" + fileOnly, directoryPath + "/" + fileOnly }) {
                std::string ext = candidate.substr(candidate.find_last_of('.') == std::string::npos ? candidate.size() : candidate.find_last_of('.'));
                for (char& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tif" && ext != ".tiff") {
                    continue;
                }
                if (fs::is_regular_file(fs::path(StringUtility::ConvertString(candidate)), ec)) {
                    data.textureFilePath = candidate;
                    break;
                }
            }
        }
        modelData.materials["mat_" + std::to_string(i)] = data;
    }

    modelData.rootNode = ReadNode(scene->mRootNode);

    // 骨（メッシュに骨が付いているモデルだけ）
    bool hasBones = false;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        hasBones = hasBones || scene->mMeshes[i]->HasBones();
    }
    std::shared_ptr<SkinData> skin;
    std::unordered_map<std::string, int> jointIndex;
    if (hasBones) {
        skin = std::make_shared<SkinData>();
        FlattenJoints(scene->mRootNode, -1, *skin, jointIndex);
        ReadClips(scene, *skin, jointIndex);
    }

    BuildMeshes(scene, scene->mRootNode, aiMatrix4x4(), modelData, skin.get(), jointIndex);
    if (modelData.meshes.empty()) {
        return false;
    }

    // 骨つきなら、静止姿勢（アニメーションなし）の頂点を求めて、普通の頂点データに入れておく
    //   （骨を動かさない描画・バウンディングボックスはこの形を使う）
    if (skin) {
        std::vector<Matrix4x4> globals;
        SkinMath::ComputeGlobals(*skin, nullptr, 0.0f, nullptr, 0.0f, 0.0f, globals);
        for (size_t m = 0; m < modelData.meshes.size() && m < skin->meshes.size(); ++m) {
            if (!skin->meshes[m].skinned) {
                continue;
            }
            SkinMath::SkinVertices(skin->meshes[m], globals, modelData.meshes[m].vertices.data());
            skin->meshes[m].baseVertices = modelData.meshes[m].vertices;
        }
        modelData.skin = skin;
    }
    out = std::move(modelData);
    return true;
#else
    (void)directoryPath; (void)fileName; (void)out;
    return false;
#endif
}

//==================================================
// 球を頂点計算で生成する関数（objファイルは使わない）
// kSubdivision: 緯度・経度それぞれの分割数
//==================================================
ModelData GenerateSphere(uint32_t kSubdivision)
{
    ModelData modelData;
    modelData.meshes.push_back(MeshData{});
    MeshData& mesh = modelData.meshes.back();
    mesh.materialName = "sphereMaterial";

    const float pi = 3.141592653589793f;
    const float kLonEvery = pi * 2.0f / float(kSubdivision); // 経度分割1つ分の角度
    const float kLatEvery = pi / float(kSubdivision);        // 緯度分割1つ分の角度

    mesh.vertices.resize(size_t(kSubdivision) * size_t(kSubdivision) * 6);

    // 緯度theta, 経度phiから球面上の1点を作る（w=1を忘れない）
    auto MakeVertex = [](float theta, float phi, float u, float v) {
        VertexData vertex{};
        float x = std::cosf(theta) * std::cosf(phi);
        float y = std::sinf(theta);
        float z = std::cosf(theta) * std::sinf(phi);
        vertex.position = { x, y, z, 1.0f };
        vertex.texcoord = { u, v };
        vertex.normal = { x, y, z }; // 原点中心の単位球なので位置=法線
        return vertex;
        };

    // 緯度の方向に分割 -pi/2 ～ pi/2
    for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex)
    {
        float lat = -pi / 2.0f + kLatEvery * float(latIndex); // theta

        // 経度の方向に分割
        for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex)
        {
            uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;
            float lon = float(lonIndex) * kLonEvery; // phi

            float u0 = float(lonIndex) / float(kSubdivision);
            float u1 = float(lonIndex + 1) / float(kSubdivision);
            float v0 = 1.0f - float(latIndex) / float(kSubdivision);
            float v1 = 1.0f - float(latIndex + 1) / float(kSubdivision);

            VertexData a = MakeVertex(lat, lon, u0, v0);
            VertexData b = MakeVertex(lat + kLatEvery, lon, u0, v1);
            VertexData c = MakeVertex(lat, lon + kLonEvery, u1, v0);
            VertexData d = MakeVertex(lat + kLatEvery, lon + kLonEvery, u1, v1);

            // 四角形を三角形2枚(abcとcbd)に分割
            mesh.vertices[start + 0] = a;
            mesh.vertices[start + 1] = b;
            mesh.vertices[start + 2] = c;

            mesh.vertices[start + 3] = c;
            mesh.vertices[start + 4] = b;
            mesh.vertices[start + 5] = d;
        }
    }

    modelData.materials[mesh.materialName].textureFilePath = "resources/uvChecker.png";
    return modelData;
}
}

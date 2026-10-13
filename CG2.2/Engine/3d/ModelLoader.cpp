#include "ModelLoader.h"

#include <cassert>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>

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

// ノードを辿り、メッシュに親からの累積変換を焼き込んで追加する
void BuildMeshes(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parent, ModelData& modelData)
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

        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) {
                continue; // aiProcess_Triangulateで三角形のみのはず
            }
            for (unsigned e = 0; e < 3; ++e) {
                const unsigned idx = face.mIndices[e];
                aiVector3D p = world * mesh->mVertices[idx];
                aiVector3D nrm = normalMatrix * mesh->mNormals[idx];
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
            }
        }
        modelData.meshes.push_back(std::move(meshData));
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        BuildMeshes(scene, node->mChildren[i], world, modelData);
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
        aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate | aiProcess_GenNormals);
    if (!scene || !scene->HasMeshes()) {
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
    BuildMeshes(scene, scene->mRootNode, aiMatrix4x4(), modelData);
    if (modelData.meshes.empty()) {
        return false;
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

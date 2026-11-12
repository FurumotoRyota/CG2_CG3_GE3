#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../math/Matrix4x4.h"
#include "../math/Vector2.h"
#include "../math/Vector4.h"
#include "CullingList.h"
#include "VertexData.h"

class Object3dCommon;
class ModelManager;

/// <summary>
/// 静的バッチ：動かない物を、素材ごと・場所（タイル）ごとに、大きなメッシュ1つにまとめて描く。
/// 描画の命令（ドローコール）が数千回から数十回に減るので、物が多いシーンのFPSが大きく上がる。
/// 画面外のかたまりは、更新も描画もしない（CullingList）。
///
/// 使い方:
///   StaticBatcher batch;
///   batch.Initialize(common, models, "world");          // 最初に1回（名前の頭は他と重ならないように）
///   // シーンを作り直すたびに:
///   batch.Clear();
///   batch.AddObject(someObject3d);                       // 動かない Object3d を足す（モデルの形をそのまま使う）
///   batch.AddPiece(piece);                               // 自分で頂点を渡して足すこともできる
///   batch.Build();                                       // まとめたメッシュを作る
///   // 毎フレーム:
///   batch.SetVisibleRect(rect);  batch.Update(view, proj);  batch.Draw();
///   // 影: batch.DrawShadow(cmd, cx, cz, range);
///
/// まとめる単位（これが違うものは別のかたまりになる）: タイル・グループ・影を落とすか・材質・色・テクスチャ。
/// グループ（建物など）はタイルで分けず1つにまとめ、SetGroupAlpha で半透明にできる。
/// まとめられないもの: 骨つきモデル、頂点を持たないモデル（AddObject は false を返す。そのまま自分で描くこと）
/// </summary>
class StaticBatcher
{
public:
    // まとめる部品1つぶん
    struct Piece
    {
        const std::vector<VertexData>* vertices = nullptr; // 元の頂点（モデルの座標）。Build が終わるまで生きていること
        std::string texture;                               // テクスチャのファイル
        Matrix4x4 world = Matrix4x4::MakeIdentity4x4();    // 置く場所（これで頂点をワールド座標に直す）
        float x = 0.0f;                                    // タイル分けに使う位置（地面の x,z）
        float z = 0.0f;
        int group = -1;                                    // 0以上なら、そのグループ（タイル分けしない）
        bool castShadow = true;
        bool exactColor = false;                           // true なら color をそのまま材質の色にする。false なら白（色は uv などで付ける）
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float roughness = 0.85f;                           // PBR
        float metallic = 0.0f;
        bool overrideUv = false;                           // true なら全頂点のUVを uv にする（パレット画像で色を付けるとき）
        Vector2 uv = { 0.0f, 0.0f };
    };

    float tileSize = 10.0f; // タイルの大きさ（ワールド）。小さいほど画面外を細かく省けるが、かたまりが増える
    bool usePbr = true;     // まとめたメッシュの材質をPBRにする

    void Initialize(Object3dCommon* common, ModelManager* models, const std::string& namePrefix);

    // 前回の結果を捨てる（作り直しの前に）。Object3d は次の Build で使い回す
    void Clear();

    void AddPiece(const Piece& piece);
    // Object3d の形・場所・材質をそのまま使って足す。まとめられなければ false
    bool AddObject(Object3d& obj, int group = -1);

    // 足したものから、大きなメッシュを作る
    void Build();

    void SetVisibleRect(const VisibleRect& rect) { rect_ = rect; }
    // 範囲の中のかたまりだけ更新する。更新した数を返す
    size_t Update(const Matrix4x4& view, const Matrix4x4& proj);
    void Draw() { list_.Draw(); }
    void DrawShadow(ID3D12GraphicsCommandList* commandList, float cx, float cz, float range) { list_.DrawShadow(commandList, cx, cz, range); }

    // グループの不透明度（1で不透明。1未満で半透明にして描く）。建物が自機を隠すときなどに
    void SetGroupAlpha(int group, float alpha);

    size_t ChunkCount() const { return list_.Size(); }

private:
    struct Key;
    struct Acc;

    Object3dCommon* common_ = nullptr;
    ModelManager* models_ = nullptr;
    std::string prefix_;
    int serial_ = 0;
    VisibleRect rect_;

    std::vector<Piece> pieces_;
    std::vector<std::unique_ptr<Object3d>> objects_;     // かたまりの実体（作り直しのとき使い回す）
    size_t used_ = 0;
    CullingList list_;
};

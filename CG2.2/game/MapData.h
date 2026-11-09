#pragma once
// 【部品】マップのデータと、ファイルへの保存・読み込み
//   エディタ（MapEditorScene）で作り、ゲーム（WesternShooterScene）で読む。どちらもこのクラスだけを見る。
//   地面は XZ 平面。マス (x, z) は、ワールドの x ∈ [x, x+1)、z ∈ [z, z+1) の正方形（原点は左下）。
//
//   置くもの:
//     マス      : 0=空き  1=壁  2=樽  3=建物（どれも通れない・弾が当たって消える。3は建物の範囲に自動でつく）
//     自機の開始位置 : 1か所
//     出現点（ドア）: 敵が出てくる場所。area=どの区切りに属するか、count=出す数、interval=出す間隔(秒)
//     区切り（ゲート）: 四角い範囲。area の敵を全員倒すまで通れない
//     建物      : 四角い範囲・種類（酒場・保安官事務所・雑貨店）・正面の向き。範囲のマスは通れない。正面のすぐ前に酒場のドア（出現点）を置く
//
//   ファイル形式（人が読める文字の形式。1行に1つ）:
//     WSMAP,1
//     size,横,縦
//     cells
//     0,0,1,...      ← 縦の行数ぶん。1行は横のマス数ぶん（z=0 の行が先頭）
//     end
//     player,x,z
//     spawn,x,z,area,count,interval
//     gate,x0,z0,x1,z1,area
//     building,x0,z0,x1,z1,kind,facing    ← kind: 0=酒場 1=保安官事務所 2=雑貨店 / facing: 0=手前(-z) 1=奥(+z) 2=左(-x) 3=右(+x)。
//                                           facing は省略してよい（0）。building の行そのものも無くてよい
#include <cstdint>
#include <string>
#include <vector>

struct MapData
{
    static constexpr int kMaxSize = 128;   // 縦横の最大
    static constexpr int kMaxSpawns = 64;
    static constexpr int kMaxGates = 32;
    static constexpr int kMaxBuildings = 32;

    enum Cell : std::uint8_t { Empty = 0, Wall = 1, Barrel = 2, Building = 3 };
    enum BuildingKind { Saloon = 0, Sheriff = 1, Store = 2, KindCount = 3 };
    // 建物の正面の向き（画面では 手前=下、奥=上、左、右）
    enum Facing { FaceSouth = 0, FaceNorth = 1, FaceWest = 2, FaceEast = 3, FacingCount = 4 };

    struct Spawn
    {
        int x = 0;
        int z = 0;
        int area = 0;
        int count = 5;
        float interval = 1.0f;
    };
    struct Gate
    {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0; // x0<=x1, z0<=z1（含む範囲）
        int area = 0;
    };

    struct BuildingRect
    {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0; // x0<=x1, z0<=z1（含む範囲）
        int kind = Saloon;
        int facing = FaceSouth;
    };

    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> cells;  // width * height（index = z * width + x）
    bool hasPlayerStart = false;
    int startX = 0;
    int startZ = 0;
    std::vector<Spawn> spawns;
    std::vector<Gate> gates;
    std::vector<BuildingRect> buildings;

    // 空のマップにする（範囲外の大きさは 1〜kMaxSize に丸める）
    void Reset(int w, int h);

    bool InRange(int x, int z) const { return x >= 0 && z >= 0 && x < width && z < height; }
    std::uint8_t Get(int x, int z) const { return InRange(x, z) ? cells[static_cast<size_t>(z) * width + x] : static_cast<std::uint8_t>(Wall); }
    void Set(int x, int z, std::uint8_t value) { if (InRange(x, z)) { cells[static_cast<size_t>(z) * width + x] = value; } }

    // 通れないマスか（範囲の外は壁として扱う）
    bool IsSolid(int x, int z) const { return Get(x, z) != Empty; }

    int CountCells(std::uint8_t kind) const;

    // ゲートの範囲を x0<=x1, z0<=z1 に直して、マップの内側に収める
    static Gate MakeGate(int xa, int za, int xb, int zb, int area);

    // 建物の範囲を x0<=x1, z0<=z1 に直す
    static BuildingRect MakeBuilding(int xa, int za, int xb, int zb, int kind, int facing = FaceSouth);

    // 建物の正面のすぐ前、真ん中のマス（酒場のドアを置く場所）
    static void FrontCell(const BuildingRect& b, int& x, int& z);

    // (x, z) が建物の正面の前（正面から2マス以内）にあるか
    static bool IsInFront(const BuildingRect& b, int x, int z);

    // 建物を置く。範囲がマップの内側で、壁・樽・他の建物・開始位置・出現点と重ならなければ true（範囲のマスを Building にする）。
    // 置けないときは false を返し、why に理由（日本語）を入れる
    bool PlaceBuilding(const BuildingRect& rect, std::string& why);

    // そのマスを含む建物の番号（無ければ -1）
    int BuildingAt(int x, int z) const;

    // そのマスを含む建物を消す（範囲のマスは空きに戻る）。消したら true
    bool RemoveBuildingAt(int x, int z);

    // 番号の建物を消す
    void RemoveBuilding(size_t index);

    // 保存・読み込み。失敗したら false を返し、error に理由（英語）を入れる
    bool Save(const std::string& path, std::string& error) const;
    bool Load(const std::string& path, std::string& error);

    // 作りかけの確認。問題があれば、その説明（日本語）を返す。空なら問題なし
    std::vector<std::string> Validate() const;
};

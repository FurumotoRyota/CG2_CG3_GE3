#pragma once
// 【部品】マップ(MapData)の上での「見える？」「歩ける？」「どう行く？」「どこに隠れる？」
//   敵のAIが使う、地面の2次元(x, z)の計算だけをまとめたもの。ゲームの状態は持たない（呼ぶたびに答えを返すだけ）。
//   座標は MapData と同じ：マス (x, z) は、ワールドの x ∈ [x, x+1)、z ∈ [z, z+1)。Vector2 は (x, z) を (x, y) に入れる。
//   通れないのは壁と樽（MapData::IsSolid）。マップの外も壁として扱う。
//   注意: 敵の半径より狭い通路（1マス幅など）は通れない。マップは通路を2マス以上にしておくこと。
#include <vector>

#include "MapData.h"
#include "math/Vector2.h"

namespace GridNav
{
    // a から b を見通せるか（中心から中心への線が、壁・樽のマスを通らなければ true）
    bool HasLineOfSight(const MapData& map, const Vector2& a, const Vector2& b);

    // 半径 radius の円が a から b まで、壁・樽にぶつからずに歩けるか
    bool CanWalk(const MapData& map, const Vector2& a, const Vector2& b, float radius);

    // 半径 radius の円を p に置いたとき、壁・樽にめり込むか
    bool TouchesSolid(const MapData& map, const Vector2& p, float radius);

    // from から to までの道。曲がり角の点を順に並べて返す（最後の点は to、to が壁の中ならいちばん近い空きマスの真ん中）。
    // 道が無ければ false（outPath は空）。マスごとの探索(A*)のあと、まっすぐ歩ける区間をつなげて角を減らす。
    bool FindPath(const MapData& map, const Vector2& from, const Vector2& to, float radius, std::vector<Vector2>& outPath);

    // 隠れ場所を探す問い合わせ
    struct CoverQuery
    {
        Vector2 from{ 0.0f, 0.0f };       // 探す人のいま位置（近いほど良い）
        Vector2 threat{ 0.0f, 0.0f };     // 隠れる相手の位置（ここから見えない場所が条件）
        float searchRadius = 8.0f;        // from からこの距離までを探す
        float minThreatDist = 4.0f;       // 相手に近すぎる場所は除く
        float maxThreatDist = 13.0f;      // 相手から遠すぎる場所は除く
        float radius = 0.6f;              // 立つ人の半径
        float approachWeight = 0.0f;      // 大きいほど「相手に近い場所」を好む（0なら自分に近い場所だけで選ぶ。隠れながら前へ詰めたいときに使う）
        float margin = 1.5f;              // 相手が左右にこれだけ動いても見えない場所を選ぶ（0なら今の位置からだけ）
    };

    // 相手(threat)から見えず、壁・樽のそばにある空きマスを探す。taken は他の人が使う場所（近い場所は避ける）。
    // 見つかれば true（out はそのマスの真ん中）。道が通じているかは確かめない（FindPath で確かめる）
    bool FindCover(const MapData& map, const CoverQuery& query, const std::vector<Vector2>& taken, Vector2& out);

    // 隠れ場所(cover)から短い距離で動いて、相手(threat)を見通せる場所を探す（陰から顔を出して撃つ場所）。
    // cover に近いものを選ぶ。見つかれば true
    bool FindPeek(const MapData& map, const Vector2& cover, const Vector2& threat, float radius, float maxMove, float maxThreatDist, Vector2& out);
}

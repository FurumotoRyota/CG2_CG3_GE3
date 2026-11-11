#include "GridNav.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

#include "math/Collision.h"

namespace {
    constexpr float kSkin = 0.02f;       // 押し出された直後の「ぴったり接している」状態を、ぶつかりとは見なさない余裕
    constexpr float kWalkStep = 0.2f;    // CanWalk で円を置いて調べる間隔
    constexpr float kInf = 1.0e30f;

    int FloorToInt(float v) { return static_cast<int>(std::floor(v)); }

    float Dist(const Vector2& a, const Vector2& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    Vector2 CellCenter(int x, int z) { return { x + 0.5f, z + 0.5f }; }

    // まわり8マスのどこかが壁・樽か（壁ぎわを歩くと半径ぶんで引っかかるので、探索でこのマスを少し避ける）
    bool NearSolid(const MapData& map, int x, int z)
    {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx != 0 || dz != 0) && map.IsSolid(x + dx, z + dz)) {
                    return true;
                }
            }
        }
        return false;
    }

    // 斜めに進むときは、となり合う2マスも空いていること（角をかすめて抜けない）
    bool CanStep(const MapData& map, int x, int z, int dx, int dz)
    {
        if (map.IsSolid(x + dx, z + dz)) {
            return false;
        }
        if (dx != 0 && dz != 0) {
            return !map.IsSolid(x + dx, z) && !map.IsSolid(x, z + dz);
        }
        return true;
    }

    // 円が壁・樽にめり込んでいたら外へ押し出した位置を返す（マスの真ん中だと、狭い通路で壁に触れるため）
    Vector2 NudgeFromSolids(const MapData& map, Vector2 p, float radius)
    {
        for (int iter = 0; iter < 3; ++iter) {
            const int x0 = FloorToInt(p.x - radius);
            const int x1 = FloorToInt(p.x + radius);
            const int z0 = FloorToInt(p.y - radius);
            const int z1 = FloorToInt(p.y + radius);
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    if (map.IsSolid(x, z)) {
                        const Collision::Rect rect = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(x + 1), static_cast<float>(z + 1) };
                        p = Collision::PushCircleOutOfRect(p, radius, rect);
                    }
                }
            }
        }
        return p;
    }
}

namespace GridNav
{
    bool HasLineOfSight(const MapData& map, const Vector2& a, const Vector2& b)
    {
        // マスを1つずつたどる（DDA）。通ったマスに壁・樽があれば見えない
        int x = FloorToInt(a.x);
        int z = FloorToInt(a.y);
        const int ex = FloorToInt(b.x);
        const int ez = FloorToInt(b.y);
        const float dx = b.x - a.x;
        const float dz = b.y - a.y;
        const int stepX = dx > 0.0f ? 1 : -1;
        const int stepZ = dz > 0.0f ? 1 : -1;
        const float tDeltaX = dx != 0.0f ? std::fabs(1.0f / dx) : kInf;
        const float tDeltaZ = dz != 0.0f ? std::fabs(1.0f / dz) : kInf;
        float tMaxX = dx > 0.0f ? (x + 1 - a.x) / dx : (dx < 0.0f ? (a.x - x) / -dx : kInf);
        float tMaxZ = dz > 0.0f ? (z + 1 - a.y) / dz : (dz < 0.0f ? (a.y - z) / -dz : kInf);

        const int maxSteps = std::abs(ex - x) + std::abs(ez - z) + 1;
        for (int i = 0; i <= maxSteps; ++i) {
            if (map.IsSolid(x, z)) {
                return false;
            }
            if (x == ex && z == ez) {
                return true;
            }
            if (tMaxX < tMaxZ) {
                x += stepX;
                tMaxX += tDeltaX;
            }
            else {
                z += stepZ;
                tMaxZ += tDeltaZ;
            }
        }
        return true;
    }

    bool TouchesSolid(const MapData& map, const Vector2& p, float radius)
    {
        const float r = (std::max)(0.0f, radius - kSkin);
        const int x0 = FloorToInt(p.x - r);
        const int x1 = FloorToInt(p.x + r);
        const int z0 = FloorToInt(p.y - r);
        const int z1 = FloorToInt(p.y + r);
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (!map.IsSolid(x, z)) {
                    continue;
                }
                const Collision::Rect rect = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(x + 1), static_cast<float>(z + 1) };
                if (Collision::CircleRect(p, r, rect)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool CanWalk(const MapData& map, const Vector2& a, const Vector2& b, float radius)
    {
        const float len = Dist(a, b);
        const int steps = (std::max)(1, static_cast<int>(std::ceil(len / kWalkStep)));
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const Vector2 p = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
            if (TouchesSolid(map, p, radius)) {
                return false;
            }
        }
        return true;
    }

    bool FindPath(const MapData& map, const Vector2& from, const Vector2& to, float radius, std::vector<Vector2>& outPath)
    {
        outPath.clear();
        if (map.width <= 0 || map.height <= 0) {
            return false;
        }
        const int W = map.width;
        const int H = map.height;
        auto clampX = [&](int v) { return (std::min)((std::max)(v, 0), W - 1); };
        auto clampZ = [&](int v) { return (std::min)((std::max)(v, 0), H - 1); };

        const int sx = clampX(FloorToInt(from.x));
        const int sz = clampZ(FloorToInt(from.y));
        int gx = clampX(FloorToInt(to.x));
        int gz = clampZ(FloorToInt(to.y));
        Vector2 goalPos = to;

        // 目的地が壁の中なら、いちばん近い空きマスに変える（まわり4マスまで探す）
        if (map.IsSolid(gx, gz)) {
            float best = kInf;
            int bx = -1;
            int bz = -1;
            for (int z = gz - 4; z <= gz + 4; ++z) {
                for (int x = gx - 4; x <= gx + 4; ++x) {
                    if (map.IsSolid(x, z)) {
                        continue;
                    }
                    const float d = Dist(CellCenter(x, z), to);
                    if (d < best) {
                        best = d;
                        bx = x;
                        bz = z;
                    }
                }
            }
            if (bx < 0) {
                return false;
            }
            gx = bx;
            gz = bz;
            goalPos = CellCenter(gx, gz);
        }

        // まっすぐ歩けるなら探すまでもない
        if (CanWalk(map, from, goalPos, radius)) {
            outPath.push_back(goalPos);
            return true;
        }

        // A*（8方向）。コストは直進1、斜め√2。見積もりは斜めを含む距離
        const size_t cellCount = static_cast<size_t>(W) * H;
        // 作業用の配列は使い回す（毎回マップ全体ぶんを作って埋めると、敵が多いときに重い）。
        //   stamp[i] が今回の番号と同じマスだけ「今回の探索で触れた」ことにして、埋め直しを省く
        static std::vector<float> gBuf;
        static std::vector<int> parentBuf;
        static std::vector<char> closedBuf;
        static std::vector<unsigned> stampBuf;
        static unsigned searchId = 0;
        if (stampBuf.size() != cellCount) {
            gBuf.assign(cellCount, kInf);
            parentBuf.assign(cellCount, -1);
            closedBuf.assign(cellCount, 0);
            stampBuf.assign(cellCount, 0);
            searchId = 0;
        }
        ++searchId;
        auto touch = [&](int i) {
            if (stampBuf[i] != searchId) {
                stampBuf[i] = searchId;
                gBuf[i] = kInf;
                parentBuf[i] = -1;
                closedBuf[i] = 0;
            }
        };
        int expanded = 0; // 取り出したマスの数（道が無いときに、マップ全体を調べ続けないための上限）
        using Node = std::pair<float, int>; // (見積もり合計, マス番号)
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

        auto heuristic = [&](int x, int z) {
            const float ddx = static_cast<float>(std::abs(x - gx));
            const float ddz = static_cast<float>(std::abs(z - gz));
            return (ddx + ddz) + (1.41421356f - 2.0f) * (std::min)(ddx, ddz);
        };

        const int startIndex = sz * W + sx;
        const int goalIndex = gz * W + gx;
        touch(startIndex);
        gBuf[startIndex] = 0.0f;
        open.push({ heuristic(sx, sz), startIndex });

        static const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        static const int kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

        bool found = false;
        while (!open.empty()) {
            const int cur = open.top().second;
            open.pop();
            if (closedBuf[cur]) { // cur は取り出したマス（touch 済み）
                continue;
            }
            closedBuf[cur] = 1;
            if (++expanded > 6000) {
                break; // 遠すぎる・道が無い：あきらめる（見つからなかった扱い）
            }
            if (cur == goalIndex) {
                found = true;
                break;
            }
            const int cx = cur % W;
            const int cz = cur / W;
            for (int k = 0; k < 8; ++k) {
                const int nx = cx + kDx[k];
                const int nz = cz + kDz[k];
                if (!map.InRange(nx, nz) || !CanStep(map, cx, cz, kDx[k], kDz[k])) {
                    continue;
                }
                const int ni = nz * W + nx;
                touch(ni);
                if (closedBuf[ni]) {
                    continue;
                }
                const float cost = (kDx[k] != 0 && kDz[k] != 0) ? 1.41421356f : 1.0f;
                const float ng = gBuf[cur] + cost + (NearSolid(map, nx, nz) ? 1.0f : 0.0f);
                if (ng < gBuf[ni]) {
                    gBuf[ni] = ng;
                    parentBuf[ni] = cur;
                    open.push({ ng + heuristic(nx, nz), ni });
                }
            }
        }
        if (!found) {
            return false;
        }

        // ゴールからさかのぼって、マスの真ん中を順に並べる（最後は goalPos）
        std::vector<Vector2> raw;
        for (int i = goalIndex; i != -1 && i != startIndex; i = parentBuf[i]) {
            raw.push_back(CellCenter(i % W, i / W));
        }
        std::reverse(raw.begin(), raw.end());
        if (raw.empty()) {
            raw.push_back(goalPos);
        }
        else {
            raw.back() = goalPos;
        }
        for (size_t i = 0; i + 1 < raw.size(); ++i) {
            raw[i] = NudgeFromSolids(map, raw[i], radius);
        }

        // まっすぐ歩ける限り先の点へとばして、曲がり角だけを残す
        Vector2 anchor = from;
        size_t idx = 0;
        while (idx < raw.size()) {
            size_t pick = idx;
            for (size_t j = raw.size(); j-- > idx;) {
                if (CanWalk(map, anchor, raw[j], radius)) {
                    pick = j;
                    break;
                }
            }
            outPath.push_back(raw[pick]);
            anchor = raw[pick];
            idx = pick + 1;
        }
        return true;
    }

    bool FindCoverWithMargin(const MapData& map, const CoverQuery& q, float margin, const std::vector<Vector2>& taken, Vector2& out)
    {
        const int x0 = FloorToInt(q.from.x - q.searchRadius);
        const int x1 = FloorToInt(q.from.x + q.searchRadius);
        const int z0 = FloorToInt(q.from.y - q.searchRadius);
        const int z1 = FloorToInt(q.from.y + q.searchRadius);

        bool found = false;
        float bestScore = kInf;
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (map.IsSolid(x, z)) {
                    continue;
                }
                const Vector2 c = CellCenter(x, z);
                const float fromDist = Dist(q.from, c);
                const float threatDist = Dist(q.threat, c);
                if (fromDist > q.searchRadius || threatDist < q.minThreatDist || threatDist > q.maxThreatDist) {
                    continue;
                }
                if (TouchesSolid(map, c, q.radius) || HasLineOfSight(map, q.threat, c)) {
                    continue; // 狭すぎる場所・相手から見える場所は隠れ場所にならない
                }
                if (margin > 0.0f) {
                    // 相手が左右に動いても見えないか（相手→この場所の向きに垂直な方向へ、margin ずらして確かめる）
                    const float dx = c.x - q.threat.x;
                    const float dz = c.y - q.threat.y;
                    const float len = std::sqrt(dx * dx + dz * dz);
                    if (len > 0.0001f) {
                        const Vector2 side = { -dz / len * margin, dx / len * margin };
                        const Vector2 p1 = { q.threat.x + side.x, q.threat.y + side.y };
                        const Vector2 p2 = { q.threat.x - side.x, q.threat.y - side.y };
                        if (HasLineOfSight(map, p1, c) || HasLineOfSight(map, p2, c)) {
                            continue;
                        }
                    }
                }
                bool used = false;
                for (const Vector2& t : taken) {
                    if (Dist(t, c) < 1.6f) {
                        used = true;
                        break;
                    }
                }
                if (used) {
                    continue;
                }
                // 壁・樽のそばを好む（まわり8マスに壁があるほど良い）。近いほど良い
                int walls = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if ((dx != 0 || dz != 0) && map.IsSolid(x + dx, z + dz)) {
                            ++walls;
                        }
                    }
                }
                const float score = fromDist + q.approachWeight * threatDist + (walls == 0 ? 3.0f : 0.0f) - 0.3f * static_cast<float>((std::min)(walls, 3));
                if (score < bestScore) {
                    bestScore = score;
                    out = c;
                    found = true;
                }
            }
        }
        return found;
    }

    bool FindCover(const MapData& map, const CoverQuery& q, const std::vector<Vector2>& taken, Vector2& out)
    {
        // まず「相手が少し動いても見えない」場所を探し、無ければ今の位置から見えなければよいことにする
        if (q.margin > 0.0f && FindCoverWithMargin(map, q, q.margin, taken, out)) {
            return true;
        }
        return FindCoverWithMargin(map, q, 0.0f, taken, out);
    }

    bool FindPeek(const MapData& map, const Vector2& cover, const Vector2& threat, float radius, float maxMove, float maxThreatDist, Vector2& out)
    {
        const int x0 = FloorToInt(cover.x - maxMove);
        const int x1 = FloorToInt(cover.x + maxMove);
        const int z0 = FloorToInt(cover.y - maxMove);
        const int z1 = FloorToInt(cover.y + maxMove);

        bool found = false;
        float best = kInf;
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (map.IsSolid(x, z)) {
                    continue;
                }
                const Vector2 c = CellCenter(x, z);
                const float move = Dist(cover, c);
                if (move > maxMove || move >= best || Dist(threat, c) > maxThreatDist) {
                    continue;
                }
                if (TouchesSolid(map, c, radius) || !HasLineOfSight(map, threat, c) || !CanWalk(map, cover, c, radius)) {
                    continue;
                }
                best = move;
                out = c;
                found = true;
            }
        }
        return found;
    }
}

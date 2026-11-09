#include "WireActionGame.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace wa
{
    namespace
    {
        constexpr float kDt = 1.0f / 60.0f;
        constexpr float kBlank = 0.001f;
        constexpr float kPi = 3.14159265f;

        float Clamp(float v, float lo, float hi) { return (std::max)(lo, (std::min)(v, hi)); }
        float SmoothStep01(float u)
        {
            u = Clamp(u, 0.0f, 1.0f);
            return u * u * (3.0f - 2.0f * u);
        }
    }

    float Game::Rand01()
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>((rng_ >> 8) & 0xFFFFFF) / 16777216.0f;
    }

    //==================================================
    // 座標・マップ
    //==================================================
    IndexSet Game::ToIndex(Vec2 p) const
    {
        const int ix = static_cast<int>(std::floor(p.x + 0.5f));
        const int iy = kH - 1 - static_cast<int>(std::floor(p.y + 0.5f));
        return { ix, iy };
    }

    Rect Game::CellRect(int ix, int iy) const
    {
        const Vec2 c = CellCenter(ix, iy);
        return { c.x - 0.5f, c.x + 0.5f, c.y + 0.5f, c.y - 0.5f };
    }

    // 左右の外は壁、上下の外は空
    bool Game::IsBlockIdx(int ix, int iy) const
    {
        if (ix < 0 || ix >= kW) {
            return true;
        }
        if (iy < 0 || iy >= kH) {
            return false;
        }
        return map_[static_cast<size_t>(iy) * kW + ix] == 1;
    }

    bool Game::IsBlockAt(Vec2 p) const
    {
        const IndexSet i = ToIndex(p);
        return IsBlockIdx(i.x, i.y);
    }

    // 線分上を0.2刻みで調べる。終点側 ignoreEnd 分は無視する（掛け先自身で遮られないため）
    bool Game::SegmentBlocked(Vec2 a, Vec2 b, float ignoreEnd) const
    {
        const Vec2 d = b - a;
        const float len = Length(d);
        if (len < 1e-4f) {
            return false;
        }
        const Vec2 dir = d / len;
        for (float t = 0.0f; t < len - ignoreEnd; t += 0.2f) {
            if (IsBlockAt(a + dir * t)) {
                return true;
            }
        }
        return false;
    }

    void Game::SetBlock(int ix, int worldY, bool solid)
    {
        const int iy = kH - 1 - worldY;
        if (ix < 0 || ix >= kW || iy < 0 || iy >= kH) {
            return;
        }
        map_[static_cast<size_t>(iy) * kW + ix] = solid ? 1 : 0;
    }

    Vec2 Game::ScreenToWorld(float sx, float sy) const
    {
        return { cam.x + (sx - screenW * 0.5f) / ppu, cam.y - (sy - screenH * 0.5f) / ppu };
    }

    Vec2 Game::WorldToScreen(Vec2 w) const
    {
        return { (w.x - cam.x) * ppu + screenW * 0.5f, screenH * 0.5f - (w.y - cam.y) * ppu };
    }

    void Game::SetView(float newPpu, float w, float h)
    {
        ppu = newPpu;
        screenW = w;
        screenH = h;
    }

    //==================================================
    // ステージ
    //==================================================
    void Game::LoadDefaultStage()
    {
        std::fill(map_.begin(), map_.end(), static_cast<uint8_t>(0));
        enemySpawns_.clear();

        // 地面（下2段）。穴は落下死
        const int pits[][2] = { { 38, 46 }, { 78, 90 }, { 128, 140 }, { 165, 172 } };
        for (int x = 0; x < kW; ++x) {
            bool pit = false;
            for (const auto& p : pits) {
                if (x >= p[0] && x <= p[1]) {
                    pit = true;
                }
            }
            if (!pit) {
                SetBlock(x, 0, true);
                SetBlock(x, 1, true);
            }
        }
        // 浮いている足場
        auto platform = [&](int x0, int x1, int y) {
            for (int x = x0; x <= x1; ++x) {
                SetBlock(x, y, true);
            }
        };
        platform(18, 23, 5);
        platform(28, 31, 8);
        platform(60, 66, 4);
        platform(68, 70, 7);
        platform(100, 106, 5);
        platform(110, 113, 9);
        // 天井（ワイヤーを掛ける場所）
        platform(36, 48, 13);
        platform(36, 48, 14);
        platform(76, 92, 13);
        platform(76, 92, 14);
        platform(126, 142, 13);
        platform(126, 142, 14);
        platform(145, 156, 13);
        platform(145, 156, 14);
        platform(163, 174, 13);
        platform(163, 174, 14);
        // 壁
        for (int y = 2; y <= 8; ++y) {
            SetBlock(150, y, true);
            SetBlock(151, y, true);
        }

        spawn = { 3.0f, 1.9f };
        // 敵（地面の上は y=2.0、足場の上は足場の高さ+1）
        const float groundY = 2.0f;
        enemySpawns_.push_back({ { 14.0f, groundY }, false });
        enemySpawns_.push_back({ { 20.5f, 6.0f }, false });
        enemySpawns_.push_back({ { 26.0f, groundY }, false });
        enemySpawns_.push_back({ { 52.0f, groundY }, false });
        enemySpawns_.push_back({ { 63.0f, 5.0f }, false });
        enemySpawns_.push_back({ { 72.0f, groundY }, true });
        enemySpawns_.push_back({ { 95.0f, groundY }, true });
        enemySpawns_.push_back({ { 103.0f, 6.0f }, false });
        enemySpawns_.push_back({ { 120.0f, groundY }, true });
        enemySpawns_.push_back({ { 148.0f, groundY }, false });
        enemySpawns_.push_back({ { 158.0f, groundY }, true });
        enemySpawns_.push_back({ { 180.0f, groundY }, false });

        hasGoal = true;
        goal = { 192.0f, 2.5f };
        Reset();
    }

    bool Game::LoadStageCsv(const std::string& path)
    {
        std::ifstream file(path);
        if (!file) {
            return false;
        }
        std::vector<uint8_t> newMap(kW * kH, 0);
        std::vector<EnemySpawn> newSpawns;
        Vec2 newSpawn = spawn;
        bool newHasGoal = false;
        Vec2 newGoal;
        bool any = false;

        std::string line;
        int row = 0;
        while (std::getline(file, line) && row < kH) {
            std::stringstream ss(line);
            std::string tok;
            int col = 0;
            while (std::getline(ss, tok, ',') && col < kW) {
                // 前後の空白・改行を取る
                const size_t b = tok.find_first_not_of(" \t\r\n");
                const size_t e = tok.find_last_not_of(" \t\r\n");
                tok = (b == std::string::npos) ? "" : tok.substr(b, e - b + 1);
                const Vec2 c = CellCenter(col, row);
                if (tok == "1") {
                    newMap[static_cast<size_t>(row) * kW + col] = 1;
                }
                else if (tok == "P") {
                    newSpawn = { c.x, c.y - 0.1f };
                }
                else if (tok == "E") {
                    newSpawns.push_back({ c, false });
                }
                else if (tok == "S") {
                    newSpawns.push_back({ c, true });
                }
                else if (tok == "G") {
                    newHasGoal = true;
                    newGoal = c;
                }
                any = true;
                ++col;
            }
            ++row;
        }
        if (!any) {
            return false;
        }
        map_ = newMap;
        enemySpawns_ = newSpawns;
        spawn = newSpawn;
        hasGoal = newHasGoal;
        goal = newGoal;
        Reset();
        return true;
    }

    bool Game::SaveStageCsv(const std::string& path) const
    {
        std::vector<std::string> grid(static_cast<size_t>(kW) * kH, "0");
        for (int i = 0; i < kW * kH; ++i) {
            if (map_[i] == 1) {
                grid[i] = "1";
            }
        }
        auto put = [&](Vec2 p, const char* tok) {
            const IndexSet i = ToIndex(p);
            if (i.x >= 0 && i.x < kW && i.y >= 0 && i.y < kH) {
                grid[static_cast<size_t>(i.y) * kW + i.x] = tok;
            }
        };
        put({ spawn.x, spawn.y }, "P");
        for (const EnemySpawn& s : enemySpawns_) {
            put(s.pos, s.shield ? "S" : "E");
        }
        if (hasGoal) {
            put(goal, "G");
        }
        std::ofstream file(path);
        if (!file) {
            return false;
        }
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                file << grid[static_cast<size_t>(y) * kW + x] << (x + 1 < kW ? "," : "");
            }
            file << "\n";
        }
        return true;
    }

    void Game::Reset()
    {
        player = Player{};
        player.pos = spawn;
        wire = WireRuntime{};
        enemies.clear();
        particles.clear();
        nextEnemyId_ = 1;
        for (const EnemySpawn& s : enemySpawns_) {
            Enemy e;
            e.id = nextEnemyId_++;
            e.pos = s.pos;
            e.homeX = s.pos.x;
            e.shield = s.shield;
            e.dir = -1;
            enemies.push_back(e);
        }
        phase = Phase::Play;
        hitStop = 0;
        killZoom = 0.0f;
        killCount = 0;
        time = 0.0f;
        cam = player.pos;
        ApplyCameraBounds();
    }

    const Enemy* Game::FindEnemy(int id) const
    {
        for (const Enemy& e : enemies) {
            if (e.id == id && !e.dead) {
                return &e;
            }
        }
        return nullptr;
    }

    Enemy* Game::FindEnemyMut(int id)
    {
        for (Enemy& e : enemies) {
            if (e.id == id && !e.dead) {
                return &e;
            }
        }
        return nullptr;
    }

    //==================================================
    // 衝突（軸ごとに分けて判定。動く前と別のマスに入ったときだけ衝突とする）
    //==================================================
    void Game::CheckMapCollision(Vec2 pos, Collision& c) const
    {
        const float hw = kPlayerW * 0.5f;
        const float hh = kPlayerH * 0.5f;

        // ---- X ----
        if (c.move.x > 0.0f) {
            const float ys[2] = { pos.y + hh - kBlank, pos.y - hh + kBlank };
            for (float y : ys) {
                const IndexSet n = ToIndex({ pos.x + c.move.x + hw, y });
                if (!IsBlockIdx(n.x, n.y)) {
                    continue;
                }
                const IndexSet b = ToIndex({ pos.x + hw - kBlank, y });
                if (b.x != n.x) {
                    const float allowed = CellRect(n.x, n.y).left - (pos.x + hw) - kBlank;
                    c.move.x = (std::min)(c.move.x, (std::max)(0.0f, allowed));
                    c.wall = true;
                    break;
                }
            }
        }
        else if (c.move.x < 0.0f) {
            const float ys[2] = { pos.y + hh - kBlank, pos.y - hh + kBlank };
            for (float y : ys) {
                const IndexSet n = ToIndex({ pos.x + c.move.x - hw, y });
                if (!IsBlockIdx(n.x, n.y)) {
                    continue;
                }
                const IndexSet b = ToIndex({ pos.x - hw + kBlank, y });
                if (b.x != n.x) {
                    const float allowed = CellRect(n.x, n.y).right - (pos.x - hw) + kBlank;
                    c.move.x = (std::max)(c.move.x, (std::min)(0.0f, allowed));
                    c.wall = true;
                    break;
                }
            }
        }

        // ---- Y（X を適用した位置で）----
        const float px = pos.x + c.move.x;
        const float xs[2] = { px - hw + kBlank, px + hw - kBlank };
        if (c.move.y > 0.0f) {
            for (float x : xs) {
                const IndexSet n = ToIndex({ x, pos.y + c.move.y + hh });
                if (!IsBlockIdx(n.x, n.y)) {
                    continue;
                }
                const IndexSet b = ToIndex({ x, pos.y + hh - kBlank });
                if (b.y != n.y) {
                    const float allowed = CellRect(n.x, n.y).bottom - (pos.y + hh) - kBlank;
                    c.move.y = (std::min)(c.move.y, (std::max)(0.0f, allowed));
                    c.top = true;
                    break;
                }
            }
        }
        else if (c.move.y < 0.0f) {
            for (float x : xs) {
                const IndexSet n = ToIndex({ x, pos.y + c.move.y - hh });
                if (!IsBlockIdx(n.x, n.y)) {
                    continue;
                }
                const IndexSet b = ToIndex({ x, pos.y - hh + kBlank });
                if (b.y != n.y) {
                    const float allowed = CellRect(n.x, n.y).top - (pos.y - hh) + kBlank;
                    c.move.y = (std::max)(c.move.y, (std::min)(0.0f, allowed));
                    c.ground = true;
                    break;
                }
            }
        }
    }

    // 速度が大きいときは小さく分けて判定する（すり抜け対策）。塞がれた軸は残りのステップで動かさない
    Game::Collision Game::MoveWithCollision(Vec2 v)
    {
        Collision total;
        const Vec2 start = player.pos;
        const int n = (std::max)(1, static_cast<int>(std::ceil(Length(v) / 0.45f)));
        const Vec2 step = v / static_cast<float>(n);
        bool blockedX = false;
        bool blockedY = false;
        for (int i = 0; i < n; ++i) {
            Collision c;
            c.move = step;
            if (blockedX) {
                c.move.x = 0.0f;
            }
            if (blockedY) {
                c.move.y = 0.0f;
            }
            CheckMapCollision(player.pos, c);
            player.pos = player.pos + c.move;
            if (c.wall) {
                blockedX = true;
                total.wall = true;
            }
            if (c.ground) {
                blockedY = true;
                total.ground = true;
            }
            if (c.top) {
                blockedY = true;
                total.top = true;
            }
        }
        total.move = player.pos - start;
        return total;
    }

    bool Game::ProbeGround() const
    {
        const float hw = kPlayerW * 0.5f - kBlank;
        const float y = player.pos.y - kPlayerH * 0.5f - 0.01f;
        return IsBlockAt({ player.pos.x - hw, y }) || IsBlockAt({ player.pos.x + hw, y });
    }

    // 通常の移動の後始末（壁・天井・地面に当たった分の速度を消す）
    void Game::ApplyMoveResult(const Collision& c)
    {
        Player& p = player;
        if (c.wall) {
            p.vel.x = 0.0f;
        }
        if (c.top && p.vel.y > 0.0f) {
            p.vel.y = 0.0f;
        }
        if (c.ground) {
            if (p.vel.y < 0.0f) {
                p.vel.y = 0.0f;
            }
            p.onGround = true;
            p.jumpArc = false;
        }
        else {
            p.onGround = (p.vel.y <= 0.0f) && ProbeGround();
        }
        // マップの左右の端
        const float minX = kPlayerW * 0.5f - 0.5f;
        const float maxX = kW - 0.5f - kPlayerW * 0.5f;
        if (p.pos.x < minX) {
            p.pos.x = minX;
            p.vel.x = 0.0f;
        }
        if (p.pos.x > maxX) {
            p.pos.x = maxX;
            p.vel.x = 0.0f;
        }
    }

    //==================================================
    // 更新
    //==================================================
    void Game::Update(const InputState& in)
    {
        time += kDt;
        if (phase == Phase::Play) {
            if (hitStop > 0) {
                --hitStop; // ヒットストップ中は世界を止める（火花だけ進める）
                UpdateParticles();
                return;
            }
            UpdateAim(in);
            UpdatePlayer(in);
            UpdateEnemies();
            UpdateCamera();
            CheckPlayerEnemyContact();

            if (phase == Phase::Play) {
                if (player.pos.y < kFallDeathY) {
                    player.hp = 0;
                    phase = Phase::Death; // 落下死は爆発なし
                }
                else if (hasGoal) {
                    if (std::fabs(player.pos.x - goal.x) < 0.9f && std::fabs(player.pos.y - goal.y) < 1.1f) {
                        phase = Phase::Clear;
                    }
                }
                else {
                    bool anyAlive = false;
                    for (const Enemy& e : enemies) {
                        if (!e.dead && !e.dying) {
                            anyAlive = true;
                        }
                    }
                    if (!anyAlive) {
                        phase = Phase::Clear;
                    }
                }
            }
        }
        if (killZoom > 0.0f) {
            killZoom += kDt;
            if (killZoom > 0.3f) {
                killZoom = 0.0f;
            }
        }
        UpdateParticles();
    }

    //==================================================
    // 照準・候補
    //==================================================
    void Game::UpdateAim(const InputState& in)
    {
        const WireParams& wp = settings.wire;
        wire.hold = in.fireHeld;
        if (in.firePressed) {
            wire.fireBuffer = wp.fireBufferTime;
        }
        else {
            wire.fireBuffer = (std::max)(0.0f, wire.fireBuffer - kDt);
        }
        if (wire.state != WireState::None) {
            return;
        }
        if (!wireEnabled) {
            wire.fireBuffer = 0.0f;
            wire.cand = Candidate{};
            return;
        }

        Candidate cand;
        const Vec2 pp = player.pos;
        const Vec2 aim = in.aim;

        // ① 敵：カーソルに一番近い敵（吸着半径内・射程内・間に障害物なし）
        float best = wp.lockRadius;
        for (const Enemy& e : enemies) {
            if (e.dead || e.dying) {
                continue;
            }
            if (Length(e.pos - pp) > wp.range) {
                continue;
            }
            const float d = Length(e.pos - aim);
            if (d >= best) {
                continue;
            }
            if (SegmentBlocked(pp, e.pos, 0.6f)) {
                continue;
            }
            best = d;
            cand.type = CandType::Enemy;
            cand.pos = e.pos;
            cand.enemyId = e.id;
        }
        if (cand.type == CandType::Enemy) {
            wire.cand = cand;
            return;
        }

        // ② カーソル方向へ歩いて最初のブロック
        Vec2 dir = Normalize(aim - pp);
        if (Length(dir) < 0.5f) {
            dir = { static_cast<float>(player.lr), 0.0f };
        }
        const float startT = (std::max)(0.6f, wp.minRange);
        Vec2 prev = pp + dir * startT;
        for (float t = startT; t <= wp.range; t += 0.1f) {
            const Vec2 p = pp + dir * t;
            if (IsBlockAt(p)) {
                // 境界を二分探索して、手前の面に掛ける
                Vec2 lo = prev;
                Vec2 hi = p;
                for (int i = 0; i < 6; ++i) {
                    const Vec2 mid = (lo + hi) * 0.5f;
                    if (IsBlockAt(mid)) {
                        hi = mid;
                    }
                    else {
                        lo = mid;
                    }
                }
                cand.type = CandType::Block;
                cand.pos = lo;
                wire.cand = cand;
                return;
            }
            prev = p;
        }

        // ③ カーソルの近くのブロックに吸着（射程外を指していたら射程ぎりぎりの点を基準にする）
        Vec2 base = aim;
        if (Length(aim - pp) > wp.range) {
            base = pp + Normalize(aim - pp) * wp.range;
        }
        const IndexSet bi = ToIndex(base);
        float bestD = wp.blockAssistRadius;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int ix = bi.x + dx;
                const int iy = bi.y + dy;
                if (ix < 0 || ix >= kW || iy < 0 || iy >= kH || !IsBlockIdx(ix, iy)) {
                    continue;
                }
                const Vec2 c = CellCenter(ix, iy);
                const float d = Length(c - base);
                const float dp = Length(c - pp);
                if (d >= bestD || dp > wp.range || dp < wp.minRange) {
                    continue;
                }
                if (SegmentBlocked(pp, c, 0.9f)) {
                    continue;
                }
                bestD = d;
                cand.type = CandType::Block;
                cand.pos = c + Normalize(pp - c) * 0.5f; // プレイヤー側へ寄せる
            }
        }
        wire.cand = cand;
    }

    //==================================================
    // プレイヤー
    //==================================================
    void Game::UpdatePlayer(const InputState& in)
    {
        Player& p = player;
        p.attacking = (wire.state == WireState::Pull);
        if (p.invincible > 0.0f) {
            p.invincible -= kDt;
        }
        if (wire.cooldown > 0.0f) {
            wire.cooldown -= kDt;
        }
        if (p.wireMomentum > 0.0f) {
            p.wireMomentum -= kDt;
        }

        TryStartWire();

        switch (wire.state) {
        case WireState::None: RootUpdate(in); break;
        case WireState::Shoot: ShootUpdate(in); break;
        case WireState::Pull: PullUpdate(); break;
        case WireState::Swing: SwingUpdate(in); break;
        }
    }

    void Game::RootUpdate(const InputState& in)
    {
        const JumpParams& jp = settings.jump;
        Player& p = player;

        // コヨーテタイム
        p.coyote = p.onGround ? 0.1f : (std::max)(0.0f, p.coyote - kDt);

        // ジャンプ（地上 or コヨーテ中）
        if ((p.onGround || p.coyote > 0.0f) && in.jumpPressed) {
            p.vel.y = jp.jumpSpeed;
            p.jumpArc = true;
            p.onGround = false;
            p.coyote = 0.0f;
        }
        // 可変ジャンプ：上昇中にボタンを離したら抑える
        if (p.jumpArc && !p.onGround && p.vel.y > 0.0f && !in.jumpHeld) {
            p.vel.y *= jp.jumpCut;
            p.jumpArc = false;
        }

        // 重力：上昇は軽く、落下は重く、頂点はふわっと
        float scale = p.vel.y > 0.0f ? jp.gravityScaleUp : jp.gravityScaleDown;
        if (std::fabs(p.vel.y) < jp.apexThreshold) {
            scale *= jp.apexFloat;
        }
        if (p.wireMomentum > 0.0f) {
            scale *= settings.wire.floatGravityScale;
        }
        p.vel.y -= kGravity * scale;
        p.vel.y = (std::max)(p.vel.y, -jp.maxFallSpeed);

        // 横移動
        const int dir = (in.right ? 1 : 0) - (in.left ? 1 : 0);
        float drag = p.onGround ? jp.groundFriction : jp.airDrag;
        if (p.wireMomentum > 0.0f) {
            drag = (std::max)(drag, settings.wire.momentumAttenuation);
        }
        if (dir != 0) {
            p.lr = dir;
            float accel = p.onGround ? jp.groundAccel : jp.airAccel;
            if (p.vel.x * dir < 0.0f) {
                accel *= 2.0f; // 切り返しは速く
            }
            if (p.vel.x * dir < jp.runSpeed) {
                p.vel.x += dir * accel;
                p.vel.x = dir > 0 ? (std::min)(p.vel.x, jp.runSpeed) : (std::max)(p.vel.x, -jp.runSpeed);
            }
            else {
                // 走り速度を超えている分（ワイヤーの勢い）はゆるやかに落とす
                p.vel.x *= (std::max)(drag, 0.98f);
            }
        }
        else {
            p.vel.x *= drag;
            if (std::fabs(p.vel.x) < 0.002f) {
                p.vel.x = 0.0f;
            }
        }

        const Collision c = MoveWithCollision(p.vel);
        ApplyMoveResult(c);
    }

    //==================================================
    // ワイヤー
    //==================================================
    void Game::TryStartWire()
    {
        if (wire.state == WireState::None && wire.cooldown <= 0.0f && wire.fireBuffer > 0.0f
            && wire.cand.type != CandType::None) {
            StartWire(wire.cand);
            wire.fireBuffer = 0.0f;
        }
    }

    void Game::StartWire(const Candidate& cand)
    {
        wire.state = WireState::Shoot;
        wire.target = cand.pos;
        wire.enemyId = (cand.type == CandType::Enemy) ? cand.enemyId : -1;
        const float dist = Length(cand.pos - player.pos);
        wire.duration = (std::max)(settings.wire.shootTime, dist / (settings.wire.shootSpeed * 60.0f));
        wire.timer = wire.duration;
        wire.blockTimer = 0.0f;
        wire.swingPeak = 0.0f;
        player.jumpArc = false;
        player.lr = cand.pos.x >= player.pos.x ? 1 : -1;
    }

    Vec2 Game::WireTip() const
    {
        switch (wire.state) {
        case WireState::Shoot: {
            const float u = wire.duration > 1e-5f ? 1.0f - wire.timer / wire.duration : 1.0f;
            return player.pos + (wire.target - player.pos) * Clamp(u, 0.0f, 1.0f);
        }
        case WireState::Pull: return wire.target;
        case WireState::Swing: return wire.anchor;
        default: return player.pos;
        }
    }

    void Game::FinishWire(float momentumTime)
    {
        wire.state = WireState::None;
        wire.enemyId = -1;
        wire.cooldown = settings.wire.cooldown;
        player.wireMomentum = momentumTime;
        player.attacking = false;
    }

    void Game::ShootUpdate(const InputState& in)
    {
        (void)in;
        Player& p = player;
        const WireParams& wp = settings.wire;
        wire.timer -= kDt;

        if (wire.enemyId >= 0) {
            const Enemy* e = FindEnemy(wire.enemyId);
            if (!e || e->dying) {
                FinishWire(0.0f);
                return;
            }
            wire.target = e->pos;
        }
        else if (!wire.hold) {
            FinishWire(0.0f); // ブロックへのワイヤーは、発射中に離すとキャンセル
            return;
        }

        p.lr = wire.target.x >= p.pos.x ? 1 : -1;
        p.vel.x *= wp.shootDamping;
        if (!p.onGround) {
            p.vel.y -= kGravity * 0.4f;
            p.vel.y = (std::max)(p.vel.y, -settings.jump.maxFallSpeed);
        }
        const Collision c = MoveWithCollision(p.vel);
        ApplyMoveResult(c);

        if (wire.timer <= 0.0f) {
            if (wire.enemyId >= 0) {
                wire.state = WireState::Pull;
                wire.pullSpeed = 0.0f;
                wire.pullTime = 0.0f;
                wire.stall = 0;
            }
            else {
                AttachSwing();
            }
        }
    }

    void Game::PullUpdate()
    {
        Player& p = player;
        const WireParams& wp = settings.wire;
        Enemy* e = FindEnemyMut(wire.enemyId);
        if (!e || e->dying) {
            FinishWire(0.0f);
            return;
        }
        wire.target = e->pos;
        wire.pullTime += kDt;
        if (wire.pullTime > wp.pullMaxTime) {
            FinishWire(0.0f);
            return;
        }
        wire.pullSpeed = (std::min)(wire.pullSpeed + wp.pullAccel, wp.pullSpeed); // 加速して最高速度で頭打ち
        p.vel = Normalize(e->pos - p.pos) * wire.pullSpeed;
        p.lr = e->pos.x >= p.pos.x ? 1 : -1;

        const Vec2 before = p.pos;
        const Collision c = MoveWithCollision(p.vel);
        (void)c;
        p.onGround = false;
        const float moved = Length(p.pos - before);
        if (moved < wire.pullSpeed * 0.25f) {
            ++wire.stall;
        }
        else {
            wire.stall = 0;
        }

        // 敵に当たった
        const float hw = kPlayerW * 0.5f + 0.15f;
        if (std::fabs(p.pos.x - e->pos.x) < hw + 0.5f && std::fabs(p.pos.y - e->pos.y) < kPlayerH * 0.5f + 0.5f + 0.15f) {
            HitEnemy(*e);
            return;
        }
        if (wire.stall > 8) {
            FinishWire(0.0f);
        }
    }

    // ブロックにワイヤーが付いた瞬間
    void Game::AttachSwing()
    {
        Player& p = player;
        const WireParams& wp = settings.wire;
        wire.state = WireState::Swing;
        wire.anchor = wire.target;
        wire.fromGround = p.onGround;
        wire.blockTimer = 0.0f;
        wire.swingPeak = Length(p.vel);

        const float dist = Length(p.pos - wire.anchor);
        wire.ropeLength = Clamp(dist, 1.5f, wp.range * 1.5f);

        if (p.onGround) {
            p.vel.y = (std::max)(p.vel.y, (std::max)(wp.attachHop, 0.28f)); // 地上から撃ったら小さく浮く
            p.onGround = false;
        }

        // アンカーの真下を探して、最下点が地面より上に残る長さまでロープを縮める
        float groundTop = -2.0f;
        for (float y = wire.anchor.y; y > -2.0f; y -= 0.1f) {
            if (IsBlockAt({ wire.anchor.x, y })) {
                const IndexSet i = ToIndex({ wire.anchor.x, y });
                groundTop = CellRect(i.x, i.y).top;
                break;
            }
        }
        const float maxLen = wire.anchor.y - kPlayerH * 0.5f - groundTop - wp.attachClearance;
        float targetLen = Clamp(maxLen, wp.attachMinLength, wire.ropeLength);
        if (wire.fromGround) {
            targetLen = (std::max)(wp.attachMinLength, (std::min)(targetLen, wire.ropeLength * wp.attachGroundRatio));
        }
        wire.shrinkFrom = wire.ropeLength;
        wire.shrinkTo = (std::min)(targetLen, wire.ropeLength);
        wire.shrinkFrame = 0;
        const int baseFrames = wire.fromGround ? wp.attachGroundShrinkFrames : wp.attachShrinkFrames;
        const float diff = wire.shrinkFrom - wire.shrinkTo;
        wire.shrinkFrames = (std::max)(baseFrames, static_cast<int>(std::ceil(1.5f * diff / 0.35f)));
        wire.shrinkFrames = (std::max)(wire.shrinkFrames, 1);

        // 取り付けアシスト：振れる向きに最低限の速度を乗せる（助走なしでも弧に乗る）
        const Vec2 rv = p.pos - wire.anchor;
        const float rl = Length(rv);
        if (rl > 1e-4f) {
            const Vec2 tang{ -rv.y / rl, rv.x / rl };
            const float s = Dot(p.vel, tang);
            if (std::fabs(s) < wp.attachAssistSpeed) {
                float sign = s >= 0.0f ? 1.0f : -1.0f;
                if (std::fabs(s) < 0.01f) {
                    sign = (tang.x * static_cast<float>(p.lr) >= 0.0f) ? 1.0f : -1.0f;
                }
                p.vel = p.vel + tang * (sign * (wp.attachAssistSpeed - std::fabs(s)));
            }
        }
    }

    // 振り子
    void Game::SwingUpdate(const InputState& in)
    {
        Player& p = player;
        const WireParams& wp = settings.wire;

        // ロープを縮める（smoothstep）
        float shrunk = 0.0f;
        if (wire.shrinkFrame < wire.shrinkFrames) {
            ++wire.shrinkFrame;
            const float u = static_cast<float>(wire.shrinkFrame) / static_cast<float>(wire.shrinkFrames);
            const float newLen = wire.shrinkFrom + (wire.shrinkTo - wire.shrinkFrom) * SmoothStep01(u);
            shrunk = wire.ropeLength - newLen;
            wire.ropeLength = newLen;
        }

        // 離脱：ボタンを離す / Space
        if (!wire.hold) {
            ReleaseSwing(false, true);
            return;
        }
        if (in.jumpPressed) {
            ReleaseSwing(true, true);
            return;
        }

        // ロープが障害物に遮られ続けたら外れる（遠いときだけ見る。角をかすめただけでは切れない）
        if (Length(p.pos - wire.anchor) > 2.0f && SegmentBlocked(p.pos, wire.anchor, 1.0f)) {
            wire.blockTimer += kDt;
            if (wire.blockTimer > wp.ropeBlockTime) {
                ReleaseSwing(false, false);
                return;
            }
        }
        else {
            wire.blockTimer = 0.0f;
        }

        // 1) A/D：ロープに直角（接線）に力を加える = 漕ぐ
        const int steer = (in.right ? 1 : 0) - (in.left ? 1 : 0);
        Vec2 rv = p.pos - wire.anchor;
        float rl = Length(rv);
        if (rl < 1e-4f) {
            rv = { 0.0f, -1.0f };
            rl = 1.0f;
        }
        Vec2 tang{ -rv.y / rl, rv.x / rl };
        if (steer != 0 && tang.x * static_cast<float>(steer) < 0.0f) {
            tang = tang * -1.0f;
        }
        if (std::fabs(tang.x) < 0.25f && Dot(p.vel, tang) < 0.0f) {
            tang = tang * -1.0f; // ほぼ真横：今動いている向きに漕ぐ
        }
        if (steer != 0) {
            p.lr = steer;
        }
        p.vel = p.vel + tang * wp.swingForce * (steer != 0 ? 1.0f : 0.0f);

        // 2) 重力と空気抵抗
        p.vel.y -= wp.swingGravity;
        p.vel = p.vel * wp.swingDrag;

        // 3) 動かしてロープ長で拘束（位置ベース）。拘束後の移動量を速度にする
        Vec2 newPos = p.pos + p.vel;
        Vec2 d = newPos - wire.anchor;
        if (Length(d) > wire.ropeLength) {
            newPos = wire.anchor + Normalize(d) * wire.ropeLength;
        }
        p.vel = newPos - p.pos;

        // 4) 最高速度の制限と、直近の最高速度の記録（離脱時の救済）
        float speed = Length(p.vel);
        if (speed > wp.swingMaxSpeed) {
            p.vel = p.vel * (wp.swingMaxSpeed / speed);
            speed = wp.swingMaxSpeed;
        }
        wire.swingPeak = (std::max)(speed, wire.swingPeak * 0.985f);

        // 5) 実際の衝突
        const Vec2 before = p.pos;
        const Collision c = MoveWithCollision(p.vel);
        p.vel = p.pos - before;
        p.onGround = c.ground;
        p.lr = p.vel.x > 0.01f ? 1 : (p.vel.x < -0.01f ? -1 : p.lr);

        // 6) ロープを巻き取っただけの内向きの動きは本当の速度ではないので取り除く
        if (shrunk > 0.0f) {
            p.vel = p.vel + Normalize(p.pos - wire.anchor) * shrunk;
        }

        // マップの左右の端
        const float minX = kPlayerW * 0.5f - 0.5f;
        const float maxX = kW - 0.5f - kPlayerW * 0.5f;
        p.pos.x = Clamp(p.pos.x, minX, maxX);
    }

    void Game::ReleaseSwing(bool kick, bool boost)
    {
        Player& p = player;
        const WireParams& wp = settings.wire;
        const float speed = Length(p.vel);
        if (boost && speed > 1e-4f) {
            // 離すタイミングが少しずれても気持ちよく飛べるよう、直近の最高速度の一部まで底上げする
            const float base = (std::max)(speed, (std::min)(wire.swingPeak * wp.releaseBufferKeep, wp.swingBoostMax));
            const float target = (std::min)(base * wp.swingReleaseBoost, (std::max)(wp.swingBoostMax, base));
            p.vel = p.vel * (target / speed);
        }
        if (kick) {
            p.vel.y += wp.releaseKick;
        }
        p.jumpArc = false;
        FinishWire(wp.momentumTime);
    }

    //==================================================
    // 敵を倒す / 被弾
    //==================================================
    void Game::KillEnemy(Enemy& e)
    {
        e.dying = true;
        e.dyingTimer = 0.0f;
        ++killCount;
        killZoom = 0.0001f;
        SpawnBurst(e.pos, 18, 7.0f, 1.0f, 0.55f, 0.2f);
    }

    // ワイヤーの突進が敵に当たった
    void Game::HitEnemy(Enemy& e)
    {
        Player& p = player;
        const WireParams& wp = settings.wire;
        const float dirX = e.pos.x >= p.pos.x ? 1.0f : -1.0f;

        // 盾：お互いに向き合っているならガード成功（敵は倒れない）
        if (e.shield && static_cast<float>(e.dir) * -dirX > 0.0f) {
            p.vel = { -dirX * 0.30f, 0.25f };
            e.guardFlash = 0.3f;
            SpawnBurst((p.pos + e.pos) * 0.5f, 10, 5.0f, 0.6f, 0.8f, 1.0f);
            hitStop = 4;
            FinishWire(wp.hitMomentumTime);
            return;
        }

        const float speed = Length(p.vel);
        KillEnemy(e);
        hitStop = 5;
        p.vel.x = dirX * (std::min)((std::max)(wp.hitBounceForward, speed * wp.hitKeepRatio), wp.exitSpeedCap);
        p.vel.y = wp.hitBounceUp;
        p.onGround = false;
        SpawnBurst((p.pos + e.pos) * 0.5f, 12, 6.0f, 1.0f, 1.0f, 0.6f);
        FinishWire(wp.hitMomentumTime);
    }

    void Game::TakeDamage(float fromX)
    {
        Player& p = player;
        if (p.invincible > 0.0f || phase != Phase::Play) {
            return;
        }
        --p.hp;
        p.invincible = 1.5f;
        const float dir = p.pos.x >= fromX ? 1.0f : -1.0f;
        p.vel = { 0.28f * dir, 0.35f };
        p.onGround = false;
        p.jumpArc = false;
        p.wireMomentum = 0.0f;
        if (wire.state != WireState::None) {
            FinishWire(0.0f);
        }
        hitStop = 4;
        SpawnBurst(p.pos, 10, 5.0f, 1.0f, 0.3f, 0.3f);
        if (p.hp <= 0) {
            phase = Phase::Death;
            SpawnBurst(p.pos, 30, 9.0f, 1.0f, 0.5f, 0.2f);
        }
    }

    //==================================================
    // 敵
    //==================================================
    void Game::UpdateEnemies()
    {
        for (Enemy& e : enemies) {
            UpdateEnemy(e);
        }
        ResolveEnemyCollisions();
        // 片付け
        for (size_t i = 0; i < enemies.size();) {
            if (enemies[i].dead) {
                if (wire.enemyId == enemies[i].id) {
                    FinishWire(0.0f); // ワイヤーの標的が消えた
                }
                enemies.erase(enemies.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else {
                ++i;
            }
        }
    }

    void Game::UpdateEnemy(Enemy& e)
    {
        if (e.guardFlash > 0.0f) {
            e.guardFlash -= kDt;
        }
        if (e.dying) {
            e.dyingTimer += kDt;
            if (e.dyingTimer >= 0.4f) {
                e.dead = true;
            }
            return;
        }
        e.walkTime += kDt;

        // 横：配置位置を中心に往復。ブロックに当たる / 足元が無い（ledge）なら反転
        if (e.canMove) {
            if (e.pos.x > e.homeX + e.patrolRange) {
                e.dir = -1;
            }
            else if (e.pos.x < e.homeX - e.patrolRange) {
                e.dir = 1;
            }
            const float fd = static_cast<float>(e.dir);
            const float nx = e.pos.x + fd * e.speed;
            const float fx = nx + fd * 0.5f;
            bool blocked = IsBlockAt({ fx, e.pos.y + 0.45f }) || IsBlockAt({ fx, e.pos.y }) || IsBlockAt({ fx, e.pos.y - 0.45f });
            if (!blocked && e.turnAtLedge && e.onGround) {
                if (!IsBlockAt({ nx + fd * 0.45f, e.pos.y - 0.5f - 0.15f })) {
                    blocked = true;
                }
            }
            if (blocked) {
                e.dir = -e.dir;
            }
            else {
                e.pos.x = nx;
            }
        }

        // 縦：重力と着地
        e.vel.y = (std::max)(e.vel.y - 0.03f, -0.4f);
        const float ny = e.pos.y + e.vel.y;
        e.onGround = false;
        if (e.vel.y <= 0.0f) {
            const float xs[2] = { e.pos.x - 0.45f, e.pos.x + 0.45f };
            for (float x : xs) {
                const Vec2 foot{ x, ny - 0.5f };
                if (IsBlockAt(foot)) {
                    const IndexSet i = ToIndex(foot);
                    e.pos.y = CellRect(i.x, i.y).top + 0.5f;
                    e.vel.y = 0.0f;
                    e.onGround = true;
                    break;
                }
            }
            if (!e.onGround) {
                e.pos.y = ny;
            }
        }
        else {
            e.pos.y = ny;
        }

        if (e.pos.y < -3.0f) {
            e.dead = true; // 落ちたら静かに消える
        }
    }

    // 重なった敵同士を横に押し返す
    void Game::ResolveEnemyCollisions()
    {
        for (size_t i = 0; i < enemies.size(); ++i) {
            for (size_t j = i + 1; j < enemies.size(); ++j) {
                Enemy& a = enemies[i];
                Enemy& b = enemies[j];
                if (a.dying || b.dying || a.dead || b.dead) {
                    continue;
                }
                const float dx = b.pos.x - a.pos.x;
                if (std::fabs(dx) < 1.0f && std::fabs(b.pos.y - a.pos.y) < 1.0f) {
                    const float push = (1.0f - std::fabs(dx)) * 0.5f;
                    const float s = dx >= 0.0f ? 1.0f : -1.0f;
                    if (!IsBlockAt({ a.pos.x - s * (push + 0.5f), a.pos.y })) {
                        a.pos.x -= s * push;
                    }
                    if (!IsBlockAt({ b.pos.x + s * (push + 0.5f), b.pos.y })) {
                        b.pos.x += s * push;
                    }
                }
            }
        }
    }

    // 敵との接触：上から踏めば倒せる。それ以外はダメージ
    void Game::CheckPlayerEnemyContact()
    {
        if (player.attacking || phase != Phase::Play) {
            return;
        }
        for (Enemy& e : enemies) {
            if (e.dying || e.dead) {
                continue;
            }
            if (std::fabs(player.pos.x - e.pos.x) < kPlayerW * 0.5f + 0.5f && std::fabs(player.pos.y - e.pos.y) < kPlayerH * 0.5f + 0.5f) {
                // 落下中で、足が敵の上半分にあれば踏みつけ
                if (player.vel.y < 0.0f && player.pos.y - kPlayerH * 0.5f > e.pos.y) {
                    KillEnemy(e);
                    player.vel.y = 0.38f;
                    player.onGround = false;
                    player.jumpArc = false;
                    hitStop = 4;
                    return;
                }
                if (player.invincible <= 0.0f) {
                    TakeDamage(e.pos.x);
                    return;
                }
            }
        }
    }

    //==================================================
    // カメラ
    //==================================================
    void Game::UpdateCamera()
    {
        const Vec2 target = player.pos;
        const Vec2 v = player.vel;
        Vec2 desired;
        desired.x = target.x + v.x * 2.5f;
        desired.y = target.y + v.y * 0.6f;
        if (v.y < 0.0f) {
            desired.y += v.y * 1.2f; // 落下中は少し下を見る
        }
        desired.x = target.x + Clamp(desired.x - target.x, -5.0f, 5.0f);
        cam.x += (desired.x - cam.x) * 0.10f;
        cam.y += (desired.y - cam.y) * 0.08f;
        ApplyCameraBounds();
    }

    // 表示範囲を制限する（マップの端を映さない。下は穴の落下が見えるよう少し開ける）
    void Game::ApplyCameraBounds()
    {
        const float halfH = screenH / (2.0f * ppu);
        const float halfW = screenW / (2.0f * ppu);
        const float minX = halfW - 0.5f;
        const float maxX = kW - 0.5f - halfW;
        cam.x = (maxX < minX) ? (kW - 1) * 0.5f : Clamp(cam.x, minX, maxX);
        const float minY = -2.5f + halfH;
        const float maxY = kH - 0.5f - halfH;
        cam.y = (maxY < minY) ? (minY + maxY) * 0.5f : Clamp(cam.y, minY, maxY);
    }

    //==================================================
    // 火花
    //==================================================
    void Game::SpawnBurst(Vec2 pos, int count, float speed, float r, float g, float b)
    {
        for (int i = 0; i < count; ++i) {
            Particle p;
            const float a = Rand01() * 2.0f * kPi;
            const float s = speed * (0.4f + Rand01() * 0.6f);
            p.pos = pos;
            p.vel = { std::cos(a) * s, std::sin(a) * s };
            p.maxLife = 0.35f + Rand01() * 0.3f;
            p.life = p.maxLife;
            p.size = 0.12f + Rand01() * 0.14f;
            p.r = r;
            p.g = g;
            p.b = b;
            particles.push_back(p);
        }
    }

    void Game::UpdateParticles()
    {
        for (Particle& p : particles) {
            p.life -= kDt;
            p.pos = p.pos + p.vel * kDt;
            p.vel.y -= 14.0f * kDt;
        }
        particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0.0f; }), particles.end());
    }
}

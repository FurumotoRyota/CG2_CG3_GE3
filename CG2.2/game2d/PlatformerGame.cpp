#include "PlatformerGame.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace pf
{
    namespace
    {
        constexpr float kDt = 1.0f / 60.0f;
        constexpr float kBlank = 0.001f;
        constexpr float kPi = 3.14159265f;

        float Clamp(float v, float lo, float hi) { return (std::max)(lo, (std::min)(v, hi)); }
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

    void Game::SetBlock(int ix, int worldY, bool solid)
    {
        const int iy = kH - 1 - worldY;
        if (ix < 0 || ix >= kW || iy < 0 || iy >= kH) {
            return;
        }
        map_[static_cast<size_t>(iy) * kW + ix] = solid ? 1 : 0;
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
        ++mapVersion;
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
        ++mapVersion;
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
        enemies.clear();
        particles.clear();
        nextEnemyId_ = 1;
        for (const EnemySpawn& s : enemySpawns_) {
            Enemy e;
            e.id = nextEnemyId_++;
            e.pos = s.pos;
            e.homeX = s.pos.x;
            e.shield = s.shield;
            e.hp = s.shield ? 2 : 1;
            e.dir = -1;
            enemies.push_back(e);
        }
        phase = Phase::Play;
        ++resetCount;
        hitStop = 0;
        killZoom = 0.0f;
        killCount = 0;
        time = 0.0f;
        cam = player.pos;
        ApplyCameraBounds();
    }

    void Game::ClearStage()
    {
        std::fill(map_.begin(), map_.end(), static_cast<uint8_t>(0));
        enemySpawns_.clear();
        hasGoal = false;
        ++mapVersion;
    }

    void Game::AddEnemySpawn(Vec2 pos, bool shield)
    {
        enemySpawns_.push_back({ pos, shield });
    }

    void Game::SetGoal(Vec2 pos)
    {
        hasGoal = true;
        goal = pos;
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
    // プレイヤー
    //==================================================
    void Game::UpdatePlayer(const InputState& in)
    {
        if (player.invincible > 0.0f) {
            player.invincible -= kDt;
        }
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
        p.vel.y -= kGravity * scale;
        p.vel.y = (std::max)(p.vel.y, -jp.maxFallSpeed);

        // 横移動
        const int dir = (in.right ? 1 : 0) - (in.left ? 1 : 0);
        float drag = p.onGround ? jp.groundFriction : jp.airDrag;
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
        if (phase != Phase::Play) {
            return;
        }
        for (Enemy& e : enemies) {
            if (e.dying || e.dead) {
                continue;
            }
            if (std::fabs(player.pos.x - e.pos.x) < kPlayerW * 0.5f + 0.5f && std::fabs(player.pos.y - e.pos.y) < kPlayerH * 0.5f + 0.5f) {
                // 落下中で、足が敵の上半分にあれば踏みつけ
                if (player.vel.y < 0.0f && player.pos.y - kPlayerH * 0.5f > e.pos.y) {
                    if (e.hp > 1) {
                        --e.hp; // 盾の敵は1回目では倒れない
                        e.guardFlash = 0.3f;
                        SpawnBurst(e.pos, 8, 5.0f, 0.6f, 0.8f, 1.0f);
                    }
                    else {
                        KillEnemy(e);
                    }
                    player.vel.y = settings.jump.stompBounce;
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

// 【見本A】ゲーム用クラスの例：弾を撃って、敵に当たったら両方消える
//   エディタで「player」「enemy」を名前を付けて置いておく。弾は「bullet」という名前で置いておき、Spawn で複製して出す。
//   ※ GameApi はまだ無いので、このファイルは設計案の見本（構文だけ確認済み）
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "A_GameApi.h"
#include "math/Collision.h"

class MyGame : public IGameLogic
{
public:
    void Initialize(GameApi& api) override
    {
        player_ = api.Find("player");
        bullets_.clear();
        score_ = 0;
        fireTimer_ = 0.0f;
        spawnTimer_ = 0.0f;
    }

    void Update(GameApi& api) override
    {
        const float dt = api.Dt();
        const GameInput& in = api.Input();

        // --- プレイヤー ---
        Vector2 p = api.GetPosition(player_);
        p.x += in.Move().x * kPlayerSpeed * dt;
        p.y += in.Move().y * kPlayerSpeed * dt;
        api.SetPosition(player_, p);
        api.FollowCamera(player_, 14.0f);

        // --- 撃つ ---
        fireTimer_ -= dt;
        if (in.Fire() && in.HasAim() && fireTimer_ <= 0.0f) {
            Bullet b;
            b.ref = api.Spawn("bullet", p);
            b.vel = { in.Aim().x * kBulletSpeed, in.Aim().y * kBulletSpeed };
            b.life = kBulletLife;
            bullets_.push_back(b);
            fireTimer_ = kFireInterval;
        }

        // --- 敵を出す（プレイヤーの周りの円周上） ---
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0.0f) {
            const float a = static_cast<float>(std::rand() % 628) / 100.0f;
            api.Spawn("enemy", { p.x + std::cos(a) * 12.0f, p.y + std::sin(a) * 12.0f });
            spawnTimer_ = kSpawnInterval;
        }

        // --- 敵を追わせる ---
        const std::vector<ObjectRef> enemies = api.FindAll("enemy");
        for (const ObjectRef& e : enemies) {
            Vector2 ep = api.GetPosition(e);
            const float dx = p.x - ep.x;
            const float dy = p.y - ep.y;
            const float len = (std::max)(0.0001f, std::sqrt(dx * dx + dy * dy));
            api.SetPosition(e, { ep.x + dx / len * kEnemySpeed * dt, ep.y + dy / len * kEnemySpeed * dt });
        }

        // --- 弾を動かして、敵に当たったら両方消す ---
        for (Bullet& b : bullets_) {
            Vector2 bp = api.GetPosition(b.ref);
            bp.x += b.vel.x * dt;
            bp.y += b.vel.y * dt;
            api.SetPosition(b.ref, bp);
            b.life -= dt;

            for (const ObjectRef& e : enemies) {
                if (api.IsAlive(e) && Collision::CircleCircle(bp, api.GetRadius(b.ref), api.GetPosition(e), api.GetRadius(e))) {
                    api.Destroy(e);
                    b.life = 0.0f;
                    ++score_;
                    api.Fx().Shake(0.15f, 0.15f);
                    api.Fx().HitStop(2);
                    break;
                }
            }
            if (b.life <= 0.0f) {
                api.Destroy(b.ref);
            }
        }
        bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return b.life <= 0.0f; }), bullets_.end());
    }

private:
    struct Bullet
    {
        ObjectRef ref;
        Vector2 vel{ 0.0f, 0.0f };
        float life = 0.0f;
    };

    static constexpr float kPlayerSpeed = 6.0f;
    static constexpr float kEnemySpeed = 2.5f;
    static constexpr float kBulletSpeed = 14.0f;
    static constexpr float kBulletLife = 1.5f;
    static constexpr float kFireInterval = 0.12f;
    static constexpr float kSpawnInterval = 1.0f;

    ObjectRef player_;
    std::vector<Bullet> bullets_;
    float fireTimer_ = 0.0f;
    float spawnTimer_ = 0.0f;
    int score_ = 0;
};

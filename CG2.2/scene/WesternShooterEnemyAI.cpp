// 西部シューティングの敵のAI（WesternShooterScene の一部。長くなるので別ファイルにしてある）
//
//   考え方（上から順に、敵1体ごとに毎フレーム）:
//     1. 見える？   : 自機との間に壁・樽が無く、kSightRange 以内なら見える（GridNav::HasLineOfSight）
//     2. 見失った？ : Search（最後に見た場所へ行って探す）→ 時間切れで hunting（自機の居場所をたどる）
//     3. 行動する   : ごろつき … Advance → Rush（突っ込む）/ Hold（人数制限で取り巻いて待つ）
//                     ガンマン … 自機が近づいたらまず陰へ（Hide）→ 顔を出して（Peek）数発撃つ（Shoot）→ 陰へ戻る…
//                                陰が無ければ、左右に散って（Advance）立ち止まって撃つ（Shoot）
//     4. 重ならない : 敵どうしを押し離し、最後に壁から押し出す
//   歩き方: まっすぐ歩けるなら直進。壁があれば GridNav::FindPath の道を曲がり角ごとにたどる（kRepathInterval ごとに引き直し）
#include "WesternShooterScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "math/Collision.h"
#include "WesternShooterCommon.h"

using namespace WesternShooter;

namespace {
    Vector2 Rotate(const Vector2& v, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }

    float Random01() { return static_cast<float>(std::rand() % 100) * 0.01f; }

    constexpr float kHideTravelLimit = 4.0f;     // 隠れ場所へ向かう時間の上限（着けなければ諦めて戻る）
    constexpr float kInPositionTimeout = 3.0f;   // 狙う位置に着けなくても、この時間たてば撃ち始める
}

float WesternShooterScene::RadiusOf(EnemyType type)
{
    return (type == EnemyType::Brute) ? kBruteRadius : kGunmanRadius;
}

// ---------------------------------------------------------------- 全体

void WesternShooterScene::UpdateEnemies(float dt)
{
    // 同時に突っ込める数・撃てる数を数えておく（敵の行動が変わるたびに増減させる）
    AiShared shared;
    for (const Enemy& e : enemies_) {
        if (!e.obj) {
            continue;
        }
        if (e.state == AiState::Rush) {
            ++shared.rushers;
        }
        else if (e.state == AiState::Shoot || e.state == AiState::Peek) {
            ++shared.shooters;
        }
        else if (e.state == AiState::Hide) {
            shared.covers.push_back(e.coverPos);
        }
    }

    for (Enemy& e : enemies_) {
        if (e.obj) {
            UpdateEnemy(e, dt, shared);
        }
    }

    SeparateEnemies();

    for (Enemy& e : enemies_) {
        if (!e.obj) {
            continue;
        }
        PushOutOfWalls(e.pos, RadiusOf(e.type)); // 押し離されて壁にめり込んだら戻す
        Place(*e.obj, e.pos);
        TintEnemy(e);
    }
}

void WesternShooterScene::UpdateEnemy(Enemy& e, float dt, AiShared& shared)
{
    const float radius = RadiusOf(e.type);
    const float dist = Length2D({ playerPos_.x - e.pos.x, playerPos_.y - e.pos.y });

    e.coverRetry = (std::max)(0.0f, e.coverRetry - dt);

    // 1. 見える？
    e.sees = dist <= kSightRange && GridNav::HasLineOfSight(map_, e.pos, playerPos_);

    // 2. 見えたら覚える。見失ったら Search へ（隠れている最中は、そのまま隠れる）
    if (e.sees) {
        e.lastSeen = playerPos_;
        e.hunting = false;
        if (e.state == AiState::Search) {
            EnterState(e, AiState::Advance, shared);
        }
    }
    else if (!e.hunting && e.state != AiState::Search && e.state != AiState::Hide && e.state != AiState::Peek) {
        EnterState(e, AiState::Search, shared);
    }

    // Search：最後に見た場所へ行って、しばらく探す。見つからなければ居場所をたどる（hunting）
    if (e.state == AiState::Search) {
        const float speed = (e.type == EnemyType::Brute) ? kBruteSpeed : kGunmanSpeed;
        if (Length2D({ e.lastSeen.x - e.pos.x, e.lastSeen.y - e.pos.y }) > 0.8f) {
            MoveToward(e, e.lastSeen, radius, speed, dt);
        }
        e.stateTimer -= dt;
        if (e.stateTimer <= 0.0f) {
            e.hunting = true;
            EnterState(e, AiState::Advance, shared);
        }
        return;
    }

    // 3. 行動する
    if (e.type == EnemyType::Brute) {
        UpdateBrute(e, dt, shared);
    }
    else {
        UpdateGunman(e, dt, shared);
    }
}

// ---------------------------------------------------------------- ごろつき

void WesternShooterScene::UpdateBrute(Enemy& e, float dt, AiShared& shared)
{
    const float dist = Length2D({ playerPos_.x - e.pos.x, playerPos_.y - e.pos.y });

    switch (e.state) {
    case AiState::Advance:
        // 近づいたら突っ込む。ただし同時に突っ込めるのは kMaxRushers 体まで（残りは取り巻く）
        if (e.sees && dist < kRushDistance) {
            EnterState(e, shared.rushers < kMaxRushers ? AiState::Rush : AiState::Hold, shared);
        }
        else {
            MoveToward(e, playerPos_, kBruteRadius, kBruteSpeed, dt);
        }
        break;

    case AiState::Hold:
        // 突っ込める枠が空いたら突っ込む。それまでは自機のまわりを回って待つ
        if (shared.rushers < kMaxRushers) {
            EnterState(e, AiState::Rush, shared);
        }
        else if (dist > kRushDistance + 2.0f) {
            EnterState(e, AiState::Advance, shared);
        }
        else {
            StrafeAround(e, kBruteRadius, kBruteHoldSpeed, kHoldRing, dt);
        }
        break;

    case AiState::Rush:
        MoveToward(e, playerPos_, kBruteRadius, kBruteRushSpeed, dt);
        break;

    default:
        EnterState(e, AiState::Advance, shared);
        break;
    }

    // 触れるとダメージ
    if (Collision::CircleCircle(e.pos, kBruteRadius, playerPos_, kPlayerRadius)) {
        DamagePlayer();
    }
}

// ---------------------------------------------------------------- ガンマン

void WesternShooterScene::UpdateGunman(Enemy& e, float dt, AiShared& shared)
{
    const Vector2 toPlayer = { playerPos_.x - e.pos.x, playerPos_.y - e.pos.y };
    const float dist = Length2D(toPlayer);
    const Vector2 dirToPlayer = Normalized(toPlayer, { 0.0f, -1.0f });

    switch (e.state) {
    case AiState::Advance: {
        // 見えている間は、自機の左右に散る「撃つ位置」へ向かう（位置は一度決めたら動かさない。動かすとぐるぐる回ってしまう）
        Vector2 target = playerPos_;
        if (e.flankValid && Length2D({ e.flankPos.x - playerPos_.x, e.flankPos.y - playerPos_.y }) > kGunmanKeepDistance * 1.5f) {
            e.flankValid = false; // 自機が大きく動いた：撃つ位置を決め直す
        }
        if (e.sees) {
            if (!e.flankValid) {
                const Vector2 away = Normalized({ e.pos.x - playerPos_.x, e.pos.y - playerPos_.y }, { 0.0f, -1.0f });
                const Vector2 dir = Rotate(away, e.flankAngle);
                const float r = kGunmanKeepDistance * 0.9f;
                Vector2 cand = { playerPos_.x + dir.x * r, playerPos_.y + dir.y * r };
                if (!GridNav::CanWalk(map_, cand, cand, kGunmanRadius) || !GridNav::HasLineOfSight(map_, cand, playerPos_)) {
                    cand = { playerPos_.x + away.x * r, playerPos_.y + away.y * r }; // 回り込めない：正面から
                }
                e.flankPos = cand;
                e.flankValid = true;
            }
            target = e.flankPos;
        }

        // 自機が近づいたら、まず陰へ向かう（陰から顔を出して撃つ）。陰が見つからなければ下の「立ち止まって撃つ」へ
        if (e.sees && dist <= kCoverEngageDistance && e.coverRetry <= 0.0f) {
            if (ChooseCover(e, shared)) {
                const Vector2 cover = e.coverPos;
                EnterState(e, AiState::Hide, shared);
                shared.covers.push_back(cover);
                break;
            }
            e.coverRetry = kCoverRetry;
        }

        // 撃てる距離で、狙う位置の近く（または着くのに時間がかかりすぎた）なら撃つ。ただし撃てるのは kMaxShooters 体まで
        bool inPosition = false;
        if (e.sees && dist <= kGunmanKeepDistance * 1.1f) {
            e.stateTimer += dt;
            inPosition = Length2D({ e.flankPos.x - e.pos.x, e.flankPos.y - e.pos.y }) < 3.0f || e.stateTimer > kInPositionTimeout;
        }
        if (inPosition) {
            if (shared.shooters < kMaxShooters) {
                EnterState(e, AiState::Shoot, shared);
            }
            else {
                StrafeAround(e, kGunmanRadius, kGunmanStrafeSpeed, kGunmanKeepDistance, dt); // 順番待ち
            }
        }
        else {
            MoveToward(e, target, kGunmanRadius, kGunmanSpeed, dt);
        }
        break;
    }

    case AiState::Shoot: {
        // 陰から顔を出しているときは立ち止まって撃つ。陰が無いときは、横へ動きながら撃ち、近づかれたら下がる
        if (!e.fromCover) {
            if (dist < kGunmanTooClose) {
                if (!TryStep(e, { -dirToPlayer.x, -dirToPlayer.y }, kGunmanRadius, kGunmanBackSpeed, dt)) {
                    StrafeAround(e, kGunmanRadius, kGunmanStrafeSpeed, kGunmanKeepDistance, dt);
                }
            }
            else {
                StrafeAround(e, kGunmanRadius, kGunmanStrafeSpeed, kGunmanKeepDistance, dt);
            }
        }

        e.fireTimer -= dt;
        if (e.fireTimer <= 0.0f && e.shotsLeft > 0) {
            FireEnemyBullet(e);
            --e.shotsLeft;
            e.fireTimer = kGunmanShotInterval;
        }
        if (e.shotsLeft <= 0 && e.fireTimer <= 0.0f) {
            // 撃ち終わった：同じ陰がまだ自機から見えなければそこへ戻る。見えるなら別の陰を探す
            bool haveCover = e.fromCover && !GridNav::HasLineOfSight(map_, playerPos_, e.coverPos);
            if (!haveCover) {
                haveCover = ChooseCover(e, shared);
            }
            if (haveCover) {
                const Vector2 cover = e.coverPos; // EnterState で道を消すが、隠れ場所は残る
                EnterState(e, AiState::Hide, shared);
                shared.covers.push_back(cover);
            }
            else {
                e.shotsLeft = kGunmanBurst; // 隠れ場所が無い：間をおいて撃ち続ける
                e.fireTimer = 1.0f;
            }
        }
        break;
    }

    case AiState::Hide:
        e.stateTimer -= dt;

        // 自機が動いて、いまの隠れ場所が見えるようになったら、自機の今の位置から隠れられる別の場所へ移る
        e.coverCheck -= dt;
        if (e.coverCheck <= 0.0f) {
            e.coverCheck = kCoverCheckInterval;
            if (e.coverRetry <= 0.0f && GridNav::HasLineOfSight(map_, playerPos_, e.coverPos)) {
                if (ChooseCover(e, shared)) {
                    const Vector2 cover = e.coverPos;
                    EnterState(e, AiState::Hide, shared);
                    shared.covers.push_back(cover);
                    break;
                }
                e.coverRetry = kCoverRetry;
            }
        }

        if (!e.coverArrived) {
            MoveToward(e, e.coverPos, kGunmanRadius, kGunmanSpeed * 1.3f, dt);
            if (Length2D({ e.coverPos.x - e.pos.x, e.coverPos.y - e.pos.y }) < 0.5f) {
                e.coverArrived = true;
                e.stateTimer = kReloadMin + kReloadRange * Random01(); // 陰で装填して待つ
            }
            else if (e.stateTimer <= 0.0f) {
                EnterState(e, AiState::Advance, shared); // 着けなかった
            }
        }
        else if (e.stateTimer <= 0.0f) {
            // 顔を出す番。同時に出られる数がいっぱいなら、もう少し待つ
            if (shared.shooters >= kMaxShooters) {
                e.stateTimer = 0.3f;
            }
            else if (GridNav::FindPeek(map_, e.coverPos, playerPos_, kGunmanRadius, kPeekMaxDist, kSightRange, e.peekPos)) {
                EnterState(e, AiState::Peek, shared);
            }
            else if (e.coverRetry <= 0.0f && ChooseCover(e, shared)) {
                const Vector2 cover = e.coverPos; // 自機が動いてこの陰からは狙えない：別の陰へ
                EnterState(e, AiState::Hide, shared);
                shared.covers.push_back(cover);
            }
            else {
                e.coverRetry = kCoverRetry;
                EnterState(e, AiState::Advance, shared);
            }
        }
        break;

    case AiState::Peek:
        // 陰から短く動いて、自機が見える場所に出る。着いたら立ち止まって撃つ
        e.stateTimer -= dt;
        MoveToward(e, e.peekPos, kGunmanRadius, kPeekSpeed, dt);
        if (Length2D({ e.peekPos.x - e.pos.x, e.peekPos.y - e.pos.y }) < 0.3f || e.stateTimer <= 0.0f) {
            EnterState(e, AiState::Shoot, shared);
            e.fromCover = true;
        }
        break;

    default:
        EnterState(e, AiState::Advance, shared);
        break;
    }
}

bool WesternShooterScene::ChooseCover(Enemy& e, const AiShared& shared)
{
    GridNav::CoverQuery q;
    q.from = e.pos;
    q.threat = playerPos_;
    q.searchRadius = kCoverSearchRadius;
    q.minThreatDist = kCoverMinThreatDist;
    q.maxThreatDist = kCoverMaxThreatDist;
    q.radius = kGunmanRadius;

    Vector2 cover{};
    if (!GridNav::FindCover(map_, q, shared.covers, cover)) {
        return false;
    }
    std::vector<Vector2> check; // そこへ行く道があるか確かめる
    if (!GridNav::FindPath(map_, e.pos, cover, kGunmanRadius, check)) {
        return false;
    }
    e.coverPos = cover;
    return true;
}

// ---------------------------------------------------------------- 状態の切り替え

void WesternShooterScene::EnterState(Enemy& e, AiState next, AiShared& shared)
{
    // 人数制限の数を合わせる
    if (e.state == AiState::Rush) { --shared.rushers; }
    const bool wasShooter = e.state == AiState::Shoot || e.state == AiState::Peek;
    const bool willShoot = next == AiState::Shoot || next == AiState::Peek;
    if (wasShooter && !willShoot) { --shared.shooters; }
    if (!wasShooter && willShoot) { ++shared.shooters; }
    if (next == AiState::Rush) { ++shared.rushers; }

    e.state = next;
    e.path.clear();
    e.pathIndex = 0;
    e.repathTimer = 0.0f;
    e.stateTimer = 0.0f;
    e.fromCover = false;

    switch (next) {
    case AiState::Search:
        e.stateTimer = kSearchTime;
        break;
    case AiState::Advance:
        e.flankValid = false; // 撃つ位置は、近づき直すたびに決め直す
        break;
    case AiState::Shoot:
        e.shotsLeft = kGunmanBurst;
        e.fireTimer = 0.5f; // 構えてから撃つまでの間
        break;
    case AiState::Hide:
        e.coverArrived = false;
        e.stateTimer = kHideTravelLimit;
        e.coverCheck = kCoverCheckInterval;
        break;
    case AiState::Peek:
        e.stateTimer = 2.0f; // 顔を出す場所に着けなくても、この時間で撃ち始める
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- 歩く

void WesternShooterScene::MoveToward(Enemy& e, const Vector2& target, float radius, float speed, float dt)
{
    e.repathTimer -= dt;

    // まっすぐ歩けるなら直進
    if (GridNav::CanWalk(map_, e.pos, target, radius)) {
        e.path.clear();
        e.pathIndex = 0;
        const Vector2 d = { target.x - e.pos.x, target.y - e.pos.y };
        const float len = Length2D(d);
        if (len > 0.0001f) {
            const float step = (std::min)(speed * dt, len);
            e.pos.x += d.x / len * step;
            e.pos.y += d.y / len * step;
        }
        return;
    }

    // 壁がある：道を探し、曲がり角ごとにたどる
    if (e.pathIndex >= e.path.size() || e.repathTimer <= 0.0f) {
        GridNav::FindPath(map_, e.pos, target, radius, e.path);
        e.pathIndex = 0;
        e.repathTimer = kRepathInterval;
    }
    if (e.path.empty()) {
        // 道が無い：壁に向かって押し付けるだけにして、無理に動かない
        return;
    }
    FollowPath(e, radius, speed, dt);
}

bool WesternShooterScene::FollowPath(Enemy& e, float radius, float speed, float dt)
{
    (void)radius;
    float remaining = speed * dt;
    while (remaining > 0.0f && e.pathIndex < e.path.size()) {
        const Vector2 w = e.path[e.pathIndex];
        const Vector2 d = { w.x - e.pos.x, w.y - e.pos.y };
        const float len = Length2D(d);
        const bool last = e.pathIndex + 1 == e.path.size();
        if (len <= (last ? 0.1f : kWaypointReach)) {
            ++e.pathIndex; // 着いた：次の角へ
            continue;
        }
        const float step = (std::min)(remaining, len);
        e.pos.x += d.x / len * step;
        e.pos.y += d.y / len * step;
        remaining -= step;
    }
    return e.pathIndex >= e.path.size();
}

bool WesternShooterScene::TryStep(Enemy& e, const Vector2& dir, float radius, float speed, float dt)
{
    const Vector2 d = Normalized(dir, { 0.0f, 0.0f });
    const Vector2 next = { e.pos.x + d.x * speed * dt, e.pos.y + d.y * speed * dt };
    if (!GridNav::CanWalk(map_, e.pos, next, radius)) {
        return false;
    }
    e.pos = next;
    return true;
}

void WesternShooterScene::StrafeAround(Enemy& e, float radius, float speed, float ring, float dt)
{
    // 自機のまわりを回る。距離が ring からずれていたら、近づく・離れるを足す
    const Vector2 toPlayer = { playerPos_.x - e.pos.x, playerPos_.y - e.pos.y };
    const float dist = Length2D(toPlayer);
    const Vector2 d = Normalized(toPlayer, { 0.0f, 1.0f });
    const Vector2 side = { -d.y * e.strafeSign, d.x * e.strafeSign };
    const float radial = std::clamp((dist - ring) * 0.5f, -1.0f, 1.0f);
    const Vector2 dir = { side.x + d.x * radial, side.y + d.y * radial };
    if (!TryStep(e, dir, radius, speed, dt)) {
        e.strafeSign = -e.strafeSign; // 壁にぶつかった：逆向きへ
    }
}

void WesternShooterScene::SeparateEnemies()
{
    // 敵どうしが重ならないように押し離す（少しの重なりは許す）
    for (size_t i = 0; i < enemies_.size(); ++i) {
        Enemy& a = enemies_[i];
        if (!a.obj) {
            continue;
        }
        for (size_t j = i + 1; j < enemies_.size(); ++j) {
            Enemy& b = enemies_[j];
            if (!b.obj) {
                continue;
            }
            const float minDist = (RadiusOf(a.type) + RadiusOf(b.type)) * 0.9f;
            const Vector2 d = { b.pos.x - a.pos.x, b.pos.y - a.pos.y };
            const float dist = Length2D(d);
            if (dist >= minDist) {
                continue;
            }
            const Vector2 n = Normalized(d, { 1.0f, 0.0f });
            const float push = (minDist - dist) * 0.5f;
            a.pos.x -= n.x * push;
            a.pos.y -= n.y * push;
            b.pos.x += n.x * push;
            b.pos.y += n.y * push;
        }
    }
}

// ---------------------------------------------------------------- 見た目

void WesternShooterScene::TintEnemy(Enemy& e)
{
    float r = 0.9f, g = 0.3f, b = 0.3f; // ごろつき（ふだんの色）
    if (e.type == EnemyType::Gunman) {
        r = 0.3f; g = 0.5f; b = 0.9f;
    }
    if (showAiColors_) {
        if (e.state == AiState::Search) {
            r = 0.9f; g = 0.85f; b = 0.2f;
        }
        else if (e.type == EnemyType::Brute) {
            if (e.state == AiState::Rush) { r = 1.0f; g = 0.05f; b = 0.05f; }
            else if (e.state == AiState::Hold) { r = 1.0f; g = 0.6f; b = 0.1f; }
            else { r = 0.7f; g = 0.3f; b = 0.3f; }
        }
        else {
            if (e.state == AiState::Shoot) { r = 0.2f; g = 0.95f; b = 1.0f; }
            else if (e.state == AiState::Peek) { r = 0.55f; g = 0.8f; b = 1.0f; }
            else if (e.state == AiState::Hide) { r = 0.35f; g = 0.4f; b = 0.55f; }
            else { r = 0.3f; g = 0.5f; b = 0.9f; }
        }
    }
    SetTint(*e.obj, r, g, b);
}

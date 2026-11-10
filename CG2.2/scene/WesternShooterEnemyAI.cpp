// 西部シューティングの敵のAI（WesternShooterScene の一部。長くなるので別ファイルにしてある）
//
//   連携（AssignSquad。敵全体で毎フレーム1回）:
//     ・仲間を呼ぶ : 自機が見える敵が、近くの見えていない敵へ居場所を教える
//     ・役割分担   : ガンマンは「抑え」（陰から撃つ）と「回り込み」（抑えの向きから左右へ回って撃つ）に分かれる。抑えが倒れたら回り込みの1体が交代
//     ・交互に撃つ : 撃ち始めは kBurstStagger あけて順番に（全員が同時に顔を出さない）
//     ・まとめて突っ込む : ごろつきは取り巻いて待ち、数がそろい、ガンマンが撃っているときにまとめて突っ込む
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

    AssignSquad(shared, dt);

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

// ---------------------------------------------------------------- 連携

void WesternShooterScene::AssignSquad(AiShared& shared, float dt)
{
    burstGate_ = (std::max)(0.0f, burstGate_ - dt);
    callTimer_ -= dt;

    // 1. 仲間を呼ぶ：自機が見える敵が、近くの見えていない敵へ居場所を教える
    if (callTimer_ <= 0.0f) {
        callTimer_ = kCallInterval;
        for (const Enemy& s : enemies_) {
            if (!s.obj || !s.sees) {
                continue;
            }
            for (Enemy& o : enemies_) {
                if (!o.obj || &o == &s || o.sees) {
                    continue;
                }
                if (Length2D({ o.pos.x - s.pos.x, o.pos.y - s.pos.y }) > kCallRadius) {
                    continue;
                }
                o.lastSeen = playerPos_;
                if (o.state == AiState::Search) {
                    o.stateTimer = (std::max)(o.stateTimer, kSearchTime * 0.6f); // 教えてもらった場所を、あらためて探す
                }
            }
        }
    }

    // 2. ガンマンの役割：抑え kMaxSuppressors 体、残りは回り込み（左右に交互に）
    int suppressors = 0;
    int flankers = 0;
    float sideSum = 0.0f;
    for (const Enemy& e : enemies_) {
        if (!e.obj || e.type != EnemyType::Gunman) {
            continue;
        }
        if (e.role == Role::Suppress) {
            ++suppressors;
        }
        else if (e.role == Role::Flank) {
            ++flankers;
            sideSum += e.flankSide;
        }
    }
    for (Enemy& e : enemies_) {
        if (!e.obj || e.type != EnemyType::Gunman || e.role != Role::None) {
            continue;
        }
        if (suppressors < kMaxSuppressors) {
            e.role = Role::Suppress;
            ++suppressors;
        }
        else {
            e.role = Role::Flank;
            e.flankSide = sideSum <= 0.0f ? 1.0f : -1.0f;
            e.flankArrived = false;
            e.flankValid = false;
            sideSum += e.flankSide;
            ++flankers;
        }
    }
    // 抑えが倒れて足りないとき：自機に一番近い回り込み役が交代する
    if (suppressors < kMaxSuppressors && flankers > 0) {
        Enemy* best = nullptr;
        float bestDist = kCoverEngageDistance + 2.0f;
        for (Enemy& e : enemies_) {
            if (!e.obj || e.type != EnemyType::Gunman || e.role != Role::Flank) {
                continue;
            }
            const float d = Length2D({ playerPos_.x - e.pos.x, playerPos_.y - e.pos.y });
            if (d < bestDist) {
                bestDist = d;
                best = &e;
            }
        }
        if (best) {
            best->role = Role::Suppress;
            best->flankValid = false;
        }
    }

    // 抑え役のいる向き（回り込み役が「そこから左右へ」回るための基準）と、顔を出している数
    Vector2 sum{ 0.0f, 0.0f };
    int placed = 0;
    for (const Enemy& e : enemies_) {
        if (!e.obj || e.type != EnemyType::Gunman || e.role != Role::Suppress) {
            continue;
        }
        if (e.state == AiState::Shoot || e.state == AiState::Peek) {
            ++shared.suppressorsFiring;
        }
        if (e.state == AiState::Hide || e.state == AiState::Peek || e.state == AiState::Shoot) {
            sum.x += e.pos.x;
            sum.y += e.pos.y;
            ++placed;
        }
    }
    if (placed > 0) {
        const Vector2 centroid = { sum.x / placed, sum.y / placed };
        const Vector2 dir = { centroid.x - playerPos_.x, centroid.y - playerPos_.y };
        if (Length2D(dir) > 2.0f) {
            shared.front = Normalized(dir, { 0.0f, -1.0f });
            shared.hasFront = true;
        }
    }

    // 3. ごろつき：取り巻いた数がそろい、ガンマンが撃っているとき（または待ちくたびれたとき）にまとめて突っ込む
    int holders = 0;
    int brutes = 0;
    bool gunmen = false;
    for (const Enemy& e : enemies_) {
        if (!e.obj) {
            continue;
        }
        if (e.type == EnemyType::Brute) {
            ++brutes;
            if (e.state == AiState::Hold) {
                ++holders;
            }
        }
        else {
            gunmen = true;
        }
    }
    const int need = (std::max)(1, (std::min)(kRushGroup, brutes - shared.rushers));
    const bool grouped = holders >= need;
    const bool covered = !gunmen || shared.suppressorsFiring > 0 || rushWait_ > kRushMaxWait;
    shared.rushSignal = grouped && covered;
    if (holders > 0 && !shared.rushSignal) {
        rushWait_ += dt;
    }
    else {
        rushWait_ = 0.0f;
    }
}

void WesternShooterScene::UpdateEnemy(Enemy& e, float dt, AiShared& shared)
{
    const float radius = RadiusOf(e.type);
    const float dist = Length2D({ playerPos_.x - e.pos.x, playerPos_.y - e.pos.y });

    e.coverRetry = (std::max)(0.0f, e.coverRetry - dt);
    e.coverSwitchTimer = (std::max)(0.0f, e.coverSwitchTimer - dt);

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
        // 近づいたら、まず自機を取り巻く。突っ込むのは仲間がそろった合図（AssignSquad）が出てから
        if (e.sees && dist < kRushDistance) {
            EnterState(e, AiState::Hold, shared);
        }
        else {
            MoveToward(e, playerPos_, kBruteRadius, kBruteSpeed, dt);
        }
        break;

    case AiState::Hold:
        // 合図が出て、突っ込める枠（kMaxRushers 体まで）が空いていたら突っ込む。それまでは自機のまわりを回って待つ
        if (shared.rushSignal && shared.rushers < kMaxRushers) {
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
                const float r = kGunmanKeepDistance * 0.9f;
                Vector2 cand = { playerPos_.x + away.x * r, playerPos_.y + away.y * r }; // 回り込めないときは正面から
                if (e.role == Role::Flank) {
                    // 回り込み役：抑え役のいる向きから、自分の側（左か右）へ大きく回った位置。深く回れなければ浅く
                    const Vector2 base = shared.hasFront ? shared.front : away;
                    const float scales[3] = { 1.0f, 0.75f, 0.5f };
                    for (const float sc : scales) {
                        const Vector2 dir = Rotate(base, e.flankSide * kFlankAngle * sc);
                        const Vector2 p = { playerPos_.x + dir.x * r, playerPos_.y + dir.y * r };
                        if (GridNav::CanWalk(map_, p, p, kGunmanRadius) && GridNav::HasLineOfSight(map_, p, playerPos_)) {
                            cand = p;
                            break;
                        }
                    }
                }
                else {
                    const Vector2 dir = Rotate(away, e.flankAngle);
                    const Vector2 p = { playerPos_.x + dir.x * r, playerPos_.y + dir.y * r };
                    if (GridNav::CanWalk(map_, p, p, kGunmanRadius) && GridNav::HasLineOfSight(map_, p, playerPos_)) {
                        cand = p;
                    }
                }
                e.flankPos = cand;
                e.flankValid = true;
            }
            target = e.flankPos;
        }

        // 自機が近づいたら、まず陰へ向かう（陰から顔を出して撃つ）。陰が見つからなければ下の「立ち止まって撃つ」へ
        // 回り込み役は、狙う位置に着くまで陰に入らない（着いて一度撃ってから陰を使う）
        if (e.sees && dist <= kCoverEngageDistance && e.coverRetry <= 0.0f && (e.role != Role::Flank || e.flankArrived)) {
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
                e.flankArrived = true;
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
            // 撃ち始めは順番待ち（陰から出た敵は、顔を出す時点で順番を取っている）
            const bool first = e.shotsLeft == kGunmanBurst;
            if (!first || e.fromCover || burstGate_ <= 0.0f) {
                if (first && !e.fromCover) {
                    burstGate_ = kBurstStagger;
                }
                FireEnemyBullet(e);
                --e.shotsLeft;
                e.fireTimer = kGunmanShotInterval;
            }
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
            if (GridNav::HasLineOfSight(map_, playerPos_, e.coverPos)) {
                if (e.coverArrived) {
                    // 陰に着いているのに自機から見える：逃げ回らず、ここから顔を出して撃つ（撃ち終えたら、前の陰へ詰める）
                    e.stateTimer = (std::min)(e.stateTimer, 0.0f);
                }
                else if (e.coverRetry <= 0.0f && e.coverSwitchTimer <= 0.0f) {
                    // 向かっている途中で見えるようになった：選び直す（ただし立て続けには選び直さない）
                    if (ChooseCover(e, shared)) {
                        const Vector2 cover = e.coverPos;
                        EnterState(e, AiState::Hide, shared);
                        shared.covers.push_back(cover);
                        break;
                    }
                    e.coverRetry = kCoverRetry;
                }
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
            if (shared.shooters >= kMaxShooters || burstGate_ > 0.0f) {
                e.stateTimer = 0.15f; // 人数がいっぱい、または別の敵が顔を出したばかり：順番を待つ
            }
            else if (GridNav::FindPeek(map_, e.coverPos, playerPos_, kGunmanRadius, kPeekMaxDist, kSightRange, e.peekPos)) {
                EnterState(e, AiState::Peek, shared);
                burstGate_ = kBurstStagger; // 順番を取る
            }
            else if (e.coverSwitchTimer > 0.0f) {
                e.stateTimer = 0.2f; // 選び直したばかり：少し待つ
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
    q.approachWeight = kCoverApproachWeight; // 隠れながら、自機に近い陰へ詰めていく

    Vector2 cover{};
    if (!GridNav::FindCover(map_, q, shared.covers, cover)) {
        return false;
    }
    std::vector<Vector2> check; // そこへ行く道があるか確かめる
    if (!GridNav::FindPath(map_, e.pos, cover, kGunmanRadius, check)) {
        return false;
    }
    e.coverPos = cover;
    e.coverSwitchTimer = kCoverSwitchCooldown;
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
        e.flankArrived = false; // 見失ったら、あらためて回り込む
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
            else if (e.role == Role::Flank) { r = 0.3f; g = 0.9f; b = 0.45f; } // 回り込み中は緑
            else { r = 0.3f; g = 0.5f; b = 0.9f; }
        }
    }
    SetTint(*e.obj, r, g, b);
}

#pragma once
// 西部劇の全方位シューティング（本番用シーン）
//   操作: 移動 WASD / 矢印 / 左スティック   狙い マウス   撃つ マウス左 / J   やり直し R（ゲームオーバー・クリア後）
//   マップ: resources/western_stage.csv（マップエディタで作る）を読む。読めなければ内蔵の小さなマップで遊べる
//     壁・樽 : 自機・敵は通れない。弾は当たって消える
//     ドア   : 出現点。いまの区切り(area)のドアから、決まった数の敵が出てくる
//     区切り : area の敵を出し切って全員倒すまで通れない。倒すと次の area へ進む。最後まで倒すとクリア
//   敵 : ごろつき（突っ込んでくる） / ガンマン（距離を取って撃ってくる）。AI の中身は WesternShooterEnemyAI.cpp
//     視界 : 壁・樽の向こうは見えない。見失うと最後に見た場所を探し、しばらくして自機の居場所をたどる
//     道   : 壁・樽を避けて回り込む（GridNav::FindPath）
//     遮蔽 : ガンマンは撃ったあと、自機から見えない壁・樽の陰へ隠れ、出直す
//     連携 : 同時に突っ込めるごろつきは3体まで（残りは取り巻く）、同時に撃つガンマンは2体まで、ガンマンは左右に散る
//   弾・敵は GameObjectPool で使い回す（毎回作らない）。当たり判定は Collision の円どうし／円と四角
#include <string>
#include <vector>

#include "scene/Scene.h"
#include "../game/GameObject.h"
#include "../game/GameObjectPool.h"
#include "../game/DustBurst.h"
#include "../game/GridNav.h"
#include "../game/MapData.h"
#include "../game/MapRenderer.h"
#include "../game/SaloonDoors.h"
#include "../game/Tumbleweeds.h"
#include "camera/Camera.h"
#include "camera/CameraEffects.h"
#include "input/GameInput.h"
#include "math/Vector2.h"

class WesternShooterScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;

private:
    // 地面はXZ平面。動きと判定は (x, z) の2次元で扱い、高さ(y)は固定にする
    enum class EnemyType { Brute, Gunman };
    enum class State { Playing, Cleared, GameOver };

    struct Projectile
    {
        GameObject* obj = nullptr; // プールから借りているもの（返したら nullptr にする）
        Vector2 pos{ 0.0f, 0.0f };
        Vector2 vel{ 0.0f, 0.0f };
        float life = 0.0f;
    };
    // 敵の行動。ごろつき: Advance→Rush/Hold。ガンマン: Advance→Hide（陰へ）→Peek（顔を出す）→Shoot→Hide…（陰が無ければ Advance→Shoot）。どちらも見失うと Search
    enum class AiState { Advance, Rush, Hold, Shoot, Hide, Peek, Search };

    struct Enemy
    {
        EnemyType type = EnemyType::Brute;
        GameObject* obj = nullptr; // プールから借りているもの（返したら nullptr にする）
        Vector2 pos{ 0.0f, 0.0f };
        int hp = 1;
        float fireTimer = 0.0f;

        // --- AI ---
        AiState state = AiState::Advance;
        float stateTimer = 0.0f;       // Search の残り時間 / Hide の移動の制限・隠れて待つ時間
        bool sees = false;             // いま自機が見えているか
        bool hunting = true;           // 見失ったあとも自機の居場所をたどる（出現直後と、探し終えたあと）
        Vector2 lastSeen{ 0.0f, 0.0f };// 自機を最後に見た場所
        std::vector<Vector2> path;     // 歩いていく曲がり角の点
        size_t pathIndex = 0;
        float repathTimer = 0.0f;
        Vector2 coverPos{ 0.0f, 0.0f };// ガンマン: 隠れ場所
        bool coverArrived = false;
        Vector2 peekPos{ 0.0f, 0.0f }; // ガンマン: 陰から顔を出して撃つ場所
        bool fromCover = false;        // ガンマン: いまの Shoot は陰から顔を出して撃っている（立ち止まって撃つ）
        float coverCheck = 0.0f;       // ガンマン: 隠れ場所が自機から見えていないか、次に確かめるまでの時間
        float coverRetry = 0.0f;       // ガンマン: 陰を探し直せるようになるまでの時間
        Vector2 flankPos{ 0.0f, 0.0f };// ガンマン: 撃つために向かう場所（自機の左右に散る）
        bool flankValid = false;
        float flankAngle = 0.0f;       // ガンマン: 自機から見て、いまの位置からどれだけ回り込むか
        float strafeSign = 1.0f;       // 横移動の向き（+1 / -1）
        int shotsLeft = 0;
    };

    // 敵AIが1フレームのあいだ共有する数（同時に突っ込める数・撃てる数の管理）
    struct AiShared
    {
        int rushers = 0;
        int shooters = 0;
        std::vector<Vector2> covers; // 他のガンマンが使っている隠れ場所
    };
    // ドア1つぶんの「あと何体出すか」（map_.spawns と同じ並び・同じ数）
    struct SpawnState
    {
        int remaining = 0;
        float timer = 0.0f;
    };

    // --- 1フレームの流れ（Update から順に呼ぶ） ---
    void LoadMap();                     // マップを読む（失敗したら内蔵マップ）
    void Reset();                       // 最初から（やり直し）
    void UpdateAim();                   // マウスが指している地面の点を求める
    void UpdatePlayer(float dt);
    void UpdateSpawn(float dt);
    void UpdateEnemies(float dt);       // WesternShooterEnemyAI.cpp
    void UpdateProjectiles(float dt);
    void ResolveHits();                 // 自機の弾 × 敵
    void RemoveDead();                  // 返したものを配列から取り除く
    void UpdateArea();                  // いまの区切りを倒し切ったら、次へ進める
    void UpdateCamera(float dt);        // 自機を追って、カメラを合わせる
    void ApplyCamera();                 // カメラの位置を決める（揺れを足す）
    void UpdateVisuals();               // カメラと見た目の行列

    // --- 敵のAI（WesternShooterEnemyAI.cpp） ---
    void UpdateEnemy(Enemy& e, float dt, AiShared& shared);
    void UpdateBrute(Enemy& e, float dt, AiShared& shared);
    void UpdateGunman(Enemy& e, float dt, AiShared& shared);
    void SeparateEnemies();
    void MoveToward(Enemy& e, const Vector2& target, float radius, float speed, float dt);
    bool FollowPath(Enemy& e, float radius, float speed, float dt);
    bool TryStep(Enemy& e, const Vector2& dir, float radius, float speed, float dt);
    void StrafeAround(Enemy& e, float radius, float speed, float ring, float dt);
    void EnterState(Enemy& e, AiState next, AiShared& shared);
    bool ChooseCover(Enemy& e, const AiShared& shared);
    void TintEnemy(Enemy& e);
    static float RadiusOf(EnemyType type);

    // --- 壁・区切り ---
    void PushOutOfWalls(Vector2& pos, float radius) const;
    void PushOutOfGates(Vector2& pos, float radius) const;
    bool IsGateClosed(size_t index) const;
    bool IsSolidAt(const Vector2& pos) const;

    // --- 出す ---
    void FirePlayerBullet();
    bool SpawnEnemy(size_t spawnIndex);     // 酒場のドア（出現点）から1体出す。扉が揺れ、砂ぼこりが立つ
    void FireEnemyBullet(const Enemy& enemy);

    // --- ダメージ・返す ---
    void DamagePlayer();
    void KillEnemy(Enemy& enemy);
    GameObjectPool& PoolOf(EnemyType type);

    Camera camera_;
    CameraEffects fx_;      // 画面の揺れ・ヒットストップ
    GameInput input_;

    MapData map_;
    MapRenderer mapView_;
    SaloonDoors doors_;     // 出現点に置く酒場のスイングドア
    DustBurst dust_;        // 砂ぼこり
    Tumbleweeds weeds_;     // 風に転がるタンブルウィード（見た目だけ）
    std::string mapMessage_;             // マップの読み込み結果（画面に出す）
    bool mapFromFile_ = false;
    bool mapComplete_ = true;            // 壁・樽の見た目が全部作れたか

    std::vector<SpawnState> spawnStates_;
    int currentArea_ = 0;                // いま戦っている区切り（出現点が無ければ kNoArea）
    Vector2 cameraFocus_{ 0.0f, 0.0f };  // カメラが見ている地面の点

    GameObject player_;
    GameObject reticle_;            // マウスが指している地面の点に置く目印
    Vector2 mousePixel_{ 0.0f, 0.0f };   // ゲーム画面のピクセル座標
    bool mouseInside_ = false;           // マウスがゲーム画面の中にあるか
    Vector2 aimPoint_{ 0.0f, 0.0f };     // マウスが指している地面の点 (x, z)
    bool aimValid_ = false;              // aimPoint_ が求まっているか
    Vector2 playerPos_{ 0.0f, 0.0f };
    Vector2 facing_{ 0.0f, 1.0f }; // 撃つ向き（長さ1）
    int playerHp_ = 0;
    float invincible_ = 0.0f;      // ダメージを受けたあとの無敵時間
    float fireTimer_ = 0.0f;

    GameObjectPool playerBulletPool_;
    GameObjectPool enemyBulletPool_;
    GameObjectPool brutePool_;
    GameObjectPool gunmanPool_;

    std::vector<Projectile> playerBullets_;
    std::vector<Projectile> enemyBullets_;
    std::vector<Enemy> enemies_;

    int score_ = 0;
    bool showAiColors_ = true;   // 敵の行動を色で見せる（ImGuiで切り替え）
    State state_ = State::Playing;
};

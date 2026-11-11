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
#include "base/ShaderTypes.h"
#include "3d/PostProcess.h"
#include "3d/RenderLook.h"
#include "camera/FreeCamera.h"
#include "core/PauseMenu.h"
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
#include <memory>

class WesternShooterScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;

    // 起動済みのシーンを、遊び直せる状態に戻す（Rキーのやり直しと同じ。マップのファイルが変わっていたら読み直す）
    void Restart();

    // このシーンにいる間だけ、共通の光源（向き・色・影の設定）を西部の夕方用にする。出るときに元へ戻す
    void BeginLook();
    void EndLook();

    // 起動時の準備を細かく分けて1回に1段階ずつ進める（ロード画面を動かしたまま読み込むため）。全部終わったら true
    bool InitStep();
    static constexpr int kInitSteps = 8;
    int GetInitStep() const { return initStep_; }

private:
    // 地面はXZ平面。動きと判定は (x, z) の2次元で扱い、高さ(y)は固定にする
    enum class EnemyType { Brute, Gunman, Boss };
    enum class State { Playing, Cleared, GameOver };

    struct Projectile
    {
        GameObject* obj = nullptr; // プールから借りているもの（返したら nullptr にする）
        Vector2 pos{ 0.0f, 0.0f };
        Vector2 vel{ 0.0f, 0.0f };
        float life = 0.0f;
        bool deadEye = false;      // デッドアイで撃った弾（ゲージは増やさない）
        bool grazed = false;       // 自機をかすめてゲージを増やした敵の弾（1発につき1回）
    };
    // 敵の行動。ごろつき: Advance→Rush/Hold。ガンマン: Advance→Hide（陰へ）→Peek（顔を出す）→Shoot→Hide…（陰が無ければ Advance→Shoot）。どちらも見失うと Search
    enum class AiState { Advance, Rush, Hold, Shoot, Hide, Peek, Search };
    enum class Role { None, Suppress, Flank };
    // ボスの行動。Intro（登場）→ Pace（歩いて間合い）→ Telegraph（構え・線で予告）→ Fire（撃つ）→ Recover（隙。ここで当てると2倍）→ Pace…
    enum class BossState { Intro, Pace, Telegraph, Fire, Recover };
    enum class BossAttack { Fan, Burst };   // 扇状 / まっすぐ連射   // ガンマンの役割：抑え（陰から撃つ）／ 回り込み

    struct Enemy
    {
        int id = 0;                // 敵ごとの番号（デッドアイのマークが、倒された敵を指し続けないように）
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
        float coverSwitchTimer = 0.0f; // ガンマン: 隠れ場所を選び直せるようになるまでの時間（うろうろ防止）
        float coverRetry = 0.0f;       // ガンマン: 陰を探し直せるようになるまでの時間
        Vector2 flankPos{ 0.0f, 0.0f };// ガンマン: 撃つために向かう場所（自機の左右に散る）
        bool flankValid = false;
        float flankAngle = 0.0f;       // ガンマン: 自機から見て、いまの位置からどれだけ回り込むか
        float strafeSign = 1.0f;       // 横移動の向き（+1 / -1）
        Role role = Role::None;        // ガンマン: 役割（AssignSquad が決める）
        float flankSide = 1.0f;        // ガンマン: 回り込む側（+1 / -1）
        bool flankArrived = false;     // ガンマン: 回り込み役が、狙う位置に着いたか（着くまでは陰に入らない）
        int shotsLeft = 0;
        Vector2 aimDir{ 0.0f, -1.0f }; // ガンマン: 撃つ向き（狙いの線の向き）
        bool aimLocked = false;        // ガンマン／ごろつき: 撃つ（突っ込む）直前で、向きが固定されている
        int rushPhase = 0;             // ごろつき: Rush の段階（0=構え 1=突進 2=息切れ）
        float rushTimer = 0.0f;
        float hitFlash = 0.0f;         // 弾が当たった直後、白く光らせる残り時間

        // --- 動き（歩き・やられ） ---
        Vector2 prevPos{ 0.0f, 0.0f }; // 1フレーム前の位置（歩く速さを調べる）
        bool prevValid = false;
        float walkPhase = 0.0f;        // 歩きの周期（足の運びのかわり）
        float walkAmt = 0.0f;          // 歩きのゆれの大きさ（0=立っている 1=普通に歩く）
        float hitReact = 0.0f;         // 弾が当たった直後のけぞる強さ（当たった瞬間が 1。0 まで戻る）
        Vector2 hitReactDir{ 0.0f, 1.0f }; // 当たった弾の進む向き
        float animLock = 0.0f;         // 撃つ・やられのアニメーションを流している残り時間（その間は歩きなどに切り替えない）

        // --- ボス ---
        BossState bossState = BossState::Intro;
        BossAttack bossAttack = BossAttack::Fan;
        float bossTimer = 0.0f;        // いまの行動の残り時間
        int bossCount = 0;             // 何回構えたか（扇と連射を交互にする）

        // --- 登場の飛び出し（spawnT > 0 の間は空中。AIは止まり、弾も当たらない） ---
        float spawnT = 0.0f;           // 着地までの残り時間
        float spawnDur = 1.0f;         // 飛ぶ時間の全体
        Vector2 spawnFrom{ 0.0f, 0.0f };
        Vector2 spawnTo{ 0.0f, 0.0f }; // 着地点
        float flightPeak = 3.0f;       // 飛ぶ高さ
        float flightY = 0.0f;          // いまの高さ（見た目）

        // --- 整列のポーズ（フェーズ登場の演出だけ） ---
        int poseIndex = -1;            // 列の左から何番目か（-1 = 整列に参加しない）
        bool posed = false;
        float poseT = 0.0f;            // ポーズを始めてからの時間
    };

    // 登場演出（カットシーン）。フェーズの始まりは敵が集団で飛び出す／ボスは専用の長い演出。この間、ゲームの時間は止まる
    enum class CutsceneKind { None, Phase, Boss };
    struct Pillar   // 着地点に立つ色つきの光の柱（戦隊モノ風）
    {
        Vector2 pos{ 0.0f, 0.0f };
        float r = 1.0f, g = 1.0f, b = 1.0f;
        float scale = 1.0f;
        float age = 0.0f;
    };
    struct Cutscene
    {
        CutsceneKind kind = CutsceneKind::None;
        float t = 0.0f;
        int phase = 1;                  // 表示用のフェーズ番号（1始まり）
        int mix = 2;                    // 0=ごろつきだけ 1=ガンマンだけ 2=両方
        std::vector<size_t> doors;      // 出すドア番号（同じドアが何度も出ることがある。左のドアから順）
        bool both = false;              // 北側と南側の両方のドアから出る（一列に並ぶ）
        std::vector<Vector2> slots;     // 整列する位置（左から順。doors と同じ並び）
        size_t nextDoor = 0;
        float landedAt = -1.0f;         // 全員が着地した時刻
        float leaveAt = -1.0f;          // カメラが自機へ戻り始める時刻
        float endAt = -1.0f;            // 演出が終わる時刻
        bool bossSpawned = false;
        float flash = 0.0f;             // 画面が白く光る強さ
        float yaw0 = 0.0f;              // カメラの向き：敵が着地する側（自機の側）から、ドアを正面に見る
        Vector2 startFocus{ 0.0f, 0.0f };
        Vector2 holdFocus{ 0.0f, 0.0f };
        float holdHeight = 0.0f, holdBack = 0.0f, holdYaw = 0.0f, holdLookY = 0.0f;
    };

    // 敵AIが1フレームのあいだ共有する数（同時に突っ込める数・撃てる数の管理）
    struct AiShared
    {
        int rushers = 0;
        int shooters = 0;
        std::vector<Vector2> covers; // 他のガンマンが使っている隠れ場所
        bool rushSignal = false;     // ごろつきがまとめて突っ込んでよい合図
        int suppressorsFiring = 0;   // いま顔を出している抑え役の数
        Vector2 front{ 0.0f, -1.0f };// 自機から見て、抑え役がいる向き
        bool hasFront = false;
    };
    // 倒された敵：吹っ飛びながら回って縮み、消える（見た目だけ）
    struct Corpse
    {
        EnemyType type = EnemyType::Brute;
        GameObject* obj = nullptr; // 元の敵のオブジェクトをそのまま使う（消えるときにプールへ返す）
        Vector2 pos{ 0.0f, 0.0f };
        Vector2 vel{ 0.0f, 0.0f };
        float age = 0.0f;
        float spinSign = 1.0f;
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
    float UpdateDeadEye();              // デッドアイの状態を進め、ゲームの時間の速さ（1=ふつう）を返す
    void TryMark();                     // デッドアイ中：狙っている敵にマークをつける
    void FireDeadEyeShot(int enemyId);  // マークした敵へ1発
    void UpdateSpawn(float dt);
    void UpdateEnemies(float dt);       // WesternShooterEnemyAI.cpp
    void UpdateProjectiles(float dt);
    void ResolveHits();                 // 自機の弾 × 敵
    void RemoveDead();                  // 返したものを配列から取り除く
    void UpdateArea();                  // いまの区切りを倒し切ったら、次へ進める
    void UpdateCamera(float dt);        // 自機を追って、カメラを合わせる
    void ApplyCamera();                 // カメラの位置を決める（揺れを足す）
    void UpdateVisuals();               // カメラと見た目の行列
    void PrepareRender();               // 描く前の準備（光・行列・影）。ポーズ中もこれだけは進める
    bool UpdatePause(bool canPause);    // ポーズの開閉とメニューの処理。ゲームを止めるべきときは true
    const Camera& ActiveCamera() const { return freeCam_ ? static_cast<const Camera&>(freeCamera_) : camera_; }
    Vector3 WeatherFocus() const;       // 雨を降らせる中心（ふだんは見ている点、自由カメラ中はカメラの下）

    // --- 敵のAI（WesternShooterEnemyAI.cpp） ---
    void AssignSquad(AiShared& shared, float dt);   // 仲間を呼ぶ・役割を決める・突っ込む合図を出す
    void UpdateEnemy(Enemy& e, float dt, AiShared& shared);
    // --- 登場演出（WesternShooterScene.cpp の「登場演出」の節） ---
    void StartPhaseCutscene();          // フェーズの始まり：敵が集団で飛び出す
    void StartBossCutscene();           // ボスの登場
    void UpdateCutscene();              // スキップの受付と、1フレームぶんを進める
    void StepCutscene(float dt);
    void EndCutscene();
    void DrawCutsceneOverlay();         // 帯・文字・フラッシュ
    int PhaseKind() const;              // いまのフェーズの敵の組み合わせ（0=ごろつき 1=ガンマン 2=両方）
    EnemyType PickEnemyType() const;

    void UpdateSpawnFlight(Enemy& e, float dt);                    // 飛び出して着地するまで（WesternShooterEnemyAI.cpp）
    void LandEnemy(Enemy& e);                                      // 着地の衝撃波・画面揺れ・踏みつぶし
    void UpdateBoss(Enemy& e, float dt);                           // ボスの行動（WesternShooterEnemyAI.cpp）
    void EnterBossState(Enemy& e, BossState next);
    void FireBossBullet(const Vector2& from, const Vector2& dir);
    bool SpawnBoss();                                              // 最後の区切りを倒したあと、ボスを出す
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
    void AnimateEnemy(Enemy& e, float dt); // 歩きのゆれ・のけぞり・構えの動き
    static float RadiusOf(EnemyType type);

    // --- 壁・区切り ---
    void PushOutOfWalls(Vector2& pos, float radius) const;
    void PushOutOfGates(Vector2& pos, float radius) const;
    bool IsGateClosed(size_t index) const;
    bool IsSolidAt(const Vector2& pos) const;

    // --- 出す ---
    void FirePlayerBullet(const Vector2& dir, float speed, bool deadEye);
    bool SpawnEnemy(size_t spawnIndex);     // 酒場のドア（出現点）から1体出す。扉が揺れ、砂ぼこりが立つ
    void FireEnemyBullet(const Enemy& enemy);

    // --- ダメージ・返す ---
    void DamagePlayer();
    void KillEnemy(Enemy& enemy, const Vector2& hitDir);   // 倒す。倒れた敵は Corpse になって吹っ飛ぶ
    void UpdateCorpses(float dt);
    void EmitBlood(const Vector2& pos, const Vector2& dir, int count);
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
    // 見た目（影・環境光）。ImGuiの「画質」で変えられる
    void RenderShadowPass();  // 影の深度テクスチャを描く（更新のあと、描画より前）。影を落とす物を列挙するだけ
    PauseMenu pause_;               // ポーズ画面（エンジン側の部品）
    FreeCamera freeCamera_;         // ポーズ中のデバッグ用の自由カメラ
    bool freeCam_ = false;          // 自由カメラで見ている間 true（メニューは隠れる）
    bool freeLooking_ = false;      // 右ドラッグで向きを変えている最中
    Vector2 freeMouse_{ 0.0f, 0.0f };
    RenderLook look_;               // 見た目一式（影・時間帯・環境光・ポストエフェクト）。エンジン側の機能

    int initStep_ = 0;                   // InitStep() で次にやる段階
    long long mapStamp_ = 0;             // 最後に読んだときのマップファイルの更新時刻（変わったら読み直す）
    bool mapComplete_ = true;            // 壁・樽の見た目が全部作れたか

    std::vector<SpawnState> spawnStates_;
    std::vector<Corpse> corpses_;

    // --- 一発勝負・デッドアイ・回避 ---
    //   ふつうの射撃 : 弾は同時に1発だけ。弾が消えてから、さらに装填（kReloadTime）が終わるまで次を撃てない
    //   デッドアイ   : ゲージがあるとき、ボタンを押している間スローになり、敵をクリックでマーク（最大 kMaxMarks）。
    //                  離す（またはゲージ切れ）と、マークした敵へ連射する。ゲージは当てる・倒すと増える
    //   回避         : Space / A で転がる。転がっている間は弾が当たらない（クールダウンあり）
    std::vector<int> marks_;             // マークした敵の番号（同じ敵に2回つけてよい）
    bool deadEyeActive_ = false;         // スローで狙っている間
    bool volley_ = false;                // マークした敵へ連射している間
    float volleyTimer_ = 0.0f;
    size_t volleyIndex_ = 0;
    float deadEyeGauge_ = 0.0f;          // 0〜1
    float rollTimer_ = 0.0f;             // 転がっている残り時間
    float rollCooldown_ = 0.0f;
    Vector2 rollDir_{ 0.0f, 1.0f };
    int nextEnemyId_ = 1;
    GameObjectPool markPool_;            // マークの目印
    GameObjectPool aimPool_;             // ガンマンの狙いの線（細長い箱）
    float slowTimer_ = 0.0f;             // 最後の1体を倒した直後のスローモーションの残り時間（実時間）
    int tracerTick_ = 0;                 // 弾の尾を2フレームに1回出すための数え
    float burstGate_ = 0.0f;             // 敵が撃ち始められるようになるまでの時間（交互に撃たせる）
    float rushWait_ = 0.0f;              // ごろつきが突っ込む合図を待っている時間
    float callTimer_ = 0.0f;             // 仲間に居場所を教える間隔
    int currentArea_ = 0;                // いま戦っている区切り（出現点が無ければ kNoArea）
    Vector2 cameraFocus_{ 0.0f, 0.0f };  // カメラが見ている地面の点

    GameObject player_;             // 自機（ブロックで作った人型。resources/ws_man_player.obj）
    GameObject revolver_;           // 自機が右手に持つリボルバー（resources/ws_revolver.obj）。撃つと反動で跳ね上がる
    Vector2 prevPlayerPos_{ 0.0f, 0.0f }; // 1フレーム前の自機の位置
    float playerWalkPhase_ = 0.0f;  // 自機の歩きの周期
    float playerWalkAmt_ = 0.0f;    // 自機の歩きのゆれの大きさ
    float playerAnimLock_ = 0.0f;   // 撃つ・やられのアニメーションを流している残り時間
    float hurt_ = 0.0f;             // 撃たれた直後ののけぞり（撃たれた瞬間が 1。0 まで戻る）
    float perfUpdateMs_ = 0.0f;     // 処理時間の目安（デバッグ表示用）
    float perfMapMs_ = 0.0f;
    float perfDrawMs_ = 0.0f;
    float perfMapDrawMs_ = 0.0f;
    std::vector<Vector2> occlusionTargets_; // 建物を半透明にするための、自機・敵の位置
    Vector3 handPos_{ 0.0f, 1.0f, 0.0f };   // 右手の位置（銃を置く場所）
    float recoil_ = 0.0f;           // 撃った反動（撃った瞬間が 1。0 まで戻る）
    GameObject playerMark_;         // 自機の足もとの印（敵と見分けるため）
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
    GameObjectPool bossPool_;

    std::vector<Projectile> playerBullets_;
    std::vector<Projectile> enemyBullets_;
    std::vector<Enemy> enemies_;

    int score_ = 0;
    Cutscene cutscene_;
    std::vector<Pillar> pillars_;
    std::vector<int> areaOrder_;         // 出現点がある区切りの番号を小さい順に（フェーズの順番）
    int phaseIndex_ = 0;                 // いまのフェーズ（0始まり）
    // カメラの構え。ふだんは見下ろし（17 / 10）で、演出のあいだだけ動かす
    float shotHeight_ = 17.0f;
    float shotBack_ = 10.0f;
    float shotYaw_ = 0.0f;               // 見ている点のまわりを回る角度
    float shotLookY_ = 0.0f;             // 見ている点の高さ
    bool bossSpawned_ = false;           // ボスを出したか（倒したらクリア）
    Vector2 lastDoorPos_{ 0.0f, 0.0f };   // 最後に敵が出てきたドア（ボスはここから出る）
    float endTimer_ = 0.0f;      // ゲームオーバー・クリアになってからの時間（結果画面へ進むまで）
    bool showAiColors_ = true;   // 敵の行動を色で見せる（ImGuiで切り替え）
    State state_ = State::Playing;
};

/// <summary>
/// 西部シューティングの「使い回し」窓口。重い準備（モデル・弾や敵のプール・マップの見た目）は
/// アプリ起動時に Preload() で一度だけ行い、タイトル→ゲーム、リトライでは作り直さずに使い回す。
/// 使い方: sceneManager->ChangeScene<WesternShooterEntry&gt;();（WesternShooterScene を直接 ChangeScene しないこと）
/// </summary>
class WesternShooterEntry : public Scene
{
public:
    static void Preload(SceneContext* context); // 起動時に1回だけ、全部作る
    static void Release();                      // アプリ終了時に解放する（GPUより先に呼ぶ）
    // 起動時の読み込みを1段階だけ進める（ロード画面から毎フレーム呼ぶ）。全部終わったら true
    static bool PreloadStep(SceneContext* context);
    static bool IsReady();                      // 読み込みが終わっているか
    static float Progress();                    // 読み込みの進み具合（0〜1）
    static void DrawLoading(float screenW, float screenH); // 「NOW LOADING」を出す（読み込み中だけ。エンジンの LoadQueue の表示）

    void Initialize() override;                 // まだ作っていなければ作る。あれば Restart() だけ
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
    void Finalize() override; // 光源だけ元に戻す。中身は次に遊ぶときまで残す

private:
    static std::unique_ptr<WesternShooterScene>& Shared();
};

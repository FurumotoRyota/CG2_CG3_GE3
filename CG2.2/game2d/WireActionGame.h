#pragma once
// ワイヤーアクション（2D横スクロール）のゲームロジック
// エンジン（DirectX / ImGui / DirectInput）には依存しない。入力は InputState で渡し、
// 描画は呼び出し側（WireActionScene）が公開データを読んで行う。
// 時間は 1/60 秒固定ステップ（速度・加速は「1フレームあたり」の値）。
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace wa
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };
    inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
    inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
    inline Vec2 operator*(Vec2 a, float s) { return { a.x * s, a.y * s }; }
    inline Vec2 operator/(Vec2 a, float s) { return { a.x / s, a.y / s }; }
    inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
    inline float Length(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
    inline Vec2 Normalize(Vec2 a)
    {
        const float l = Length(a);
        return l > 1e-6f ? a / l : Vec2{ 0.0f, 0.0f };
    }

    // ジャンプ・移動の調整値（1フレームあたり）
    struct JumpParams
    {
        float runSpeed = 0.26f;
        float jumpSpeed = 0.60f;
        float gravityScaleUp = 0.8f;
        float gravityScaleDown = 0.7f;
        float maxFallSpeed = 0.30f;
        float apexFloat = 0.55f;
        float apexThreshold = 0.10f;
        float groundAccel = 0.05f;
        float groundFriction = 0.86f;
        float airAccel = 0.03f;
        float airDrag = 0.992f;
        float jumpCut = 0.6f;
    };

    // ワイヤーの調整値
    struct WireParams
    {
        float range = 12.0f;
        float minRange = 1.5f;
        float lockRadius = 1.8f;
        float blockAssistRadius = 1.3f;
        float shootTime = 0.04f;
        float shootSpeed = 0.9f;
        float shootDamping = 0.985f;
        float pullSpeed = 0.55f;
        float pullAccel = 0.045f;
        float pullMaxTime = 1.0f;
        float exitSpeedCap = 0.35f;
        float hitKeepRatio = 0.35f;
        float hitBounceUp = 0.30f;
        float hitBounceForward = 0.16f;
        float swingGravity = 0.028f;
        float swingDrag = 0.998f;
        float swingForce = 0.008f;
        float swingMaxSpeed = 0.6f;
        float swingReleaseBoost = 1.12f;
        float swingBoostMax = 0.6f;
        float releaseBufferKeep = 0.85f;
        float releaseKick = 0.10f;
        float attachClearance = 0.8f;
        float attachGroundRatio = 0.65f;
        float attachMinLength = 2.5f;
        float attachAssistSpeed = 0.12f;
        float attachHop = 0.22f;
        float ropeBlockTime = 0.8f;
        float momentumTime = 0.6f;
        float hitMomentumTime = 0.35f;
        float floatGravityScale = 0.55f;
        float momentumAttenuation = 0.975f;
        float cooldown = 0.10f;
        float fireBufferTime = 0.15f;
        int attachShrinkFrames = 18;
        int attachGroundShrinkFrames = 6;
    };

    // 保存する設定（POD。そのまま fwrite / fread する）
    struct Settings
    {
        uint32_t version = 1;
        JumpParams jump;
        WireParams wire;
    };

    // 1フレーム分の入力
    struct InputState
    {
        bool left = false;
        bool right = false;
        bool jumpPressed = false; // 押した瞬間
        bool jumpHeld = false;
        bool firePressed = false; // ワイヤーボタンを押した瞬間
        bool fireHeld = false;
        Vec2 aim;                 // 照準のワールド座標
    };

    struct IndexSet
    {
        int x = 0;
        int y = 0;
    };

    struct Rect
    {
        float left = 0, right = 0, top = 0, bottom = 0;
    };

    struct Enemy
    {
        int id = 0;
        Vec2 pos;
        Vec2 vel;
        float homeX = 0.0f;
        int dir = -1;          // 進行方向 / 盾の向き
        bool shield = false;
        bool canMove = true;
        bool turnAtLedge = true;
        float patrolRange = 3.0f;
        float speed = 0.04f;
        bool dying = false;
        float dyingTimer = 0.0f; // 死亡演出の経過（秒）
        bool dead = false;       // 演出終了（削除対象）
        float guardFlash = 0.0f; // 盾で弾いた演出
        float walkTime = 0.0f;
        bool onGround = false;
    };

    struct Particle
    {
        Vec2 pos;
        Vec2 vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float size = 0.2f;
        float r = 1, g = 1, b = 1;
    };

    enum class Phase { Play, Clear, Death };
    enum class WireState { None, Shoot, Pull, Swing };
    enum class CandType { None, Block, Enemy };

    struct Candidate
    {
        CandType type = CandType::None;
        Vec2 pos;
        int enemyId = -1;
    };

    struct Player
    {
        Vec2 pos;
        Vec2 vel;
        bool onGround = false;
        bool jumpArc = false;
        float coyote = 0.0f;
        int lr = 1;
        int hp = 3;
        float invincible = 0.0f;
        float wireMomentum = 0.0f;
        bool attacking = false;
    };

    struct WireRuntime
    {
        WireState state = WireState::None;
        Candidate cand;       // 今の照準候補
        Vec2 target;          // 掛け先（ブロックは固定、敵は毎フレーム更新）
        int enemyId = -1;
        float timer = 0.0f;
        float duration = 0.0f;
        float cooldown = 0.0f;
        float fireBuffer = 0.0f;
        bool hold = false;
        // pull
        float pullSpeed = 0.0f;
        float pullTime = 0.0f;
        int stall = 0;
        // swing
        Vec2 anchor;
        float ropeLength = 0.0f;
        float shrinkFrom = 0.0f;
        float shrinkTo = 0.0f;
        int shrinkFrame = 0;
        int shrinkFrames = 0;
        float swingPeak = 0.0f;
        float blockTimer = 0.0f;
        bool fromGround = false;
    };

    class Game
    {
    public:
        static constexpr int kW = 200;
        static constexpr int kH = 20;
        static constexpr float kGravity = 0.035f;
        static constexpr float kPlayerW = 0.8f;
        static constexpr float kPlayerH = 0.8f;
        static constexpr float kFallDeathY = -3.0f;

        // ---- ステージ ----
        void Reset();                                  // 今のマップで最初からやり直す
        void LoadDefaultStage();                       // 組み込みのステージを作って Reset
        bool LoadStageCsv(const std::string& path);    // 0 空 / 1 ブロック / P 開始 / E 敵 / S 盾の敵 / G ゴール
        bool SaveStageCsv(const std::string& path) const;

        // ---- 更新 ----
        void SetView(float ppu, float screenW, float screenH);
        void Update(const InputState& input);

        // ---- 座標 ----
        Vec2 ScreenToWorld(float sx, float sy) const;
        Vec2 WorldToScreen(Vec2 w) const;
        IndexSet ToIndex(Vec2 p) const;
        Vec2 CellCenter(int ix, int iy) const { return { static_cast<float>(ix), static_cast<float>(kH - 1 - iy) }; }
        Rect CellRect(int ix, int iy) const;
        bool IsBlockIdx(int ix, int iy) const;
        bool IsBlockAt(Vec2 p) const;
        bool SegmentBlocked(Vec2 a, Vec2 b, float ignoreEnd) const;
        void SetBlock(int ix, int worldY, bool solid); // テスト・ステージ生成用（worldY は下から数える）

        // ---- 公開データ（描画・UI用） ----
        Settings settings;
        Player player;
        WireRuntime wire;
        std::vector<Enemy> enemies;
        std::vector<Particle> particles;
        Phase phase = Phase::Play;
        Vec2 cam;
        Vec2 spawn{ 3.0f, 1.9f };
        bool wireEnabled = true; // false ならワイヤーを使えない（踏みつけだけで敵を倒す）
        bool hasGoal = false;
        Vec2 goal;
        int hitStop = 0;       // 残りフレーム
        float killZoom = 0.0f; // 撃破ズームの経過（秒）。0 なら無し
        float time = 0.0f;
        float ppu = 48.0f;
        float screenW = 1280.0f;
        float screenH = 720.0f;
        int killCount = 0;

        bool IsAttacking() const { return wire.state == WireState::Pull; }
        Vec2 WireTip() const;  // ロープの先端（描画用）
        const Enemy* FindEnemy(int id) const;

    private:
        struct Collision
        {
            bool top = false;
            bool ground = false;
            bool wall = false;
            Vec2 move;
        };

        Enemy* FindEnemyMut(int id);
        void CheckMapCollision(Vec2 pos, Collision& c) const;
        Collision MoveWithCollision(Vec2 v);
        void ApplyMoveResult(const Collision& c);
        bool ProbeGround() const;

        void UpdateAim(const InputState& in);
        void UpdatePlayer(const InputState& in);
        void RootUpdate(const InputState& in);
        void TryStartWire();
        void StartWire(const Candidate& cand);
        void ShootUpdate(const InputState& in);
        void PullUpdate();
        void AttachSwing();
        void SwingUpdate(const InputState& in);
        void ReleaseSwing(bool kick, bool boost);
        void FinishWire(float momentumTime);
        void HitEnemy(Enemy& e);
        void TakeDamage(float fromX);
        void KillEnemy(Enemy& e);

        void UpdateEnemies();
        void UpdateEnemy(Enemy& e);
        void ResolveEnemyCollisions();
        void CheckPlayerEnemyContact();
        void UpdateCamera();
        void ApplyCameraBounds();
        void UpdateParticles();
        void SpawnBurst(Vec2 pos, int count, float speed, float r, float g, float b);

        struct EnemySpawn
        {
            Vec2 pos;
            bool shield = false;
        };
        std::vector<EnemySpawn> enemySpawns_;
        std::vector<uint8_t> map_ = std::vector<uint8_t>(kW * kH, 0);
        int nextEnemyId_ = 1;
        uint32_t rng_ = 12345u;
        float Rand01();
    };
}

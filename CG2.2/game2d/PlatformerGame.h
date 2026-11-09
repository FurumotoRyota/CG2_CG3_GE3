#pragma once
// 2D軸（x, y）の横スクロールアクションのゲームロジック
// 判定・移動は x, y だけの2Dで行い、描画は呼び出し側（PlatformerScene）が3Dオブジェクトで行う。
// エンジン（DirectX / ImGui / DirectInput）には依存しない。入力は InputState で渡す。
// 時間は 1/60 秒固定ステップ（速度・加速は「1フレームあたり」の値）。
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pf
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
    inline float Length(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

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
        float stompBounce = 0.38f; // 敵を踏んだときの跳ね上がり
    };

    // 保存する設定（POD。そのまま読み書きする）
    struct Settings
    {
        uint32_t version = 2;
        JumpParams jump;
    };

    // 1フレーム分の入力
    struct InputState
    {
        bool left = false;
        bool right = false;
        bool jumpPressed = false; // 押した瞬間
        bool jumpHeld = false;
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
        int dir = -1;            // 進行方向 / 盾の向き
        bool shield = false;     // 盾の敵は2回踏まないと倒せない
        int hp = 1;
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

        // ---- エディタのシーンから作る場合 ----
        void ClearStage();                             // マップ・敵・ゴールを空にする（開始位置は spawn に直接入れる）
        void AddEnemySpawn(Vec2 pos, bool shield);     // 敵を追加（Reset のとき、追加した順に id 1, 2, ... が付く）
        void SetGoal(Vec2 pos);

        // ---- 更新 ----
        // ppu：画面の高さ方向に何マス映すかを決める倍率（画面ピクセル / マス）。カメラの範囲制限に使う
        void SetView(float ppu, float screenW, float screenH);
        void Update(const InputState& input);

        // ---- 座標 ----
        IndexSet ToIndex(Vec2 p) const;
        Vec2 CellCenter(int ix, int iy) const { return { static_cast<float>(ix), static_cast<float>(kH - 1 - iy) }; }
        Rect CellRect(int ix, int iy) const;
        bool IsBlockIdx(int ix, int iy) const;
        bool IsBlockAt(Vec2 p) const;
        void SetBlock(int ix, int worldY, bool solid); // ステージ生成用（worldY は下から数える）

        // ---- 公開データ（描画・UI用） ----
        Settings settings;
        Player player;
        std::vector<Enemy> enemies;
        std::vector<Particle> particles;
        Phase phase = Phase::Play;
        Vec2 cam;                  // カメラが見ている中心（ワールド座標）
        Vec2 spawn{ 3.0f, 1.9f };
        bool hasGoal = false;
        Vec2 goal;
        int hitStop = 0;           // 残りフレーム
        float killZoom = 0.0f;     // 撃破ズームの経過（秒）。0 なら無し
        float time = 0.0f;
        float ppu = 48.0f;
        float screenW = 1280.0f;
        float screenH = 720.0f;
        int killCount = 0;
        int mapVersion = 0;        // マップを作り直すたびに増える（描画側の地形メッシュ再生成の合図）
        int resetCount = 0;        // Reset のたびに増える（描画側の敵オブジェクト作り直しの合図）

        const Enemy* FindEnemy(int id) const;

    private:
        struct Collision
        {
            bool top = false;
            bool ground = false;
            bool wall = false;
            Vec2 move;
        };

        void CheckMapCollision(Vec2 pos, Collision& c) const;
        Collision MoveWithCollision(Vec2 v);
        void ApplyMoveResult(const Collision& c);
        bool ProbeGround() const;

        void UpdatePlayer(const InputState& in);
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

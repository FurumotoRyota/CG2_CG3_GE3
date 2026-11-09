#pragma once
#include <string>

#include "scene/Scene.h"
#include "2d/ColorBatch.h"
#include "../game2d/WireActionGame.h"

/// <summary>
/// 2Dワイヤーアクション（メニュー：ゲームモード > 2Dワイヤーアクション）
/// ゲームロジックは game2d/WireActionGame（エンジン非依存）。このクラスは
/// 入力の受け渡し・描画（ColorBatch）・ImGuiの調整画面だけを担当する。
///  操作: A/D 移動 / Space ジャンプ / マウス左ボタンでワイヤー / R リスタート
/// </summary>
class WireActionScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
    void Finalize() override;

private:
    void BuildInput(wa::InputState& in);
    float EffectivePpu() const;

    // 描画
    void DrawBackground();
    void DrawTiles();
    void DrawGoal();
    void DrawEnemies();
    void DrawPlayer();
    void DrawWire();
    void DrawGlow();
    void DrawHud();

    // 画面座標への変換のショートカット
    wa::Vec2 S(wa::Vec2 world) const { return game_.WorldToScreen(world); }

    wa::Game game_;
    ColorBatch batch_;

    float ppu_ = 48.0f;               // 1ワールド単位のピクセル数
    wa::Vec2 mouse_{ 640.0f, 360.0f }; // 照準（ゲーム画面のピクセル座標）
    bool showDebug_ = false;
    std::string message_;             // 画面左のメッセージ（保存・読込の結果）
};

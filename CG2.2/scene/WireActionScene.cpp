#include "WireActionScene.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>

#include "base/DirectXCommon.h"
#include "base/WinApp.h"
#include "2d/SpriteCommon.h"
#include "editor/EditorLayout.h"
#include "input/Input.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace
{
    constexpr const char* kStagePath = "resources/stage.csv";
    constexpr const char* kSettingPath = "resources/WireSetting.bin";
    constexpr float kPi = 3.14159265f;

    ColorRGB Rgb(float r, float g, float b) { return ColorRGB{ r, g, b }; }
}

void WireActionScene::Initialize()
{
    batch_.Initialize(ctx_->spriteCommon);

    // 設定ファイルがあれば読む。ステージCSVがあればそれを、無ければ組み込みのステージを使う
    {
        std::ifstream file(kSettingPath, std::ios::binary);
        wa::Settings loaded;
        if (file.read(reinterpret_cast<char*>(&loaded), sizeof(loaded)) && loaded.version == 1) {
            game_.settings = loaded;
        }
    }
    if (!game_.LoadStageCsv(kStagePath)) {
        game_.LoadDefaultStage();
    }
    game_.SetView(ppu_, static_cast<float>(WinApp::kGameWidth), static_cast<float>(WinApp::kGameHeight));
    game_.Reset();
}

void WireActionScene::Finalize()
{
    batch_.Finalize();
}

float WireActionScene::EffectivePpu() const
{
    // 撃破のとき一瞬だけ拡大して戻す
    if (game_.killZoom > 0.0f) {
        const float u = (std::min)(game_.killZoom / 0.3f, 1.0f);
        return ppu_ * (1.0f + 0.12f * std::sin(u * kPi));
    }
    return ppu_;
}

void WireActionScene::BuildInput(wa::InputState& in)
{
    const Input& input = *ctx_->input;
    in.left = input.PushKey(DIK_A) || input.PushKey(DIK_LEFT);
    in.right = input.PushKey(DIK_D) || input.PushKey(DIK_RIGHT);
    in.jumpPressed = input.TriggerKey(DIK_SPACE) || input.TriggerGamepadButton(0);
    in.jumpHeld = input.PushKey(DIK_SPACE) || input.PushGamepadButton(0);

#ifdef USE_IMGUI
    // マウスはViewport上の位置（ゲーム画面のピクセル座標）。Viewportの外では最後の位置を保つ
    const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
    if (vp.valid) {
        mouse_ = { vp.mouse.x, vp.mouse.y };
    }
    in.firePressed = vp.clicked;
    in.fireHeld = vp.down;
#else
    // ImGuiなしのときはキーボードで代用（Jキー：前上方向にワイヤー）
    in.firePressed = input.TriggerKey(DIK_J);
    in.fireHeld = input.PushKey(DIK_J);
    mouse_ = { WinApp::kGameWidth * 0.5f + game_.player.lr * 220.0f, WinApp::kGameHeight * 0.5f - 260.0f };
#endif
    in.aim = game_.ScreenToWorld(mouse_.x, mouse_.y);
}

void WireActionScene::Update()
{
    if (ctx_->input->TriggerKey(DIK_R)) {
        game_.Reset();
    }
    game_.SetView(EffectivePpu(), static_cast<float>(WinApp::kGameWidth), static_cast<float>(WinApp::kGameHeight));
    wa::InputState in;
    BuildInput(in);
    game_.Update(in);
}

//==================================================
// 描画
//==================================================
void WireActionScene::Draw()
{
    ctx_->spriteCommon->PreDraw();
    batch_.Begin();

    // 通常ブレンド（奥から手前の順）
    DrawBackground();
    DrawTiles();
    DrawGoal();
    DrawEnemies();
    DrawPlayer();
    DrawWire();
    DrawHud();
    batch_.Flush(BlendMode::Normal);

    // 加算ブレンド（光るもの）
    DrawGlow();
    batch_.Flush(BlendMode::Add);
}

void WireActionScene::DrawBackground()
{
    const float w = static_cast<float>(WinApp::kGameWidth);
    const float h = static_cast<float>(WinApp::kGameHeight);

    // 空（上から下へ明るくなる帯）
    const int bands = 12;
    for (int i = 0; i < bands; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(bands - 1);
        const float y0 = h * static_cast<float>(i) / static_cast<float>(bands);
        const float y1 = h * static_cast<float>(i + 1) / static_cast<float>(bands);
        batch_.AddRect(0.0f, y0, w, y1 - y0 + 1.0f, Rgb(0.10f + 0.40f * t, 0.16f + 0.50f * t, 0.36f + 0.50f * t));
    }

    // 遠くの山（カメラの半分の速さで流れる）
    const float baseY = S({ 0.0f, 1.5f }).y;
    for (int k = -2; k < 24; ++k) {
        const float wx = static_cast<float>(k) * 14.0f;
        const float cx = (wx - game_.cam.x * 0.5f) * ppu_ * 0.6f + w * 0.5f;
        const float height = (4.0f + static_cast<float>((k * 5 + 40) % 4)) * ppu_ * 0.9f;
        const float half = 6.0f * ppu_ * 0.9f;
        batch_.AddTriangle(cx - half, baseY, cx, baseY - height, cx + half, baseY, Rgb(0.28f, 0.38f, 0.60f));
    }
    // 山の下を埋める
    batch_.AddRect(0.0f, baseY, w, h - baseY + 1.0f, Rgb(0.20f, 0.28f, 0.45f));
}

void WireActionScene::DrawTiles()
{
    const float ppu = game_.ppu;
    const float halfW = game_.screenW / (2.0f * ppu);
    const float halfH = game_.screenH / (2.0f * ppu);
    const int ix0 = (std::max)(0, static_cast<int>(std::floor(game_.cam.x - halfW)) - 1);
    const int ix1 = (std::min)(wa::Game::kW - 1, static_cast<int>(std::ceil(game_.cam.x + halfW)) + 1);
    const int wy0 = static_cast<int>(std::floor(game_.cam.y - halfH)) - 1;
    const int wy1 = static_cast<int>(std::ceil(game_.cam.y + halfH)) + 1;

    for (int wy = wy0; wy <= wy1; ++wy) {
        const int iy = wa::Game::kH - 1 - wy;
        if (iy < 0 || iy >= wa::Game::kH) {
            continue;
        }
        for (int ix = ix0; ix <= ix1; ++ix) {
            if (!game_.IsBlockIdx(ix, iy)) {
                continue;
            }
            const wa::Vec2 c = S({ static_cast<float>(ix), static_cast<float>(wy) });
            const float x0 = std::floor(c.x - ppu * 0.5f);
            const float x1 = std::floor(c.x + ppu * 0.5f);
            const float y0 = std::floor(c.y - ppu * 0.5f);
            const float y1 = std::floor(c.y + ppu * 0.5f);
            const bool topOpen = !game_.IsBlockIdx(ix, iy - 1);
            const bool bottomOpen = !game_.IsBlockIdx(ix, iy + 1);
            const float shade = ((ix + iy) & 1) ? 0.0f : 0.04f;

            // 土
            batch_.AddRect(x0, y0, x1 - x0, y1 - y0, Rgb(0.44f + shade, 0.29f + shade, 0.17f + shade));
            // 上が空いていれば草
            if (topOpen) {
                batch_.AddRect(x0, y0, x1 - x0, (y1 - y0) * 0.28f, Rgb(0.28f, 0.66f + shade, 0.28f));
            }
            // 下が空いていれば影
            if (bottomOpen) {
                batch_.AddRect(x0, y1 - (y1 - y0) * 0.15f, x1 - x0, (y1 - y0) * 0.15f, Rgb(0.28f, 0.18f, 0.10f));
            }
        }
    }
}

void WireActionScene::DrawGoal()
{
    if (!game_.hasGoal) {
        return;
    }
    const float ppu = game_.ppu;
    const wa::Vec2 g = S(game_.goal);
    // 旗ざお
    batch_.AddRect(g.x - ppu * 0.06f, g.y - ppu * 1.0f, ppu * 0.12f, ppu * 2.0f, Rgb(0.85f, 0.85f, 0.85f));
    // 旗（ゆれる）
    const float wave = std::sin(game_.time * 4.0f) * ppu * 0.08f;
    batch_.AddTriangle(g.x + ppu * 0.06f, g.y - ppu * 1.0f, g.x + ppu * 0.06f, g.y - ppu * 0.4f,
        g.x + ppu * (0.9f) + wave, g.y - ppu * 0.7f, Rgb(1.0f, 0.8f, 0.1f));
}

void WireActionScene::DrawEnemies()
{
    const float ppu = game_.ppu;
    for (const wa::Enemy& e : game_.enemies) {
        const wa::Vec2 c = S(e.pos);
        float scale = 1.0f;
        float angle = 0.0f;
        if (e.dying) {
            const float u = (std::min)(e.dyingTimer / 0.4f, 1.0f);
            scale = 1.0f - u; // 回りながら縮む
            angle = u * 6.0f;
        }
        const float half = ppu * 0.5f * scale;
        // 歩くときの上下の弾み
        const float bounce = e.dying ? 0.0f : std::fabs(std::sin(e.walkTime * 8.0f)) * ppu * 0.06f;
        const float cs = std::cos(angle);
        const float sn = std::sin(angle);
        auto corner = [&](float lx, float ly, float& ox, float& oy) {
            ox = c.x + (lx * cs - ly * sn);
            oy = c.y - bounce + (lx * sn + ly * cs);
            };
        ColorRGB body = e.shield ? Rgb(0.55f, 0.30f, 0.75f) : Rgb(0.85f, 0.22f, 0.22f);
        if (e.guardFlash > 0.0f) {
            body = Rgb(1.0f, 1.0f, 1.0f);
        }
        float xy[8];
        corner(-half, -half, xy[0], xy[1]);
        corner(half, -half, xy[2], xy[3]);
        corner(half, half, xy[4], xy[5]);
        corner(-half, half, xy[6], xy[7]);
        batch_.AddQuad(xy, body);

        if (!e.dying) {
            // 目（進行方向側）
            const float fd = static_cast<float>(e.dir);
            batch_.AddRect(c.x + fd * ppu * 0.12f - ppu * 0.09f, c.y - bounce - ppu * 0.18f, ppu * 0.18f, ppu * 0.22f, Rgb(1.0f, 1.0f, 1.0f));
            batch_.AddRect(c.x + fd * ppu * 0.12f + fd * ppu * 0.02f - ppu * 0.04f, c.y - bounce - ppu * 0.12f, ppu * 0.08f, ppu * 0.12f, Rgb(0.1f, 0.1f, 0.1f));
            if (e.shield) {
                // 盾（向いている側）
                const float sx = c.x + fd * ppu * 0.5f - ppu * 0.09f;
                batch_.AddRect(sx, c.y - bounce - ppu * 0.5f, ppu * 0.18f, ppu * 1.0f, Rgb(0.45f, 0.75f, 1.0f));
            }
        }
    }
}

void WireActionScene::DrawPlayer()
{
    const wa::Player& p = game_.player;
    // 無敵中は点滅
    if (p.invincible > 0.0f && (static_cast<int>(game_.time * 20.0f) & 1)) {
        return;
    }
    if (game_.phase == wa::Phase::Death) {
        return;
    }
    const float ppu = game_.ppu;
    const wa::Vec2 c = S(p.pos);
    const float w = wa::Game::kPlayerW * ppu;
    const float h = wa::Game::kPlayerH * ppu;
    const float fd = static_cast<float>(p.lr);

    // 体（ワイヤー攻撃中は光る色）
    const ColorRGB body = p.attacking ? Rgb(1.0f, 0.95f, 0.5f) : Rgb(0.95f, 0.55f, 0.15f);
    batch_.AddRect(c.x - w * 0.5f, c.y - h * 0.5f, w, h, body);
    batch_.AddRect(c.x - w * 0.5f, c.y + h * 0.5f - h * 0.18f, w, h * 0.18f, Rgb(0.65f, 0.30f, 0.05f));
    // 目
    batch_.AddRect(c.x + fd * w * 0.18f - w * 0.11f, c.y - h * 0.22f, w * 0.22f, h * 0.26f, Rgb(1.0f, 1.0f, 1.0f));
    batch_.AddRect(c.x + fd * w * 0.18f + fd * w * 0.03f - w * 0.05f, c.y - h * 0.14f, w * 0.10f, h * 0.14f, Rgb(0.1f, 0.1f, 0.15f));
    // はちまき
    batch_.AddRect(c.x - w * 0.5f, c.y - h * 0.40f, w, h * 0.10f, Rgb(0.9f, 0.15f, 0.15f));

    if (showDebug_) {
        const float t = 2.0f;
        batch_.AddRect(c.x - w * 0.5f, c.y - h * 0.5f, w, t, Rgb(0.0f, 1.0f, 0.0f));
        batch_.AddRect(c.x - w * 0.5f, c.y + h * 0.5f - t, w, t, Rgb(0.0f, 1.0f, 0.0f));
        batch_.AddRect(c.x - w * 0.5f, c.y - h * 0.5f, t, h, Rgb(0.0f, 1.0f, 0.0f));
        batch_.AddRect(c.x + w * 0.5f - t, c.y - h * 0.5f, t, h, Rgb(0.0f, 1.0f, 0.0f));
    }
}

void WireActionScene::DrawWire()
{
    if (game_.wire.state == wa::WireState::None) {
        return;
    }
    const wa::Vec2 a = S(game_.player.pos);
    const wa::Vec2 b = S(game_.WireTip());
    batch_.AddLine(a.x, a.y, b.x, b.y, 3.0f, Rgb(0.95f, 0.98f, 1.0f)); // ロープの芯
    batch_.AddDisc(b.x, b.y, 5.0f, Rgb(1.0f, 1.0f, 1.0f));              // 先端
}

// 光るもの（加算ブレンド）：ロープのグロー、照準、火花、画面の色かぶり
void WireActionScene::DrawGlow()
{
    const float w = static_cast<float>(WinApp::kGameWidth);
    const float h = static_cast<float>(WinApp::kGameHeight);

    if (game_.wire.state != wa::WireState::None) {
        const wa::Vec2 a = S(game_.player.pos);
        const wa::Vec2 b = S(game_.WireTip());
        batch_.AddLine(a.x, a.y, b.x, b.y, 11.0f, Rgb(0.06f, 0.20f, 0.30f));
        batch_.AddLine(a.x, a.y, b.x, b.y, 6.0f, Rgb(0.10f, 0.35f, 0.50f));
        batch_.AddDisc(b.x, b.y, 11.0f, Rgb(0.15f, 0.40f, 0.55f));
    }
    else if (game_.wireEnabled && game_.wire.cand.type != wa::CandType::None && game_.phase == wa::Phase::Play) {
        // ロックオンの候補（ブロックは緑、敵は赤）
        const wa::Vec2 c = S(game_.wire.cand.pos);
        const wa::Vec2 a = S(game_.player.pos);
        const bool enemy = game_.wire.cand.type == wa::CandType::Enemy;
        const ColorRGB col = enemy ? Rgb(0.9f, 0.25f, 0.15f) : Rgb(0.2f, 0.8f, 0.35f);
        batch_.AddLine(a.x, a.y, c.x, c.y, 1.5f, Rgb(col.r * 0.25f, col.g * 0.25f, col.b * 0.25f));
        const float r = enemy ? 18.0f : 8.0f;
        batch_.AddDisc(c.x, c.y, r, Rgb(col.r * 0.55f, col.g * 0.55f, col.b * 0.55f));
        batch_.AddDisc(c.x, c.y, r * 0.5f, col);
    }

    // 照準（ワイヤーを使うときだけ）
    if (game_.wireEnabled) {
    batch_.AddLine(mouse_.x - 10.0f, mouse_.y, mouse_.x - 4.0f, mouse_.y, 2.0f, Rgb(0.8f, 0.8f, 0.8f));
    batch_.AddLine(mouse_.x + 4.0f, mouse_.y, mouse_.x + 10.0f, mouse_.y, 2.0f, Rgb(0.8f, 0.8f, 0.8f));
    batch_.AddLine(mouse_.x, mouse_.y - 10.0f, mouse_.x, mouse_.y - 4.0f, 2.0f, Rgb(0.8f, 0.8f, 0.8f));
    batch_.AddLine(mouse_.x, mouse_.y + 4.0f, mouse_.x, mouse_.y + 10.0f, 2.0f, Rgb(0.8f, 0.8f, 0.8f));
    }

    // 火花
    for (const wa::Particle& p : game_.particles) {
        const float t = p.life / p.maxLife;
        const wa::Vec2 s = S(p.pos);
        const float r = p.size * game_.ppu * (0.4f + 0.6f * t);
        batch_.AddRect(s.x - r, s.y - r, r * 2.0f, r * 2.0f, Rgb(p.r * t, p.g * t, p.b * t));
    }

    // 画面の色かぶり
    if (game_.phase == wa::Phase::Death) {
        batch_.AddRect(0.0f, 0.0f, w, h, Rgb(0.30f, 0.02f, 0.02f));
    }
    else if (game_.phase == wa::Phase::Clear) {
        batch_.AddRect(0.0f, 0.0f, w, h, Rgb(0.28f, 0.24f, 0.02f));
    }
    // 被弾した瞬間の赤い縁
    if (game_.player.invincible > 1.3f) {
        batch_.AddRect(0.0f, 0.0f, w, 12.0f, Rgb(0.6f, 0.05f, 0.05f));
        batch_.AddRect(0.0f, h - 12.0f, w, 12.0f, Rgb(0.6f, 0.05f, 0.05f));
    }
}

void WireActionScene::DrawHud()
{
    // HP（ハート代わりの四角）
    for (int i = 0; i < 3; ++i) {
        const bool alive = i < game_.player.hp;
        const float x = 20.0f + static_cast<float>(i) * 34.0f;
        batch_.AddRect(x - 2.0f, 18.0f, 30.0f, 30.0f, Rgb(0.1f, 0.1f, 0.1f));
        batch_.AddRect(x, 20.0f, 26.0f, 26.0f, alive ? Rgb(0.95f, 0.2f, 0.25f) : Rgb(0.3f, 0.3f, 0.3f));
    }
    // 残りの敵の数（小さな赤い点）
    int alive = 0;
    for (const wa::Enemy& e : game_.enemies) {
        if (!e.dying && !e.dead) {
            ++alive;
        }
    }
    for (int i = 0; i < alive && i < 40; ++i) {
        batch_.AddRect(20.0f + static_cast<float>(i) * 12.0f, 58.0f, 9.0f, 9.0f, Rgb(0.85f, 0.22f, 0.22f));
    }
}

//==================================================
// ImGui
//==================================================
void WireActionScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("Hierarchy", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: 2Dワイヤーアクション");
    ImGui::Separator();
    const char* phaseName = game_.phase == wa::Phase::Play ? "プレイ中" : (game_.phase == wa::Phase::Clear ? "クリア！" : "ゲームオーバー");
    ImGui::Text("状態: %s", phaseName);
    ImGui::Text("HP: %d / 3", game_.player.hp);
    int alive = 0;
    for (const wa::Enemy& e : game_.enemies) {
        if (!e.dying && !e.dead) {
            ++alive;
        }
    }
    ImGui::Text("残りの敵: %d  倒した数: %d", alive, game_.killCount);
    const char* wireNames[] = { "なし", "発射中", "引き寄せ", "スイング" };
    ImGui::Text("ワイヤー: %s", wireNames[static_cast<int>(game_.wire.state)]);
    ImGui::Separator();
    ImGui::Text("A/D: 移動  Space: ジャンプ");
    ImGui::Checkbox("ワイヤーを使う", &game_.wireEnabled);
    ImGui::Text("マウス左: ワイヤー（押し続けてスイング）");
    ImGui::Text("敵は上から踏んで倒せる");
    ImGui::Text("スイング中 A/D: 漕ぐ / Space: 飛び出す");
    ImGui::Text("R: やり直し");
    ImGui::Separator();
    if (ImGui::Button("やり直し (R)")) {
        game_.Reset();
    }
    if (ImGui::Button("ステージCSVを読み込む")) {
        message_ = game_.LoadStageCsv(kStagePath) ? "stage.csv loaded" : "stage.csv not found";
    }
    if (ImGui::Button("組み込みステージに戻す")) {
        game_.LoadDefaultStage();
        message_ = "default stage";
    }
    if (ImGui::Button("今のステージをCSVに書き出す")) {
        message_ = game_.SaveStageCsv(kStagePath) ? "stage.csv saved" : "save failed";
    }
    if (!message_.empty()) {
        ImGui::TextDisabled("%s", message_.c_str());
    }
    ImGui::TextWrapped("CSV: 0=空 1=ブロック P=開始 E=敵 S=盾の敵 G=ゴール（200列x20行）");
    ImGui::End();

    EditorLayout::Begin("Inspector", EditorLayout::Panel::Inspector);
    ImGui::SliderFloat("拡大率 (px/マス)", &ppu_, 24.0f, 96.0f);
    ImGui::Checkbox("当たり判定を表示", &showDebug_);
    wa::JumpParams& jp = game_.settings.jump;
    wa::WireParams& wp = game_.settings.wire;
    if (ImGui::CollapsingHeader("移動・ジャンプ", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("走る速さ", &jp.runSpeed, 0.05f, 0.6f);
        ImGui::SliderFloat("ジャンプ初速", &jp.jumpSpeed, 0.2f, 1.0f);
        ImGui::SliderFloat("上昇中の重力", &jp.gravityScaleUp, 0.2f, 2.0f);
        ImGui::SliderFloat("落下中の重力", &jp.gravityScaleDown, 0.2f, 2.0f);
        ImGui::SliderFloat("最大落下速度", &jp.maxFallSpeed, 0.1f, 0.8f);
        ImGui::SliderFloat("頂点のふわっと", &jp.apexFloat, 0.1f, 1.0f);
        ImGui::SliderFloat("地上の加速", &jp.groundAccel, 0.01f, 0.2f);
        ImGui::SliderFloat("地上の減速", &jp.groundFriction, 0.5f, 1.0f);
        ImGui::SliderFloat("空中の加速", &jp.airAccel, 0.005f, 0.2f);
        ImGui::SliderFloat("空中の減速", &jp.airDrag, 0.9f, 1.0f);
        ImGui::SliderFloat("小ジャンプの抑え", &jp.jumpCut, 0.1f, 1.0f);
    }
    if (ImGui::CollapsingHeader("ワイヤー")) {
        ImGui::SliderFloat("射程", &wp.range, 4.0f, 24.0f);
        ImGui::SliderFloat("敵の吸着半径", &wp.lockRadius, 0.5f, 4.0f);
        ImGui::SliderFloat("ブロックの吸着半径", &wp.blockAssistRadius, 0.3f, 3.0f);
        ImGui::SliderFloat("引き寄せ速度", &wp.pullSpeed, 0.2f, 1.0f);
        ImGui::SliderFloat("敵を倒した跳ね(上)", &wp.hitBounceUp, 0.0f, 0.6f);
        ImGui::SliderFloat("スイング重力", &wp.swingGravity, 0.005f, 0.06f);
        ImGui::SliderFloat("漕ぐ力", &wp.swingForce, 0.0f, 0.03f);
        ImGui::SliderFloat("スイング最高速度", &wp.swingMaxSpeed, 0.2f, 1.2f);
        ImGui::SliderFloat("離したときの加速", &wp.swingReleaseBoost, 1.0f, 1.6f);
        ImGui::SliderFloat("Spaceで飛び出す上向き", &wp.releaseKick, 0.0f, 0.4f);
        ImGui::SliderFloat("取り付け時の最小ロープ長", &wp.attachMinLength, 1.5f, 8.0f);
        ImGui::SliderFloat("滞空の重力(勢い中)", &wp.floatGravityScale, 0.2f, 1.0f);
    }
    if (ImGui::Button("設定を保存")) {
        std::ofstream file(kSettingPath, std::ios::binary);
        if (file.write(reinterpret_cast<const char*>(&game_.settings), sizeof(game_.settings))) {
            message_ = "settings saved";
        }
        else {
            message_ = "settings save failed";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("設定を初期値に戻す")) {
        game_.settings = wa::Settings{};
    }
    ImGui::End();
#endif // USE_IMGUI
}

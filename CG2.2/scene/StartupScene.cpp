#include "StartupScene.h"
#include "TitleScene.h"
#include "scene/SceneManager.h"
#include "base/WinApp.h"
#include "2d/SpriteCommon.h"
#include "input/Input.h"
#include "editor/EditorLayout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace
{
    constexpr float kScreenW = static_cast<float>(WinApp::kGameWidth);
    constexpr float kScreenH = static_cast<float>(WinApp::kGameHeight);

    // 文字の画像（resources/startup_ds_glyphs.png）の作り。1文字ぶんの大きさと、並んでいる文字の順番
    constexpr float kCellW = 104.0f;
    constexpr float kCellH = 150.0f;
    constexpr const char* kCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.";
    constexpr int kCharsetCount = 37;

    // 文字を並べる間隔と、縦の位置（文字の中心）
    constexpr float kReadoutStep = 96.0f;
    constexpr float kReadoutScale = 1.5f;
    constexpr float kReadoutY = 330.0f;
    constexpr float kTitleStep = 72.0f;
    constexpr float kTitleScale = 1.1f;  // 名前の文字の大きさ
    constexpr float kTitleY = 320.0f;
    constexpr float kSubStep = 44.0f;   // 下の小さな文字
    constexpr float kSubScale = 0.7f;
    constexpr float kSubY = 478.0f;

    // 稲妻の画像（resources/startup_bolts.png）：1024x200 の絵が8枚、縦に並んでいる
    constexpr float kBoltW = 1024.0f;
    constexpr float kBoltH = 200.0f;
    constexpr int kBoltVariants = 8;
    constexpr float kPi = 3.14159265f;

    float Clamp01(float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); }
    float Smooth(float v) { v = Clamp01(v); return v * v * (3.0f - 2.0f * v); } // ゆっくり始まりゆっくり終わる

    void SetTint(Sprite& sprite, float r, float g, float b, float alpha)
    {
        sprite.GetMaterial()->color = { r, g, b, alpha };
    }

    std::unique_ptr<Sprite> MakeSprite(SpriteCommon* common, const char* path, float w, float h)
    {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(common, path, { w, h });
        return sprite;
    }
}

int StartupScene::GlyphIndex(char c)
{
    const char* p = (c == '\0') ? nullptr : std::strchr(kCharset, c);
    return p ? static_cast<int>(p - kCharset) : -1;
}

void StartupScene::ShowGlyph(Sprite& sprite, int glyph, float cx, float cy, float scale, float brightness, float alpha, float r, float g, float b)
{
    sprite.SetTextureRect({ glyph * kCellW, 0.0f }, { kCellW, kCellH });
    Transform& tr = sprite.GetTransform();
    tr.Scale = { scale, scale, 1.0f };
    tr.Translate = { cx - kCellW * scale * 0.5f, cy - kCellH * scale * 0.5f, 0.0f };
    SetTint(sprite, brightness * r, brightness * g, brightness * b, alpha);
    sprite.Update();
}

void StartupScene::SpawnBolt(float x, float y, float angle, float length, float thick, int life)
{
    // 空いている枠を探す（なければいちばん古いものを使う）
    int slot = 0;
    for (int i = 0; i < kBoltCount; ++i) {
        if (bolts_[i].life <= 0) { slot = i; break; }
        if (bolts_[i].life < bolts_[slot].life) { slot = i; }
    }
    Bolt& b = bolts_[slot];
    b.x = x; b.y = y; b.angle = angle; b.length = length; b.thick = thick;
    b.life = life; b.maxLife = life;
}

void StartupScene::UpdateBolts()
{
    for (int i = 0; i < kBoltCount; ++i) {
        Bolt& b = bolts_[i];
        if (b.life <= 0) {
            continue;
        }
        // 2フレームごとに形を引き直す（バチバチ感）。形は8種類
        if ((b.maxLife - b.life) % 2 == 0) {
            b.variant = static_cast<int>(rng_() % kBoltVariants);
        }
        const float fade = static_cast<float>(b.life) / static_cast<float>(b.maxLife);
        std::uniform_real_distribution<float> flick(0.5f, 1.0f);
        const float bright = fade * flick(rng_) * screenAlpha_;

        b.sprite->SetTextureRect({ 0.0f, b.variant * kBoltH }, { kBoltW, kBoltH });
        const float sx = b.length / kBoltW;
        const float sy = b.thick;
        Transform& tr = b.sprite->GetTransform();
        tr.Scale = { sx, sy, 1.0f };
        tr.Rotate = { 0.0f, 0.0f, b.angle };
        // 稲妻の始点は画像の左端中央(0, 100)。そこが(x, y)に来るように位置をずらす
        const float oy = kBoltH * 0.5f * sy;
        tr.Translate = { b.x + std::sin(b.angle) * oy, b.y - std::cos(b.angle) * oy, 0.0f };
        SetTint(*b.sprite, 0.75f * bright, 0.9f * bright, 1.0f * bright, 1.0f);
        b.sprite->Update();
        --b.life;
    }
}

void StartupScene::Initialize()
{
    SpriteCommon* common = ctx_->spriteCommon;

    background_ = MakeSprite(common, "resources/startup_ds_bg.png", kScreenW, kScreenH);
    scan_ = MakeSprite(common, "resources/startup_scan.png", kScreenW, kScreenH);

    // 光る文字は加算で重ねる
    for (int i = 0; i < kReadoutLength; ++i) {
        readout_[i] = MakeSprite(common, "resources/startup_ds_glyphs.png", kCellW, kCellH);
        readout_[i]->SetBlendMode(BlendMode::Add);
        shownReadout_[i] = -1;
    }
    for (int i = 0; i < kTitleLength; ++i) {
        title_[i] = MakeSprite(common, "resources/startup_ds_glyphs.png", kCellW, kCellH);
        title_[i]->SetBlendMode(BlendMode::Add);
        shownTitle_[i] = -1;
        randomGlyph_[i] = 0;
    }

    for (int i = 0; i < kSubLength; ++i) {
        sub_[i] = MakeSprite(common, "resources/startup_ds_glyphs.png", kCellW, kCellH);
        sub_[i]->SetBlendMode(BlendMode::Add);
        shownSub_[i] = -1;
        randomSub_[i] = 0;
    }
    underline_ = MakeSprite(common, "resources/startup_white.png", 8.0f, 8.0f);
    underline_->SetBlendMode(BlendMode::Add);
    bars_.clear();
    for (int i = 0; i < 3; ++i) {
        auto bar = MakeSprite(common, "resources/startup_white.png", 8.0f, 8.0f);
        bar->SetBlendMode(BlendMode::Add);
        bars_.push_back(std::move(bar));
    }
    flash_ = MakeSprite(common, "resources/startup_white.png", 8.0f, 8.0f);
    flash_->GetTransform().Scale = { kScreenW / 8.0f, kScreenH / 8.0f, 1.0f };
    flash_->GetTransform().Translate = { 0.0f, 0.0f, 0.0f };

    noise_ = MakeSprite(common, "resources/startup_noise.png", kScreenW, kScreenH);
    noise_->SetBlendMode(BlendMode::Add);
    band_ = MakeSprite(common, "resources/startup_white.png", 8.0f, 8.0f);
    band_->SetBlendMode(BlendMode::Add);
    bandLine_ = MakeSprite(common, "resources/startup_white.png", 8.0f, 8.0f);
    bandLine_->SetBlendMode(BlendMode::Add);
    intro_ = MakeSprite(common, "resources/startup_white.png", 8.0f, 8.0f);
    intro_->SetBlendMode(BlendMode::Add);
    for (int i = 0; i < kTitleLength; ++i) {
        ghostR_[i] = MakeSprite(common, "resources/startup_ds_glyphs.png", kCellW, kCellH);
        ghostR_[i]->SetBlendMode(BlendMode::Add);
        ghostB_[i] = MakeSprite(common, "resources/startup_ds_glyphs.png", kCellW, kCellH);
        ghostB_[i]->SetBlendMode(BlendMode::Add);
        ghostOn_[i] = false;
    }

    for (int i = 0; i < kBoltCount; ++i) {
        bolts_[i].sprite = MakeSprite(common, "resources/startup_bolts.png", kBoltW, kBoltH);
        bolts_[i].sprite->SetBlendMode(BlendMode::Add);
        bolts_[i].life = 0;
    }

    time_ = 0.0f;
    frameCount_ = 0;
}

void StartupScene::Update()
{
    time_ += 1.0f / 60.0f;
    ++frameCount_;
    const float t = time_;
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);

    // 全体の明るさ（最後に暗く消える）
    screenAlpha_ = 1.0f - Smooth((t - kFadeOutStart) / kFadeOut);

    background_->GetTransform().Translate = { 0.0f, 0.0f, 0.0f };
    SetTint(*background_, screenAlpha_, screenAlpha_, screenAlpha_, 1.0f);
    background_->Update();
    scan_->GetTransform().Translate = { 0.0f, 0.0f, 0.0f };
    SetTint(*scan_, 1.0f, 1.0f, 1.0f, 0.3f * screenAlpha_); // 走査線は薄めにして、暗くなりすぎないようにする
    scan_->Update();

    float glitch = 0.0f; // 画面の乱れの強さ（0～1）。ゆれ・横線・フラッシュに使う
    for (int i = 0; i < kTitleLength; ++i) {
        ghostOn_[i] = false;
    }

    // ============ 前半：数字 ============
    for (int i = 0; i < kReadoutLength; ++i) {
        shownReadout_[i] = -1;
    }
    if (t < kReadoutEnd) {
        const float flux = Clamp01((t - kFluxStart) / (kReadoutEnd - kFluxStart)); // 最後に向けて強くなる
        glitch = flux;
        const float jitter = flux * flux * 10.0f;
        const float jx = unit(rng_) * jitter;
        const float jy = unit(rng_) * jitter * 0.5f;
        const float warm = Smooth((t - 0.2f) / kReadoutWarm);

        const float totalW = kReadoutStep * kReadoutLength;
        int order = 0;
        for (int i = 0; i < kReadoutLength; ++i) {
            const char c = kReadoutText[i];
            int shown = -1;
            if (c == '.') {
                shown = (t >= 0.3f) ? GlyphIndex('.') : -1; // 小数点はずっと点いている
            }
            else {
                const float lockTime = kLockStart + order * kLockGap;
                ++order;
                // 3フレームに1回、文字ごとにずらして引き直す（高速で切り替わって見える）
                if (frameCount_ % 3 == i % 3) {
                    randomGlyph_[i] = 26 + static_cast<int>(rng_() % 10); // 数字だけ（アトラスの26番目以降が0～9）
                }
                if (t < kRandomStart) {
                    shown = -1;
                }
                else if (t < lockTime) {
                    shown = randomGlyph_[i];
                }
                else {
                    shown = GlyphIndex(c);
                    if (flux > 0.0f && std::abs(unit(rng_)) < flux * 0.7f) {
                        shown = randomGlyph_[i]; // ゆらぎ：ときどき違う数字がちらつく
                    }
                }
            }
            shownReadout_[i] = shown;
            if (shown >= 0) {
                const float flicker = 0.93f + 0.07f * std::sin(t * 37.0f + i * 1.7f);
                const float dip = (flux > 0.0f && std::abs(unit(rng_)) < flux * 0.25f) ? 0.45f : 1.0f; // たまに一瞬暗くなる
                const float cx = (kScreenW - totalW) * 0.5f + (i + 0.5f) * kReadoutStep + jx;
                ShowGlyph(*readout_[i], shown, cx, kReadoutY + jy, kReadoutScale, warm * flicker * dip * screenAlpha_, 1.0f);
                // 乱れが強いほど、赤と青の影が左右にずれる（色収差）
                if (flux > 0.15f) {
                    const float split = flux * flux * 9.0f;
                    ShowGlyph(*ghostR_[i], shown, cx + split, kReadoutY + jy, kReadoutScale, 0.55f * warm * dip * screenAlpha_, 1.0f, 1.0f, 0.1f, 0.05f);
                    ShowGlyph(*ghostB_[i], shown, cx - split, kReadoutY + jy, kReadoutScale, 0.55f * warm * dip * screenAlpha_, 1.0f, 0.1f, 0.5f, 1.0f);
                    ghostOn_[i] = true;
                }
            }
        }
    }

    // ============ 後半：名前 ============
    const float titleTotalW = kTitleStep * kTitleLength;
    float titleJitterX = 0.0f;
    float titleJitterY = 0.0f;
    const bool titlePhase = t >= kTitleStart;
    // ときどき、短く乱れる（名前が決まったあと）
    float burst = 0.0f;
    if (t > kTitleStart + kTitleGap * (kTitleLength - 1) + kScrambleTime + 0.4f && t < kFadeOutStart) {
        const float phase = std::fmod(t, 1.9f);
        burst = phase < 0.12f ? 1.0f : 0.0f;
    }
    if (burst > 0.0f) {
        glitch = (std::max)(glitch, 0.5f);
        titleJitterX = unit(rng_) * 7.0f;
        titleJitterY = unit(rng_) * 2.0f;
    }
    for (int i = 0; i < kTitleLength; ++i) {
        shownTitle_[i] = -1;
        const char c = kTitleText[i];
        if (c == ' ' || !titlePhase) {
            continue;
        }
        const float start = kTitleStart + i * kTitleGap;
        if (t < start) {
            continue;
        }
        const float lockTime = start + kScrambleTime;
        int shown = GlyphIndex(c);
        float flicker = 0.94f + 0.06f * std::sin(t * 31.0f + i * 2.3f);
        if (t < lockTime) {
            // 決まる前は、2フレームに1回、でたらめな文字に変わる
            if (frameCount_ % 2 == i % 2) {
                randomGlyph_[i] = static_cast<int>(rng_() % kCharsetCount);
            }
            shown = randomGlyph_[i];
            flicker = 0.75f + 0.25f * std::abs(unit(rng_));
        }
        else if (burst > 0.0f && std::abs(unit(rng_)) < 0.25f) {
            shown = static_cast<int>(rng_() % kCharsetCount); // 乱れのとき、数文字だけ一瞬変わる
        }
        shownTitle_[i] = shown;
        const float cx = (kScreenW - titleTotalW) * 0.5f + (i + 0.5f) * kTitleStep + titleJitterX;
        ShowGlyph(*title_[i], shown, cx, kTitleY + titleJitterY, kTitleScale, flicker * screenAlpha_, 1.0f);
        // 解読中と乱れのときは、赤と青の影をずらして重ねる
        {
            const float split = (burst > 0.0f) ? 6.0f : (t < lockTime ? 2.5f : 0.0f);
            if (split > 0.0f) {
                ShowGlyph(*ghostR_[i], shown, cx + split, kTitleY + titleJitterY, kTitleScale, 0.55f * flicker * screenAlpha_, 1.0f, 1.0f, 0.1f, 0.05f);
                ShowGlyph(*ghostB_[i], shown, cx - split, kTitleY + titleJitterY, kTitleScale, 0.55f * flicker * screenAlpha_, 1.0f, 0.1f, 0.5f, 1.0f);
                ghostOn_[i] = true;
            }
        }
    }

    // 名前の下の小さな文字：名前と同じように、でたらめな文字から決まっていく
    {
        const float subTotalW = kSubStep * kSubLength;
        for (int i = 0; i < kSubLength; ++i) {
            shownSub_[i] = -1;
            const char ch = kSubText[i];
            const float start = kSubStart + i * kSubGap;
            if (ch == ' ' || t < start) {
                continue;
            }
            int shown = GlyphIndex(ch);
            float flicker = 0.94f + 0.06f * std::sin(t * 29.0f + i * 1.9f);
            if (t < start + kSubScramble) {
                if (frameCount_ % 2 == i % 2) {
                    randomSub_[i] = static_cast<int>(rng_() % kCharsetCount);
                }
                shown = randomSub_[i];
                flicker = 0.75f + 0.25f * std::abs(unit(rng_));
            }
            shownSub_[i] = shown;
            const float cx = (kScreenW - subTotalW) * 0.5f + (i + 0.5f) * kSubStep + titleJitterX;
            ShowGlyph(*sub_[i], shown, cx, kSubY + titleJitterY, kSubScale, flicker * screenAlpha_, 1.0f);
        }
    }

    // 名前の下の線：文字が決まったあとに、真ん中から左右へ伸びる
    {
        const float u = Smooth((t - kLineStart) / kLineTime);
        const float w = (kTitleStep * (kTitleLength - 1)) * u;
        Transform& tr = underline_->GetTransform();
        tr.Scale = { (std::max)(w, 0.01f) / 8.0f, 3.0f / 8.0f, 1.0f };
        tr.Translate = { (kScreenW - w) * 0.5f, kTitleY + 92.0f, 0.0f };
        SetTint(*underline_, 1.0f, 0.55f, 0.2f, 0.8f * u * screenAlpha_);
        underline_->Update();
    }

    // 乱れの横線
    for (size_t i = 0; i < bars_.size(); ++i) {
        const bool on = glitch > 0.0f && std::abs(unit(rng_)) < glitch * 0.5f;
        Transform& tr = bars_[i]->GetTransform();
        tr.Scale = { kScreenW / 8.0f, (2.0f + 4.0f * std::abs(unit(rng_))) / 8.0f, 1.0f };
        tr.Translate = { 0.0f, (unit(rng_) * 0.5f + 0.5f) * kScreenH, 0.0f };
        SetTint(*bars_[i], 1.0f, 0.6f, 0.3f, on ? 0.3f * glitch : 0.0f);
        bars_[i]->Update();
    }

    // 白いフラッシュ：数字が終わる瞬間と、名前が決まった直後に、一瞬だけ光る
    {
        float flashAlpha = 0.0f;
        if (t >= kReadoutEnd - 0.05f && t < kReadoutEnd + 0.25f) {
            flashAlpha = 0.7f * (1.0f - Smooth((t - (kReadoutEnd - 0.05f)) / 0.3f));
        }
        const float titleDone = kTitleStart + kTitleGap * (kTitleLength - 1) + kScrambleTime;
        if (t >= titleDone && t < titleDone + 0.2f) {
            flashAlpha = (std::max)(flashAlpha, 0.35f * (1.0f - Smooth((t - titleDone) / 0.2f)));
        }
        SetTint(*flash_, 1.0f, 0.85f, 0.65f, flashAlpha * screenAlpha_);
        flash_->Update();
    }

    // ============ 画面ノイズ・走査 ============
    {
        // 砂嵐：いつもうっすら、乱れのときは濃く
        const float base = 0.02f + 0.18f * glitch;
        noiseAlpha_ = (std::min)(0.2f, base) * screenAlpha_; // 文字が読みにくくならないよう弱め
        std::uniform_int_distribution<int> ox(0, static_cast<int>(1536 - kScreenW));
        std::uniform_int_distribution<int> oy(0, static_cast<int>(864 - kScreenH));
        noise_->SetTextureRect({ static_cast<float>(ox(rng_)), static_cast<float>(oy(rng_)) }, { kScreenW, kScreenH });
        noise_->GetTransform().Translate = { 0.0f, 0.0f, 0.0f };
        SetTint(*noise_, 0.8f, 0.9f, 1.0f, noiseAlpha_);
        noise_->Update();

        // 明るい帯：2.4秒ごとに上から下へゆっくり流れる
        const float cycle = std::fmod(t, 2.4f) / 2.4f;
        const float by = -120.0f + cycle * (kScreenH + 240.0f);
        Transform& bt = band_->GetTransform();
        bt.Scale = { kScreenW / 8.0f, 110.0f / 8.0f, 1.0f };
        bt.Translate = { 0.0f, by, 0.0f };
        SetTint(*band_, 0.7f, 0.85f, 1.0f, 0.02f * screenAlpha_);
        band_->Update();
        Transform& lt = bandLine_->GetTransform();
        lt.Scale = { kScreenW / 8.0f, 2.0f / 8.0f, 1.0f };
        lt.Translate = { 0.0f, by + 110.0f, 0.0f };
        SetTint(*bandLine_, 0.7f, 0.85f, 1.0f, 0.035f * screenAlpha_);
        bandLine_->Update();

        // 起動直後：中央に光の線が現れて、左右に広がり、すぐ消える
        const float iu = Clamp01(t / 0.35f);
        const float fade = 1.0f - Smooth((t - 0.35f) / 0.35f);
        const float iw = kScreenW * Smooth(iu);
        Transform& it = intro_->GetTransform();
        it.Scale = { (std::max)(iw, 0.01f) / 8.0f, 2.0f / 8.0f, 1.0f };
        it.Translate = { (kScreenW - iw) * 0.5f, kScreenH * 0.5f, 0.0f };
        SetTint(*intro_, 0.8f, 0.9f, 1.0f, (t < 0.7f ? 0.9f * fade : 0.0f) * screenAlpha_);
        intro_->Update();
    }

    // ============ 電気のビリビリ ============
    {
        const float prev = t - 1.0f / 60.0f;
        auto crossed = [&](float when) { return prev < when && t >= when; };
        std::uniform_real_distribution<float> ang(0.0f, 2.0f * kPi);
        std::uniform_real_distribution<float> rx(0.0f, kScreenW);
        std::uniform_real_distribution<float> ry(kReadoutY - 90.0f, kReadoutY + 90.0f);
        const float readoutW = kReadoutStep * kReadoutLength;

        // 数字が1本決まるたびに、その数字から小さな放電
        int order = 0;
        for (int i = 0; i < kReadoutLength; ++i) {
            if (kReadoutText[i] == '.') { continue; }
            if (crossed(kLockStart + order * kLockGap)) {
                const float cx = (kScreenW - readoutW) * 0.5f + (i + 0.5f) * kReadoutStep;
                SpawnBolt(cx, kReadoutY, ang(rng_), 220.0f + 160.0f * std::abs(unit(rng_)), 0.5f, 6);
            }
            ++order;
        }
        // ゆらぎの間は、乱れが強いほど頻繁にバチッとなる
        if (t >= kFluxStart && t < kReadoutEnd) {
            const float flux = Clamp01((t - kFluxStart) / (kReadoutEnd - kFluxStart));
            if (std::abs(unit(rng_)) < 0.1f + flux * 0.5f) {
                SpawnBolt(rx(rng_), ry(rng_), ang(rng_), 250.0f + 300.0f * std::abs(unit(rng_)), 0.5f + 0.5f * flux, 5);
            }
        }
        // 数字が終わる瞬間：画面いっぱいの大きな放電
        if (crossed(kReadoutEnd - 0.05f)) {
            for (int k = 0; k < 6; ++k) {
                SpawnBolt(rx(rng_), kReadoutY + unit(rng_) * 60.0f, ang(rng_), 600.0f + 450.0f * std::abs(unit(rng_)), 1.1f, 12);
            }
        }
        // 名前が決まった瞬間：左右へ走る放電
        const float titleDone = kTitleStart + kTitleGap * (kTitleLength - 1) + kScrambleTime;
        if (crossed(titleDone)) {
            const float left = (kScreenW - titleTotalW) * 0.5f;
            SpawnBolt(left, kTitleY, kPi, 420.0f, 1.0f, 10);
            SpawnBolt(left + titleTotalW, kTitleY, 0.0f, 420.0f, 1.0f, 10);
            for (int k = 0; k < 3; ++k) {
                SpawnBolt(left + titleTotalW * (0.2f + 0.3f * k), kTitleY, ang(rng_), 300.0f, 0.7f, 8);
            }
        }
        // 名前の乱れ（バースト）の始まりでバチッ
        if (burst > 0.0f && std::fmod(prev, 1.9f) >= 0.12f) {
            SpawnBolt(rx(rng_), kTitleY + unit(rng_) * 40.0f, ang(rng_), 300.0f + 200.0f * std::abs(unit(rng_)), 0.6f, 5);
            SpawnBolt(rx(rng_), kTitleY + unit(rng_) * 40.0f, ang(rng_), 300.0f + 200.0f * std::abs(unit(rng_)), 0.6f, 5);
        }
        UpdateBolts();
    }

    // Enter / Space / Esc / ゲームパッドAボタンでスキップ、または最後まで見たらタイトルへ
    Input& input = *ctx_->input;
    const bool skip = input.TriggerKey(DIK_RETURN) || input.TriggerKey(DIK_SPACE) || input.TriggerKey(DIK_ESCAPE) || input.TriggerGamepadButton(0);
    if (skip || t >= kFadeOutStart + kFadeOut) {
        ctx_->sceneManager->ChangeScene<TitleScene>();
    }
}

void StartupScene::Draw()
{
    ctx_->spriteCommon->PreDraw();
    background_->Draw();
    if (time_ < 0.7f) {
        intro_->Draw();
    }
    noise_->Draw(); // 文字の後ろに敷く
    band_->Draw();
    bandLine_->Draw();
    for (int i = 0; i < kTitleLength; ++i) {
        if (ghostOn_[i]) {
            ghostR_[i]->Draw();
            ghostB_[i]->Draw();
        }
    }
    for (int i = 0; i < kReadoutLength; ++i) {
        if (shownReadout_[i] >= 0) {
            readout_[i]->Draw();
        }
    }
    for (int i = 0; i < kTitleLength; ++i) {
        if (shownTitle_[i] >= 0) {
            title_[i]->Draw();
        }
    }
    for (int i = 0; i < kSubLength; ++i) {
        if (shownSub_[i] >= 0) {
            sub_[i]->Draw();
        }
    }
    if (time_ >= kLineStart) {
        underline_->Draw();
    }
    for (int i = 0; i < kBoltCount; ++i) {
        if (bolts_[i].life > 0) {
            bolts_[i].sprite->Draw();
        }
    }
    for (auto& bar : bars_) {
        bar->Draw();
    }
    flash_->Draw();
    scan_->Draw();
}

void StartupScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: スタートアップ（Divergence Engine）");
    ImGui::Separator();
    ImGui::Text("Enter / Space / Esc / ゲームパッドA でスキップ");
    ImGui::End();
#endif // USE_IMGUI
}

void StartupScene::Finalize()
{
    background_.reset();
    scan_.reset();
    for (int i = 0; i < kReadoutLength; ++i) {
        readout_[i].reset();
    }
    for (int i = 0; i < kTitleLength; ++i) {
        title_[i].reset();
    }
    for (int i = 0; i < kSubLength; ++i) {
        sub_[i].reset();
    }
    underline_.reset();
    bars_.clear();
    flash_.reset();
    noise_.reset();
    band_.reset();
    bandLine_.reset();
    intro_.reset();
    for (int i = 0; i < kTitleLength; ++i) {
        ghostR_[i].reset();
        ghostB_[i].reset();
    }
    for (int i = 0; i < kBoltCount; ++i) {
        bolts_[i].sprite.reset();
    }
}

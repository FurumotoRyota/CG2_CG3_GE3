#include "Tumbleweeds.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
    constexpr float kHalfViewX = 22.0f;     // 画面の中心からこの距離の風上側に現れる（画面の外）
    constexpr float kHalfViewZ = 9.0f;      // 中心から前後にこの範囲の行を通る
    constexpr float kDespawnX = 26.0f;      // 中心からこの距離だけ風下へ行ったら消す
    constexpr float kDyingTime = 0.25f;     // ぶつかってから消えるまで
    constexpr float kLookAhead = 0.6f;      // この先に壁があれば縮み始める
    constexpr float kClearAhead = 12.0f;    // 出す行は、風下へこの距離ぶん壁・建物が無い行だけ
    constexpr float kFadeInTime = 0.4f;     // 出てから、ふくらんで現れるまで
    constexpr float kMinSpawnInterval = 3.0f;
    constexpr float kMaxSpawnInterval = 8.0f;
    constexpr float kPi = 3.14159265f;

    float Random01() { return static_cast<float>(std::rand() % 1000) * 0.001f; }
    float RandomRange(float a, float b) { return a + (b - a) * Random01(); }
}

void Tumbleweeds::Initialize(Object3dCommon* common, const char* modelName)
{
    pool_.Initialize(common, modelName, kMax * 2);
    weeds_.clear();
    weeds_.reserve(kMax);
    spawnTimer_ = 2.0f;
}

void Tumbleweeds::Step(float dt, const MapData& map, const Vector2& focus)
{
    // 次を出す
    spawnTimer_ -= dt;
    if (spawnTimer_ <= 0.0f) {
        spawnTimer_ = RandomRange(kMinSpawnInterval, kMaxSpawnInterval);
        if (weeds_.size() < kMax) {
            Weed w;
            // 風上の画面の外に出す（マップの端の壁の内側 1.5 まで）。風下へ転がれる行を、何回か試して選ぶ
            w.pos.x = std::clamp(focus.x - kHalfViewX, 1.5f, static_cast<float>(map.width) - 1.5f);
            bool found = false;
            for (int attempt = 0; attempt < 10 && !found; ++attempt) {
                w.pos.y = std::clamp(focus.y + RandomRange(-kHalfViewZ, kHalfViewZ), 1.5f, static_cast<float>(map.height) - 1.5f);
                found = true;
                for (float ahead = 0.0f; ahead <= kClearAhead; ahead += 0.5f) {
                    if (map.IsSolid(static_cast<int>(std::floor(w.pos.x + ahead)), static_cast<int>(std::floor(w.pos.y)))) {
                        found = false;
                        break;
                    }
                }
            }
            if (!found) {
                spawnTimer_ = 1.0f; // 今は通れる行が無い：少しあとにもう一度
            }
            else {
                w.core = pool_.Acquire({ w.pos.x, 0.5f, w.pos.y });
                w.cross = pool_.Acquire({ w.pos.x, 0.5f, w.pos.y });
                if (w.core && w.cross) {
                    w.speed = RandomRange(2.5f, 4.5f);
                    w.size = RandomRange(0.55f, 0.9f);
                    w.hopPhase = Random01() * 2.0f * kPi;
                    w.hopHeight = RandomRange(0.2f, 0.6f);
                    w.wobblePhase = Random01() * 2.0f * kPi;
                    for (GameObject* g : { w.core, w.cross }) {
                        if (Material* m = g->GetObject3d().GetMaterial()) {
                            m->color = Vector4{ 0.58f, 0.44f, 0.24f, 1.0f };
                        }
                    }
                    Apply(w);
                    weeds_.push_back(w);
                }
                else {
                    if (w.core) { pool_.Release(w.core); }
                    if (w.cross) { pool_.Release(w.cross); }
                }
            }
        }
    }

    // 動かす
    for (size_t i = 0; i < weeds_.size();) {
        Weed& w = weeds_[i];

        if (w.dying >= 0.0f) {
            w.dying += dt;
            if (w.dying >= kDyingTime) {
                Remove(i);
                continue;
            }
        }
        else {
            // 風下(+x)へ転がる。少し蛇行して、弾む
            w.age += dt;
            w.wobblePhase += dt * 0.9f;
            w.pos.x += w.speed * dt;
            w.pos.y += std::sin(w.wobblePhase) * 0.5f * dt;
            w.hopPhase += dt * 6.0f;
            w.spin -= (w.speed / (w.size * 0.5f)) * dt * 0.5f; // 進んだぶんだけ回る（弾むので少し控えめ）

            // この先が壁・建物なら縮み始める。画面から十分離れたら消す
            const int cellX = static_cast<int>(std::floor(w.pos.x + kLookAhead));
            const int cellZ = static_cast<int>(std::floor(w.pos.y));
            if (map.IsSolid(cellX, cellZ)) {
                w.dying = 0.0f;
            }
            else if (w.pos.x > focus.x + kDespawnX) {
                Remove(i);
                continue;
            }
        }
        Apply(w);
        ++i;
    }
}

void Tumbleweeds::Apply(const Weed& w)
{
    // ぶつかった後は急に縮む
    float scale = w.size * (std::min)(1.0f, w.age / kFadeInTime); // 出た直後はふくらんで現れる
    if (w.dying >= 0.0f) {
        scale *= (std::max)(0.0f, 1.0f - w.dying / kDyingTime);
    }
    const float hop = std::fabs(std::sin(w.hopPhase)) * w.hopHeight;
    const float y = w.size * 0.5f + hop;

    Transform& a = w.core->GetObject3d().GetTransform();
    a.Translate = { w.pos.x, y, w.pos.y };
    a.Scale = { scale, scale, scale };
    a.Rotate = { 0.3f, 0.0f, w.spin };

    Transform& b = w.cross->GetObject3d().GetTransform();
    b.Translate = { w.pos.x, y, w.pos.y };
    b.Scale = { scale * 0.9f, scale * 0.9f, scale * 0.9f };
    b.Rotate = { 0.8f, 0.78f, w.spin * 0.7f + 0.5f };
}

void Tumbleweeds::Remove(size_t index)
{
    pool_.Release(weeds_[index].core);
    pool_.Release(weeds_[index].cross);
    weeds_[index] = weeds_.back(); // 末尾と入れ替えて取り除く
    weeds_.pop_back();
}

void Tumbleweeds::Update(const Matrix4x4& view, const Matrix4x4& proj)
{
    pool_.Update(view, proj);
}

void Tumbleweeds::Draw()
{
    pool_.Draw();
}

void Tumbleweeds::Clear()
{
    pool_.ReleaseAll();
    weeds_.clear();
}

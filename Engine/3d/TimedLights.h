#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "../base/ShaderTypes.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

/// <summary>
/// 夜に灯る明かり（窓の灯り・街灯・ランタン・焚き火など）。暗さ（TimeOfDay の darkness）に応じて、自動で点灯する。
/// 登録した明かりのうち、見ている場所に近いものから最大16個を、ライトとして DirectionalLight に書く。
/// 使い方:
///   TimedLights lamps;
///   TimedLights::Lamp l;  l.position = { 12, 1.8f, 7 };  l.color = { 1.0f, 0.65f, 0.3f };
///   lamps.Add(l);                                                      // 登録（マップを作り直すたびに Clear → Add）
///   lamps.Apply(*directionalLight, tod.darkness, tod.clock, focus);   // 毎フレーム（RenderLook を使うなら不要）
/// </summary>
class TimedLights
{
public:
    struct Lamp
    {
        Vector3 position = { 0.0f, 1.8f, 0.0f };
        Vector3 color = { 1.0f, 0.65f, 0.30f }; // あたたかい灯り
        float intensity = 2.5f;
        float range = 7.0f;     // 光が届く距離
        float decay = 2.0f;     // 距離による弱まり方
        float flicker = 0.12f;  // ゆらぎの大きさ（0でゆらがない。焚き火は大きめ）
        float turnOnAt = 0.45f; // この暗さ（0〜1）で点灯し始める
    };

    void Clear() { lamps_.clear(); }
    void Add(const Lamp& lamp) { lamps_.push_back(lamp); }
    size_t Count() const { return lamps_.size(); }

    // darkness: 暗さ（0〜1）、time: 経過秒（ゆらぎ用）、focus: 見ている場所（近いものを優先して使う）。
    // 点灯するものを light.lights に書いて、light.lightCount を設定する（ほかのライトを使うなら、このあとに足すこと）
    void Apply(DirectionalLight& light, float darkness, float time, const Vector3& focus) const
    {
        light.lightCount = 0;
        if (lamps_.empty()) {
            return;
        }
        // 近い順に並べる
        std::vector<size_t> order(lamps_.size());
        for (size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        auto dist2 = [&](size_t i) {
            const Vector3& p = lamps_[i].position;
            const float dx = p.x - focus.x;
            const float dz = p.z - focus.z;
            return dx * dx + dz * dz;
        };
        const size_t limit = (std::min)(order.size(), static_cast<size_t>(kMaxLights));
        std::partial_sort(order.begin(), order.begin() + limit, order.end(), [&](size_t a, size_t b) { return dist2(a) < dist2(b); });

        int count = 0;
        for (size_t n = 0; n < limit; ++n) {
            const Lamp& l = lamps_[order[n]];
            // 点灯の具合：turnOnAt の少し手前からじわっと明るくなる
            float on = (darkness - (l.turnOnAt - 0.15f)) / 0.3f;
            on = on < 0.0f ? 0.0f : (on > 1.0f ? 1.0f : on);
            if (on <= 0.0f) {
                continue;
            }
            const float phase = l.position.x * 12.9898f + l.position.z * 78.233f;
            float flick = 1.0f + l.flicker * (0.5f * std::sin(time * 13.0f + phase) + 0.3f * std::sin(time * 7.1f + phase * 2.3f));
            flick = flick < 0.2f ? 0.2f : flick;

            LightData& d = light.lights[count++];
            d.color = { l.color.x, l.color.y, l.color.z, 1.0f };
            d.position = l.position;
            d.intensity = l.intensity * on * flick;
            d.direction = { 0.0f, -1.0f, 0.0f };
            d.range = l.range;
            d.decay = l.decay;
            d.cosAngle = -1.0f;
            d.cosFalloffStart = -1.0f;
            d.type = 0; // ポイントライト
        }
        light.lightCount = count;
    }

private:
    std::vector<Lamp> lamps_;
};

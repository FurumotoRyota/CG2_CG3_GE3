#pragma once
// トースト（画面の上にしばらく出て消える短いお知らせ。「SAVED」「NEW RECORD!」など）（ヘッダーだけで使える）
//
//   UiToast toast_;
//   toast_.Show("SAVED");                  // 2秒出して消える
//   toast_.Show("NEW RECORD!", 3.0f);      // 秒数を指定
//
//   toast_.Update(dt);                     // 毎フレーム
//   toast_.Draw(ui_);                      // Draw で（UiRenderer に描いてもらう）
//
//   ・同時に出せるのは最大4つ。多いときは古いものから消える
#include <algorithm>
#include <string>
#include <vector>

#include "UiCommon.h"
#include "base/WinApp.h"
#include "UiRenderer.h"

class UiToast
{
public:
    static constexpr int kMaxItems = 4;

    void Show(const std::string& text, float seconds = 2.0f)
    {
        if (static_cast<int>(items_.size()) >= kMaxItems) {
            items_.erase(items_.begin());
        }
        const float life = (std::max)(0.1f, seconds);
        items_.push_back({ text, life, life });
    }

    void Update(float dt)
    {
        for (Item& item : items_) {
            item.remaining -= dt;
        }
        items_.erase(std::remove_if(items_.begin(), items_.end(), [](const Item& i) { return i.remaining <= 0.0f; }), items_.end());
    }

    int Count() const { return static_cast<int>(items_.size()); }
    void Clear() { items_.clear(); }

    void Draw(UiRenderer& ui) const
    {
        float y = static_cast<float>(WinApp::kGameHeight) * 0.12f;
        for (const Item& item : items_) {
            // 出るときと消えるときに、すっと透明度を変える
            const float elapsed = item.total - item.remaining;
            const float alpha = (std::min)((std::min)(elapsed / 0.15f, item.remaining / 0.4f), 1.0f);
            ui.DrawLabel({ static_cast<float>(WinApp::kGameWidth) * 0.5f, y }, item.text, 36.0f, Ui::WithAlpha(Ui::Rgba(255, 240, 200), alpha));
            y += 46.0f;
        }
    }

private:
    struct Item
    {
        std::string text;
        float remaining; // 残り秒数
        float total;     // 出す秒数
    };
    std::vector<Item> items_;
};

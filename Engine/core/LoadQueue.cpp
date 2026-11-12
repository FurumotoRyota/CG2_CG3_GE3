#include "LoadQueue.h"

#include <cstdio>

#include "../base/DirectXCommon.h"
#include "../math/Vector2.h"

#ifdef USE_IMGUI
#include "../editor/EditorLayout.h"
#endif // USE_IMGUI

void LoadQueue::Add(const std::string& label, Step step)
{
    steps_.push_back({ label, std::move(step) });
}

void LoadQueue::Clear()
{
    steps_.clear();
    next_ = 0;
    waited_ = 0;
    frames_ = 0;
}

bool LoadQueue::Tick(DirectXCommon* dxCommon)
{
    ++frames_;
    if (IsDone()) {
        return true;
    }
    if (waited_ < waitFrames) {
        ++waited_;
        return false;
    }
    if (next_ < steps_.size()) {
        steps_[next_].step();
        ++next_;
        if (dxCommon) {
            dxCommon->ExecuteAndWait(); // 1段階ごとに、積んだアップロードをここで確定させる
        }
    }
    return IsDone();
}

void LoadQueue::RunAll(DirectXCommon* dxCommon)
{
    while (next_ < steps_.size()) {
        steps_[next_].step();
        ++next_;
        if (dxCommon) {
            dxCommon->ExecuteAndWait();
        }
    }
}

float LoadQueue::Progress() const
{
    if (steps_.empty()) {
        return 0.0f;
    }
    return static_cast<float>(next_) / static_cast<float>(steps_.size());
}

const std::string& LoadQueue::CurrentLabel() const
{
    static const std::string kEmpty;
    return next_ < steps_.size() ? steps_[next_].label : kEmpty;
}

void LoadQueue::DrawNowLoading(float screenW, float screenH, unsigned int color)
{
#ifdef USE_IMGUI
    const int dots = (frames_ / 20) % 4;
    const char* dotText[4] = { "NOW LOADING", "NOW LOADING.", "NOW LOADING..", "NOW LOADING..." };
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(Progress() * 100.0f));
    EditorLayout::DrawViewportText({ screenW - 190.0f, screenH - 70.0f }, dotText[dots], 34.0f, color);
    EditorLayout::DrawViewportText({ screenW - 190.0f, screenH - 30.0f }, buf, 26.0f, color);
#else
    (void)screenW;
    (void)screenH;
    (void)color;
#endif // USE_IMGUI
}

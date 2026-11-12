#pragma once
#include <Windows.h>
#include <timeapi.h>
#include <chrono>
#include <cstdint>
#include <thread>

#pragma comment(lib, "winmm.lib") // timeBeginPeriod / timeEndPeriod

/// <summary>
/// Fixed frame rate controller.
/// Call Update() once at the end of every frame; it waits until the target frame time has passed.
/// </summary>
class FrameRateController
{
public:
    FrameRateController() = default;
    ~FrameRateController() { Finalize(); }

    FrameRateController(const FrameRateController&) = delete;
    FrameRateController& operator=(const FrameRateController&) = delete;

    void Initialize(uint32_t targetFps = 60)
    {
        targetFrameTime_ = std::chrono::duration<double>(1.0 / static_cast<double>(targetFps));
        // Make Sleep()-based waiting accurate to about 1ms
        timeBeginPeriod(1);
        timerPeriodSet_ = true;
        reference_ = std::chrono::steady_clock::now();
    }

    void Finalize()
    {
        if (timerPeriodSet_) {
            timeEndPeriod(1);
            timerPeriodSet_ = false;
        }
    }

    // Call once per frame (at the end of the frame)
    void Update()
    {
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;

        // Sleep for most of the remaining time, then spin for the last ~1ms
        const auto sleepMargin = 1ms;
        while (Clock::now() - reference_ < targetFrameTime_ - sleepMargin) {
            std::this_thread::sleep_for(1ms);
        }
        while (Clock::now() - reference_ < targetFrameTime_) {
            std::this_thread::yield();
        }

        const auto now = Clock::now();
        deltaTime_ = std::chrono::duration<float>(now - reference_).count();
        reference_ = now;
    }

    // Duration of the last frame in seconds
    float GetDeltaTime() const { return deltaTime_; }

private:
    std::chrono::duration<double> targetFrameTime_{ 1.0 / 60.0 };
    std::chrono::steady_clock::time_point reference_{};
    float deltaTime_ = 1.0f / 60.0f;
    bool timerPeriodSet_ = false;
};

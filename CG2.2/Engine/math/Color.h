#pragma once
#include <cmath>
#include "../math/Vector4.h"

// 色相(0～360度)からRGBを生成する（彩度・明度は最大固定）
inline Vector4 HueToColor(float hueDegrees)
{
    float h = std::fmod(hueDegrees, 360.0f) / 60.0f;
    if (h < 0.0f) {
        h += 6.0f;
    }
    const float c = 1.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 1.0f) { r = c; g = x; b = 0.0f; }
    else if (h < 2.0f) { r = x; g = c; b = 0.0f; }
    else if (h < 3.0f) { r = 0.0f; g = c; b = x; }
    else if (h < 4.0f) { r = 0.0f; g = x; b = c; }
    else if (h < 5.0f) { r = x; g = 0.0f; b = c; }
    else { r = c; g = 0.0f; b = x; }

    return Vector4{ r, g, b, 1.0f };
}

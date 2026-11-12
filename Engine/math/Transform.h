#pragma once
#include "../math/Vector3.h"

// Scale / Rotate(ラジアン) / Translate をまとめた構造体
struct Transform
{
    Vector3 Scale;
    Vector3 Rotate;
    Vector3 Translate;
};

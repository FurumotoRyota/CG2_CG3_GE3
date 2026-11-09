#pragma once
// 【部品】画面の座標 ⇔ ワールドの座標
//   マウスで狙う・クリックした場所を調べる・敵の頭上にHPを出す、など「画面」と「世界」をつなぐときに使う。
//   カメラが真上でも斜めでも、どの向きでも使える（view / proj には、いま描いているカメラの行列を渡す）。
//
//   ScreenToGround : 画面のピクセル → その位置から出した視線が「高さ groundY の水平な面」と交わる点
//   WorldToScreen  : ワールドの点 → 画面のピクセル（左上が原点）
//
//   ピクセルは「ゲーム画面の座標」（WinApp::kGameWidth x kGameHeight）。EditorLayout の ViewportInput::mouse がそのまま使える。
#include <cmath>

#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"

namespace ScreenProjection
{
    // 画面のピクセル → 高さ groundY の水平な面上の点。面と交わらない（視線が水平・面が後ろ）ときは false
    inline bool ScreenToGround(const Vector2& pixel, const Matrix4x4& view, const Matrix4x4& proj,
        float screenW, float screenH, float groundY, Vector3& out)
    {
        // ピクセル → 正規化デバイス座標（x: -1..1 左→右、y: -1..1 下→上、z: 0=手前 1=奥）
        const float nx = pixel.x / screenW * 2.0f - 1.0f;
        const float ny = 1.0f - pixel.y / screenH * 2.0f;

        const Matrix4x4 invViewProj = Inverse(Multiply(view, proj));
        const Vector3 nearPoint = Matrix4x4::Transform({ nx, ny, 0.0f }, invViewProj);
        const Vector3 farPoint = Matrix4x4::Transform({ nx, ny, 1.0f }, invViewProj);

        const Vector3 dir = farPoint.Subtract(nearPoint);
        if (std::fabs(dir.y) < 0.00001f) {
            return false; // 視線が面と平行
        }
        const float t = (groundY - nearPoint.y) / dir.y;
        if (t < 0.0f) {
            return false; // 面がカメラの後ろ
        }
        out = nearPoint.Add(dir.Multiply(t));
        return true;
    }

    // ワールドの点 → 画面のピクセル。カメラの後ろにある点は false
    inline bool WorldToScreen(const Vector3& world, const Matrix4x4& view, const Matrix4x4& proj,
        float screenW, float screenH, Vector2& out)
    {
        const Matrix4x4 viewProj = Multiply(view, proj);
        const float w = world.x * viewProj.m[0][3] + world.y * viewProj.m[1][3] + world.z * viewProj.m[2][3] + viewProj.m[3][3];
        if (w <= 0.0f) {
            return false; // カメラの後ろ
        }
        const Vector3 ndc = Matrix4x4::Transform(world, viewProj);
        out = { (ndc.x * 0.5f + 0.5f) * screenW, (0.5f - ndc.y * 0.5f) * screenH };
        return true;
    }
}

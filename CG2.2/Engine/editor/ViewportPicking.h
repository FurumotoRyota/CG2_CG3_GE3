#pragma once
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"

/// <summary>
/// Viewport上のクリック判定に使う計算（ImGuiには依存しない）
/// 座標は「ゲーム画面のピクセル座標」（左上が原点、kGameWidth x kGameHeight）
/// </summary>
namespace Picking
{
    struct Ray
    {
        Vector3 origin;
        Vector3 direction; // 正規化済み
    };

    // ゲーム画面のピクセル座標からカメラのレイ（視線）を作る
    Ray ScreenToRay(const Vector2& gamePos, const Matrix4x4& view, const Matrix4x4& projection);

    // Transformを適用したモデルの外接ボックス(boundsMin～boundsMax)とレイの交差判定。交差したら距離tを返す
    bool RayIntersectsBox(const Ray& ray, const Transform& transform, const Vector3& boundsMin, const Vector3& boundsMax, float& outT);

    // レイと平面の交点（平行なら false）
    bool RayIntersectsPlane(const Ray& ray, const Vector3& planePoint, const Vector3& planeNormal, Vector3& outPoint);

    // ワールド座標をゲーム画面のピクセル座標に変換する（カメラの後ろなら false）
    bool WorldToScreen(const Vector3& world, const Matrix4x4& view, const Matrix4x4& projection, Vector2& outScreen);

    // スプライト(左上原点・回転Zのみ考慮)の矩形内に点があるか
    bool PointInSprite(const Vector2& point, const Transform& transform, const Vector2& size);

    // スプライトの4隅（ゲーム画面ピクセル座標）
    void GetSpriteCorners(const Transform& transform, const Vector2& size, Vector2 outCorners[4]);
}

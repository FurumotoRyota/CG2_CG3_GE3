#include "ViewportPicking.h"
#include "../base/WinApp.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // 行ベクトル×行列（w除算あり）。wは変換後のw
    Vector3 TransformPoint(const Vector3& v, const Matrix4x4& m, float& outW)
    {
        const float x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
        const float y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
        const float z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
        outW = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
        if (std::fabs(outW) < 1e-8f) {
            return { x, y, z };
        }
        return { x / outW, y / outW, z / outW };
    }

    // 平行移動なしの方向ベクトル変換
    Vector3 TransformDirection(const Vector3& v, const Matrix4x4& m)
    {
        return {
            v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
            v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]
        };
    }
}

namespace Picking
{
    Ray ScreenToRay(const Vector2& gamePos, const Matrix4x4& view, const Matrix4x4& projection)
    {
        const float ndcX = gamePos.x / WinApp::kGameWidth * 2.0f - 1.0f;
        const float ndcY = 1.0f - gamePos.y / WinApp::kGameHeight * 2.0f;

        const Matrix4x4 invViewProj = Inverse(Multiply(view, projection));
        float w = 1.0f;
        const Vector3 nearPoint = TransformPoint({ ndcX, ndcY, 0.0f }, invViewProj, w); // 深度0(手前)
        const Vector3 farPoint = TransformPoint({ ndcX, ndcY, 1.0f }, invViewProj, w);  // 深度1(奥)

        Ray ray;
        ray.origin = nearPoint;
        ray.direction = farPoint.Subtract(nearPoint).Normalize();
        return ray;
    }

    bool RayIntersectsBox(const Ray& ray, const Transform& transform, const Vector3& boundsMin, const Vector3& boundsMax, float& outT)
    {
        if (transform.Scale.x == 0.0f || transform.Scale.y == 0.0f || transform.Scale.z == 0.0f) {
            return false;
        }

        // レイをモデルのローカル座標系に変換して、軸に平行なボックスと判定する（tは両座標系で共通）
        const Matrix4x4 world = Matrix4x4::MakeAffineMatrix(transform.Scale, transform.Rotate, transform.Translate);
        const Matrix4x4 invWorld = Inverse(world);
        float w = 1.0f;
        const Vector3 origin = TransformPoint(ray.origin, invWorld, w);
        const Vector3 dir = TransformDirection(ray.direction, invWorld);

        const float o[3] = { origin.x, origin.y, origin.z };
        const float d[3] = { dir.x, dir.y, dir.z };
        const float bmin[3] = { boundsMin.x, boundsMin.y, boundsMin.z };
        const float bmax[3] = { boundsMax.x, boundsMax.y, boundsMax.z };

        float tMin = 0.0f;
        float tMax = (std::numeric_limits<float>::max)();
        for (int axis = 0; axis < 3; ++axis) {
            if (std::fabs(d[axis]) < 1e-8f) {
                if (o[axis] < bmin[axis] || o[axis] > bmax[axis]) {
                    return false;
                }
                continue;
            }
            float t1 = (bmin[axis] - o[axis]) / d[axis];
            float t2 = (bmax[axis] - o[axis]) / d[axis];
            if (t1 > t2) {
                std::swap(t1, t2);
            }
            tMin = (std::max)(tMin, t1);
            tMax = (std::min)(tMax, t2);
            if (tMin > tMax) {
                return false;
            }
        }
        outT = tMin;
        return true;
    }

    bool RayIntersectsPlane(const Ray& ray, const Vector3& planePoint, const Vector3& planeNormal, Vector3& outPoint)
    {
        const float denom = ray.direction.Dot(planeNormal);
        if (std::fabs(denom) < 1e-5f) {
            return false;
        }
        const float t = planePoint.Subtract(ray.origin).Dot(planeNormal) / denom;
        outPoint = ray.origin.Add(ray.direction.Multiply(t));
        return true;
    }

    bool WorldToScreen(const Vector3& world, const Matrix4x4& view, const Matrix4x4& projection, Vector2& outScreen)
    {
        float w = 1.0f;
        const Vector3 ndc = TransformPoint(world, Multiply(view, projection), w);
        if (w <= 1e-5f) {
            return false; // カメラの後ろ
        }
        outScreen = { (ndc.x * 0.5f + 0.5f) * WinApp::kGameWidth, (1.0f - (ndc.y * 0.5f + 0.5f)) * WinApp::kGameHeight };
        return true;
    }

    void GetSpriteCorners(const Transform& transform, const Vector2& size, Vector2 outCorners[4])
    {
        const float sx = size.x * transform.Scale.x;
        const float sy = size.y * transform.Scale.y;
        const float c = std::cos(transform.Rotate.z);
        const float s = std::sin(transform.Rotate.z);
        const Vector2 local[4] = { {0.0f, 0.0f}, {sx, 0.0f}, {sx, sy}, {0.0f, sy} };
        for (int i = 0; i < 4; ++i) {
            outCorners[i] = {
                transform.Translate.x + local[i].x * c - local[i].y * s,
                transform.Translate.y + local[i].x * s + local[i].y * c
            };
        }
    }

    bool PointInSprite(const Vector2& point, const Transform& transform, const Vector2& size)
    {
        const float sx = size.x * transform.Scale.x;
        const float sy = size.y * transform.Scale.y;
        if (sx == 0.0f || sy == 0.0f) {
            return false;
        }
        // 点を回転の逆変換でスプライトのローカル座標に戻して、矩形と比べる
        const float rx = point.x - transform.Translate.x;
        const float ry = point.y - transform.Translate.y;
        const float c = std::cos(transform.Rotate.z);
        const float s = std::sin(transform.Rotate.z);
        const float lx = rx * c + ry * s;
        const float ly = -rx * s + ry * c;
        return (std::min)(0.0f, sx) <= lx && lx <= (std::max)(0.0f, sx)
            && (std::min)(0.0f, sy) <= ly && ly <= (std::max)(0.0f, sy);
    }
}

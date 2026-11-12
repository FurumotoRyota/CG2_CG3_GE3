#pragma once
// 当たり判定の道具（ヘッダーだけで使える）。ゲームのコードから1行で呼べる
//   2D（地面の上）：円・四角・点・線分・レイ
//   3D：球・箱（軸にそろえた箱）・レイ
// 2Dの座標は (横, 奥) など、好きな平面の2軸でよい。3Dシューティングなら (x, z) を渡す
#include <algorithm>
#include <cmath>

#include "Vector2.h"
#include "Vector3.h"

namespace Collision
{
    // 軸にそろえた四角（2D）
    struct Rect
    {
        float minX, minY, maxX, maxY;
    };

    // 軸にそろえた箱（3D）
    struct Box
    {
        Vector3 min;
        Vector3 max;
    };

    //==================================================
    // 2D
    //==================================================
    inline bool PointInCircle(const Vector2& p, const Vector2& c, float r)
    {
        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        return dx * dx + dy * dy <= r * r;
    }

    inline bool PointInRect(const Vector2& p, const Rect& r)
    {
        return p.x >= r.minX && p.x <= r.maxX && p.y >= r.minY && p.y <= r.maxY;
    }

    inline bool CircleCircle(const Vector2& a, float ra, const Vector2& b, float rb)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float rr = ra + rb;
        return dx * dx + dy * dy <= rr * rr;
    }

    // 円と四角
    inline bool CircleRect(const Vector2& c, float r, const Rect& rect)
    {
        const float qx = (std::max)(rect.minX, (std::min)(rect.maxX, c.x)); // 四角の中でいちばん近い点
        const float qy = (std::max)(rect.minY, (std::min)(rect.maxY, c.y));
        const float dx = c.x - qx;
        const float dy = c.y - qy;
        return dx * dx + dy * dy <= r * r;
    }

    inline bool RectRect(const Rect& a, const Rect& b)
    {
        return a.minX <= b.maxX && a.maxX >= b.minX && a.minY <= b.maxY && a.maxY >= b.minY;
    }

    // 円が四角にめり込んでいたら、外へ押し出した中心を返す。めり込んでいなければそのまま返す
    inline Vector2 PushCircleOutOfRect(const Vector2& c, float r, const Rect& rect, bool* hit = nullptr)
    {
        if (hit) { *hit = false; }
        const float qx = (std::max)(rect.minX, (std::min)(rect.maxX, c.x));
        const float qy = (std::max)(rect.minY, (std::min)(rect.maxY, c.y));
        float dx = c.x - qx;
        float dy = c.y - qy;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= r * r) {
            return c;
        }
        if (hit) { *hit = true; }
        if (d2 > 0.000001f) {
            const float d = std::sqrt(d2);
            const float push = r - d;
            return { c.x + dx / d * push, c.y + dy / d * push };
        }
        // 中心が四角の中：いちばん近い辺から外へ出す
        const float left = c.x - rect.minX;
        const float right = rect.maxX - c.x;
        const float bottom = c.y - rect.minY;
        const float top = rect.maxY - c.y;
        const float m = (std::min)((std::min)(left, right), (std::min)(bottom, top));
        if (m == left) { return { rect.minX - r, c.y }; }
        if (m == right) { return { rect.maxX + r, c.y }; }
        if (m == bottom) { return { c.x, rect.minY - r }; }
        return { c.x, rect.maxY + r };
    }

    // 点から線分までの距離
    inline float DistancePointSegment(const Vector2& p, const Vector2& a, const Vector2& b)
    {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float len2 = abx * abx + aby * aby;
        float t = 0.0f;
        if (len2 > 0.000001f) {
            t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / len2;
            t = (std::max)(0.0f, (std::min)(1.0f, t));
        }
        const float dx = p.x - (a.x + abx * t);
        const float dy = p.y - (a.y + aby * t);
        return std::sqrt(dx * dx + dy * dy);
    }

    // 線分（a→b）と円。当たったら true、outT に a からの割合（0〜1）で最初の当たり位置を返す
    inline bool SegmentCircle(const Vector2& a, const Vector2& b, const Vector2& c, float r, float* outT = nullptr)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float fx = a.x - c.x;
        const float fy = a.y - c.y;
        const float qa = dx * dx + dy * dy;
        const float qb = 2.0f * (fx * dx + fy * dy);
        const float qc = fx * fx + fy * fy - r * r;
        if (qc <= 0.0f) { // 始点がもう円の中
            if (outT) { *outT = 0.0f; }
            return true;
        }
        if (qa < 0.000001f) {
            return false;
        }
        const float disc = qb * qb - 4.0f * qa * qc;
        if (disc < 0.0f) {
            return false;
        }
        const float t = (-qb - std::sqrt(disc)) / (2.0f * qa);
        if (t < 0.0f || t > 1.0f) {
            return false;
        }
        if (outT) { *outT = t; }
        return true;
    }

    // 線分（a→b）と四角。当たったら true、outT に a からの割合（0〜1）で最初の当たり位置を返す
    inline bool SegmentRect(const Vector2& a, const Vector2& b, const Rect& rect, float* outT = nullptr)
    {
        float t0 = 0.0f;
        float t1 = 1.0f;
        const float d[2] = { b.x - a.x, b.y - a.y };
        const float o[2] = { a.x, a.y };
        const float mn[2] = { rect.minX, rect.minY };
        const float mx[2] = { rect.maxX, rect.maxY };
        for (int i = 0; i < 2; ++i) {
            if (std::fabs(d[i]) < 0.000001f) {
                if (o[i] < mn[i] || o[i] > mx[i]) {
                    return false;
                }
            }
            else {
                float ta = (mn[i] - o[i]) / d[i];
                float tb = (mx[i] - o[i]) / d[i];
                if (ta > tb) { std::swap(ta, tb); }
                t0 = (std::max)(t0, ta);
                t1 = (std::min)(t1, tb);
                if (t0 > t1) {
                    return false;
                }
            }
        }
        if (outT) { *outT = t0; }
        return true;
    }

    // 線分どうし。交わったら true、outT に (a→b) 上の割合を返す
    inline bool SegmentSegment(const Vector2& a, const Vector2& b, const Vector2& c, const Vector2& d, float* outT = nullptr)
    {
        const float rx = b.x - a.x;
        const float ry = b.y - a.y;
        const float sx = d.x - c.x;
        const float sy = d.y - c.y;
        const float denom = rx * sy - ry * sx;
        if (std::fabs(denom) < 0.000001f) {
            return false; // 平行
        }
        const float t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / denom;
        const float u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / denom;
        if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) {
            return false;
        }
        if (outT) { *outT = t; }
        return true;
    }

    //==================================================
    // 3D
    //==================================================
    inline bool SphereSphere(const Vector3& a, float ra, const Vector3& b, float rb)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        const float rr = ra + rb;
        return dx * dx + dy * dy + dz * dz <= rr * rr;
    }

    inline bool SphereBox(const Vector3& c, float r, const Box& box)
    {
        const float qx = (std::max)(box.min.x, (std::min)(box.max.x, c.x));
        const float qy = (std::max)(box.min.y, (std::min)(box.max.y, c.y));
        const float qz = (std::max)(box.min.z, (std::min)(box.max.z, c.z));
        const float dx = c.x - qx;
        const float dy = c.y - qy;
        const float dz = c.z - qz;
        return dx * dx + dy * dy + dz * dz <= r * r;
    }

    inline bool BoxBox(const Box& a, const Box& b)
    {
        return a.min.x <= b.max.x && a.max.x >= b.min.x &&
               a.min.y <= b.max.y && a.max.y >= b.min.y &&
               a.min.z <= b.max.z && a.max.z >= b.min.z;
    }

    // レイ（origin から dir の向き。dir は正規化済み）と球。当たったら true、outT に距離を返す
    inline bool RaySphere(const Vector3& origin, const Vector3& dir, const Vector3& c, float r, float* outT = nullptr)
    {
        const float mx = origin.x - c.x;
        const float my = origin.y - c.y;
        const float mz = origin.z - c.z;
        const float b = mx * dir.x + my * dir.y + mz * dir.z;
        const float cc = mx * mx + my * my + mz * mz - r * r;
        if (cc > 0.0f && b > 0.0f) {
            return false;
        }
        const float disc = b * b - cc;
        if (disc < 0.0f) {
            return false;
        }
        const float t = (std::max)(0.0f, -b - std::sqrt(disc));
        if (outT) { *outT = t; }
        return true;
    }

    // レイと箱。当たったら true、outT に距離を返す
    inline bool RayBox(const Vector3& origin, const Vector3& dir, const Box& box, float* outT = nullptr)
    {
        float t0 = 0.0f;
        float t1 = 1.0e30f;
        const float o[3] = { origin.x, origin.y, origin.z };
        const float d[3] = { dir.x, dir.y, dir.z };
        const float mn[3] = { box.min.x, box.min.y, box.min.z };
        const float mx[3] = { box.max.x, box.max.y, box.max.z };
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(d[i]) < 0.000001f) {
                if (o[i] < mn[i] || o[i] > mx[i]) {
                    return false;
                }
            }
            else {
                float ta = (mn[i] - o[i]) / d[i];
                float tb = (mx[i] - o[i]) / d[i];
                if (ta > tb) { std::swap(ta, tb); }
                t0 = (std::max)(t0, ta);
                t1 = (std::min)(t1, tb);
                if (t0 > t1) {
                    return false;
                }
            }
        }
        if (outT) { *outT = t0; }
        return true;
    }
}

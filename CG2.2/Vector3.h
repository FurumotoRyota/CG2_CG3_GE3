#pragma once
#include <cmath>

struct Vector3
{
	// 3次元ベクトルの構造体
	float x, y, z;

	// 加算
	Vector3 Add(const Vector3& other) const
	{
		return { x + other.x, y + other.y, z + other.z };
	}

	// 減算
	Vector3 Subtract(const Vector3& other) const
	{
		return { x - other.x, y - other.y, z - other.z };
	}

	// スカラー乗算
	Vector3 Multiply(float scalar) const
	{
		return { x * scalar, y * scalar, z * scalar };
	}

	// 内積
	float Dot(const Vector3& other) const
	{
		return x * other.x + y * other.y + z * other.z;
	}

	// 長さ（ノルム）
	float Length() const
	{
		return std::sqrt(x * x + y * y + z * z);
	}

	// 正規化
	Vector3 Normalize() const
	{
		float len = Length();
		if (len != 0.0f) {
			return { x / len, y / len, z / len };
		}
		return { 0.0f, 0.0f, 0.0f };
	}

	// クロス積
	Vector3 Cross(const Vector3& other) const
	{
		return {
			y * other.z - z * other.y,
			z * other.x - x * other.z,
			x * other.y - y * other.x
		};
	}
};

// フリー関数
inline Vector3 Add(const Vector3& v1, const Vector3& v2)
{
	return v1.Add(v2);
}

inline Vector3 Subtract(const Vector3& v1, const Vector3& v2)
{
	return v1.Subtract(v2);
}

inline Vector3 Multiply(const Vector3& v, float scalar)
{
	return v.Multiply(scalar);
}

inline float Dot(const Vector3& v1, const Vector3& v2)
{
	return v1.Dot(v2);
}

inline float Length(const Vector3& v)
{
	return v.Length();
}

inline Vector3 Normalize(const Vector3& v)
{
	return v.Normalize();
}

inline Vector3 Cross(const Vector3& v1, const Vector3& v2)
{
	return v1.Cross(v2);
}
#pragma once
#include "Vector3.h"
#include <cmath>

struct Matrix4x4 {
	// 4x4行列の構造体
	// m[行][列]
	float m[4][4];

	// 加算
	Matrix4x4 Add(const Matrix4x4& other) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				result.m[i][j] = m[i][j] + other.m[i][j];
			}
		}
		return result;
	}

	// 減算
	Matrix4x4 Subtract(const Matrix4x4& other) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				result.m[i][j] = m[i][j] - other.m[i][j];
			}
		}
		return result;
	}

	// 行列の乗算
	Matrix4x4 Multiply(const Matrix4x4& other) const {
		Matrix4x4 result{};
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				for (int k = 0; k < 4; k++) {
					result.m[i][j] += m[i][k] * other.m[k][j];
				}
			}
		}
		return result;
	}

	// スカラー乗算
	Matrix4x4 Multiply(float scalar) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				result.m[i][j] = m[i][j] * scalar;
			}
		}
		return result;
	}

	// 転置行列
	Matrix4x4 Transpose() const { return { m[0][0], m[1][0], m[2][0], m[3][0], m[0][1], m[1][1], m[2][1], m[3][1], m[0][2], m[1][2], m[2][2], m[3][2], m[0][3], m[1][3], m[2][3], m[3][3] }; }

	// 逆行列
	Matrix4x4 Inverse() const {
		Matrix4x4 result;
		float det;

		result.m[0][0] =
			m[1][1] * m[2][2] * m[3][3] - m[1][1] * m[2][3] * m[3][2] - m[2][1] * m[1][2] * m[3][3] + m[2][1] * m[1][3] * m[3][2] + m[3][1] * m[1][2] * m[2][3] - m[3][1] * m[1][3] * m[2][2];
		result.m[0][1] =
			-m[0][1] * m[2][2] * m[3][3] + m[0][1] * m[2][3] * m[3][2] + m[2][1] * m[0][2] * m[3][3] - m[2][1] * m[0][3] * m[3][2] - m[3][1] * m[0][2] * m[2][3] + m[3][1] * m[0][3] * m[2][2];
		result.m[0][2] =
			m[0][1] * m[1][2] * m[3][3] - m[0][1] * m[1][3] * m[3][2] - m[1][1] * m[0][2] * m[3][3] + m[1][1] * m[0][3] * m[3][2] + m[3][1] * m[0][2] * m[1][3] - m[3][1] * m[0][3] * m[1][2];
		result.m[0][3] =
			-m[0][1] * m[1][2] * m[2][3] + m[0][1] * m[1][3] * m[2][2] + m[1][1] * m[0][2] * m[2][3] - m[1][1] * m[0][3] * m[2][2] - m[2][1] * m[0][2] * m[1][3] + m[2][1] * m[0][3] * m[1][2];

		result.m[1][0] =
			-m[1][0] * m[2][2] * m[3][3] + m[1][0] * m[2][3] * m[3][2] + m[2][0] * m[1][2] * m[3][3] - m[2][0] * m[1][3] * m[3][2] - m[3][0] * m[1][2] * m[2][3] + m[3][0] * m[1][3] * m[2][2];
		result.m[1][1] =
			m[0][0] * m[2][2] * m[3][3] - m[0][0] * m[2][3] * m[3][2] - m[2][0] * m[0][2] * m[3][3] + m[2][0] * m[0][3] * m[3][2] + m[3][0] * m[0][2] * m[2][3] - m[3][0] * m[0][3] * m[2][2];
		result.m[1][2] =
			-m[0][0] * m[1][2] * m[3][3] + m[0][0] * m[1][3] * m[3][2] + m[1][0] * m[0][2] * m[3][3] - m[1][0] * m[0][3] * m[3][2] - m[3][0] * m[0][2] * m[1][3] + m[3][0] * m[0][3] * m[1][2];
		result.m[1][3] =
			m[0][0] * m[1][2] * m[2][3] - m[0][0] * m[1][3] * m[2][2] - m[1][0] * m[0][2] * m[2][3] + m[1][0] * m[0][3] * m[2][2] + m[2][0] * m[0][2] * m[1][3] - m[2][0] * m[0][3] * m[1][2];

		result.m[2][0] =
			m[1][0] * m[2][1] * m[3][3] - m[1][0] * m[2][3] * m[3][1] - m[2][0] * m[1][1] * m[3][3] + m[2][0] * m[1][3] * m[3][1] + m[3][0] * m[1][1] * m[2][3] - m[3][0] * m[1][3] * m[2][1];
		result.m[2][1] =
			-m[0][0] * m[2][1] * m[3][3] + m[0][0] * m[2][3] * m[3][1] + m[2][0] * m[0][1] * m[3][3] - m[2][0] * m[0][3] * m[3][1] - m[3][0] * m[0][1] * m[2][3] + m[3][0] * m[0][3] * m[2][1];
		result.m[2][2] =
			m[0][0] * m[1][1] * m[3][3] - m[0][0] * m[1][3] * m[3][1] - m[1][0] * m[0][1] * m[3][3] + m[1][0] * m[0][3] * m[3][1] + m[3][0] * m[0][1] * m[1][3] - m[3][0] * m[0][3] * m[1][1];
		result.m[2][3] =
			-m[0][0] * m[1][1] * m[2][3] + m[0][0] * m[1][3] * m[2][1] + m[1][0] * m[0][1] * m[2][3] - m[1][0] * m[0][3] * m[2][1] - m[2][0] * m[0][1] * m[1][3] + m[2][0] * m[0][3] * m[1][1];

		result.m[3][0] =
			-m[1][0] * m[2][1] * m[3][2] + m[1][0] * m[2][2] * m[3][1] + m[2][0] * m[1][1] * m[3][2] - m[2][0] * m[1][2] * m[3][1] - m[3][0] * m[1][1] * m[2][2] + m[3][0] * m[1][2] * m[2][1];
		result.m[3][1] =
			m[0][0] * m[2][1] * m[3][2] - m[0][0] * m[2][2] * m[3][1] - m[2][0] * m[0][1] * m[3][2] + m[2][0] * m[0][2] * m[3][1] + m[3][0] * m[0][1] * m[2][2] - m[3][0] * m[0][2] * m[2][1];
		result.m[3][2] =
			-m[0][0] * m[1][1] * m[3][2] + m[0][0] * m[1][2] * m[3][1] + m[1][0] * m[0][1] * m[3][2] - m[1][0] * m[0][2] * m[3][1] - m[3][0] * m[0][1] * m[1][2] + m[3][0] * m[0][2] * m[1][1];
		result.m[3][3] =
			m[0][0] * m[1][1] * m[2][2] - m[0][0] * m[1][2] * m[2][1] - m[1][0] * m[0][1] * m[2][2] + m[1][0] * m[0][2] * m[2][1] + m[2][0] * m[0][1] * m[1][2] - m[2][0] * m[0][2] * m[1][1];

		det = m[0][0] * result.m[0][0] + m[0][1] * result.m[1][0] + m[0][2] * result.m[2][0] + m[0][3] * result.m[3][0];

		if (det != 0.0f) {
			det = 1.0f / det;
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					result.m[i][j] *= det;
				}
			}
			return result;
		}
		return { 0.0f };
	}

	// 単位行列を作成
	static Matrix4x4 MakeIdentity4x4() { return { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }; }

	// 平行移動行列を作成
	static Matrix4x4 MakeTranslateMatrix(const Vector3& translate)
	{
		Matrix4x4 result = MakeIdentity4x4();
		result.m[3][0] = translate.x;
		result.m[3][1] = translate.y;
		result.m[3][2] = translate.z;
		return result;
	}

	// 拡大縮小行列を作成
	static Matrix4x4 MakeScaleMatrix(const Vector3& scale)
	{
		Matrix4x4 result = MakeIdentity4x4();
		result.m[0][0] = scale.x;
		result.m[1][1] = scale.y;
		result.m[2][2] = scale.z;
		return result;
	}

	// 座標変換（行ベクトル形式: ベクトル×行列）
	static Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix)
	{
		Vector3 result;
		result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + 1.0f * matrix.m[3][0];
		result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + 1.0f * matrix.m[3][1];
		result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + 1.0f * matrix.m[3][2];
		float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + 1.0f * matrix.m[3][3];
		if (w != 0.0f) {
			result.x /= w;
			result.y /= w;
			result.z /= w;
		}
		return result;
	}

	// X軸回転行列を作成
	static Matrix4x4 MakeRotateXMatrix(float radian)
	{
		Matrix4x4 result = MakeIdentity4x4();
		float cosTheta = std::cos(radian);
		float sinTheta = std::sin(radian);
		result.m[1][1] = cosTheta;
		result.m[1][2] = sinTheta;
		result.m[2][1] = -sinTheta;
		result.m[2][2] = cosTheta;
		return result;
	}

	// Y軸回転行列を作成
	static Matrix4x4 MakeRotateYMatrix(float radian)
	{
		Matrix4x4 result = MakeIdentity4x4();
		float cosTheta = std::cos(radian);
		float sinTheta = std::sin(radian);
		result.m[0][0] = cosTheta;
		result.m[0][2] = -sinTheta;
		result.m[2][0] = sinTheta;
		result.m[2][2] = cosTheta;
		return result;
	}

	// Z軸回転行列を作成
	static Matrix4x4 MakeRotateZMatrix(float radian)
	{
		Matrix4x4 result = MakeIdentity4x4();
		float cosTheta = std::cos(radian);
		float sinTheta = std::sin(radian);
		result.m[0][0] = cosTheta;
		result.m[0][1] = sinTheta;
		result.m[1][0] = -sinTheta;
		result.m[1][1] = cosTheta;
		return result;
	}

	//3次元アフィン変換行列の作成
	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate)
	{
		Matrix4x4 scaleMatrix = MakeScaleMatrix(scale); // 拡大縮小行列
		Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x); // X軸回転行列
		Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y); // Y軸回転行列
		Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z); // Z軸回転行列
		Matrix4x4 translateMatrix = MakeTranslateMatrix(translate); // 平行移動行列
		return scaleMatrix.Multiply(rotateXMatrix)
			.Multiply(rotateYMatrix)
			.Multiply(rotateZMatrix)
			.Multiply(translateMatrix); // 拡大縮小 → X軸回転 → Y軸回転 → Z軸回転 → 平行移動の順で行列を掛け合わせる
	}


	// 透視投影行列
	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
		Matrix4x4 result = {};
		float cot = 1.0f / std::tan(fovY / 2.0f);
		result.m[0][0] = cot / aspectRatio;
		result.m[1][1] = cot;
		result.m[2][2] = farClip / (farClip - nearClip);
		result.m[2][3] = 1.0f;
		result.m[3][2] = -nearClip * farClip / (farClip - nearClip);
		return result;
	}

	// 正射影行列
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
		Matrix4x4 result = {};
		result.m[0][0] = 2.0f / (right - left);
		result.m[1][1] = 2.0f / (top - bottom);
		result.m[2][2] = 1.0f / (farClip - nearClip);
		result.m[3][0] = (left + right) / (left - right);
		result.m[3][1] = (top + bottom) / (bottom - top);
		result.m[3][2] = nearClip / (nearClip - farClip);
		result.m[3][3] = 1.0f;
		return result;
	}


	//ビューポート変換行列の作成
	static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth)
	{
		Matrix4x4 result = MakeIdentity4x4(); // 単位行列からスタート
		result.m[0][0] = width / 2.0f; // 水平方向のスケーリング
		result.m[1][1] = -height / 2.0f; // 垂直方向のスケーリング（Y軸は上が正なのでマイナス）
		result.m[2][2] = maxDepth - minDepth; // 深度方向のスケーリング
		result.m[3][0] = left + width / 2.0f; // 水平方向の平行移動
		result.m[3][1] = top + height / 2.0f; // 垂直方向の平行移動
		result.m[3][2] = minDepth;            // 深度方向の平行移動
		return result;
	}
};

// フリー関数
inline Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2) { return m1.Add(m2); }

inline Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2) { return m1.Subtract(m2); }

inline Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) { return m1.Multiply(m2); }

inline Matrix4x4 Multiply(const Matrix4x4& m, float scalar) { return m.Multiply(scalar); }

inline Matrix4x4 Transpose(const Matrix4x4& m) { return m.Transpose(); }

inline Matrix4x4 Inverse(const Matrix4x4& m) { return m.Inverse(); }

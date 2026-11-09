#pragma once
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "../base/BlendMode.h"
#include "../base/ShaderTypes.h"
#include "../base/TextureManager.h"

class SpriteCommon;
struct VertexData;

// 単色の色
struct ColorRGB
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

/// <summary>
/// 単色の四角・三角・線・円をまとめて1回の描画で出すバッチ（2Dゲームの地形・キャラ・エフェクト用）
///  ・座標はスプライトと同じ「ゲーム画面のピクセル座標（左上が原点）」
///  ・色は resources/palette.png（10x10x10段階のパレット）から引く。頂点ごとの色が無いので、
///    UVをパレットの1マスの中心に固定して単色にしている（半透明は Add ブレンドの明るさで表す）
///  ・使い方： Begin() → Add〜() をたくさん → SpriteCommon::PreDraw() → Flush(ブレンド) を必要な回数
///    Flush() は「前回のFlushからここまで」の分だけ描く。1フレームの間はバッファを書き換えない
/// </summary>
class ColorBatch
{
public:
    // パレットの段階数（palette.png と同じ）
    static constexpr int kLevels = 10;
    static constexpr int kPaletteSize = 32;

    void Initialize(SpriteCommon* common, uint32_t maxQuads = 20000);

    // デバイスより先にGPUリソースを解放する
    void Finalize();

    // フレームの最初に呼ぶ
    void Begin();

    // 色 → パレット上のマスの中心UV（単色の描画やモデルの色付けに使う）
    static void ColorToUv(const ColorRGB& color, float& u, float& v);

    void AddRect(float x, float y, float w, float h, const ColorRGB& color);
    // 四角形（左上・右上・右下・左下の順の8個のfloat）。回転した四角に使う
    void AddQuad(const float xy[8], const ColorRGB& color);
    void AddTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const ColorRGB& color);
    void AddLine(float ax, float ay, float bx, float by, float thickness, const ColorRGB& color);
    void AddDisc(float cx, float cy, float radius, const ColorRGB& color, int segments = 14);

    // ここまでに追加した分を描く
    void Flush(BlendMode mode);

private:
    void AddVertex(float x, float y, float u, float v);

    SpriteCommon* common_ = nullptr;
    TextureManager::TextureHandle textureHandle_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    VertexData* vertexData_ = nullptr;
    uint32_t maxVertices_ = 0;
    uint32_t cursor_ = 0;   // 次に書く頂点の位置
    uint32_t flushed_ = 0;  // ここまでは描画済み

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
};

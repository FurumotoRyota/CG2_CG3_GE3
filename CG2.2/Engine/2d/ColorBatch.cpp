#include "ColorBatch.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#include "../2d/SpriteCommon.h"
#include "../core/FrameStats.h"
#include "../3d/VertexData.h"
#include "../base/BasicPipeline.h"
#include "../base/DirectXCommon.h"
#include "../base/WinApp.h"

void ColorBatch::Initialize(SpriteCommon* common, uint32_t maxQuads)
{
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // パレット画像（Initialize中に読んでおくと、アップロードがシーン切り替え時に確定する）
    textureHandle_ = common_->GetTextureManager()->Load("resources/palette.png");

    maxVertices_ = maxQuads * 6;
    const size_t sizeInBytes = sizeof(VertexData) * maxVertices_;
    vertexResource_ = dxCommon->CreateBufferResource(sizeInBytes);
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    std::memset(materialData_, 0, sizeof(Material));
    materialData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->lightingMode = 0; // ライティングなし
    materialData_->alphaCutoff = 0.0f;
    materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();

    // 頂点はもうピクセル座標なので、ワールド行列は単位行列。正射影だけを掛ける
    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    const Matrix4x4 projection = Matrix4x4::MakeOrthographicMatrix(
        0.0f, 0.0f, static_cast<float>(WinApp::kGameWidth), static_cast<float>(WinApp::kGameHeight), 0.1f, 100.0f);
    wvpData_->WVP = projection;
    wvpData_->World = Matrix4x4::MakeIdentity4x4();
    wvpData_->WorldInverseTranspose = Matrix4x4::MakeIdentity4x4();
    wvpData_->LightWVP = Matrix4x4::MakeIdentity4x4();
}

void ColorBatch::Finalize()
{
    vertexResource_.Reset();
    materialResource_.Reset();
    wvpResource_.Reset();
    vertexData_ = nullptr;
    materialData_ = nullptr;
    wvpData_ = nullptr;
}

void ColorBatch::Begin()
{
    cursor_ = 0;
    flushed_ = 0;
}

// 色を 0..9 に丸めて、パレット上のマスの中心のUVにする
void ColorBatch::ColorToUv(const ColorRGB& color, float& u, float& v)
{
    auto level = [](float c) {
        return (std::max)(0, (std::min)(kLevels - 1, static_cast<int>(std::lround(c * (kLevels - 1)))));
        };
    const int index = level(color.r) + kLevels * level(color.g) + kLevels * kLevels * level(color.b);
    const int px = index % kPaletteSize;
    const int py = index / kPaletteSize;
    u = (static_cast<float>(px) + 0.5f) / static_cast<float>(kPaletteSize);
    v = (static_cast<float>(py) + 0.5f) / static_cast<float>(kPaletteSize);
}

void ColorBatch::AddVertex(float x, float y, float u, float v)
{
    VertexData& vd = vertexData_[cursor_++];
    vd.position = { x, y, 0.0f, 1.0f };
    vd.texcoord = { u, v };
    vd.normal = { 0.0f, 0.0f, -1.0f };
}

void ColorBatch::AddTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const ColorRGB& color)
{
    if (cursor_ + 3 > maxVertices_) {
        return; // 満杯のときは捨てる
    }
    // スプライトと同じ向き（画面で時計回りの逆）に揃える。向きが逆だと裏面カリングで消える
    const float cross = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    float u = 0.0f;
    float v = 0.0f;
    ColorToUv(color, u, v);
    if (cross >= 0.0f) {
        AddVertex(x0, y0, u, v);
        AddVertex(x1, y1, u, v);
        AddVertex(x2, y2, u, v);
    }
    else {
        AddVertex(x0, y0, u, v);
        AddVertex(x2, y2, u, v);
        AddVertex(x1, y1, u, v);
    }
}

void ColorBatch::AddQuad(const float xy[8], const ColorRGB& color)
{
    AddTriangle(xy[0], xy[1], xy[2], xy[3], xy[4], xy[5], color);
    AddTriangle(xy[0], xy[1], xy[4], xy[5], xy[6], xy[7], color);
}

void ColorBatch::AddRect(float x, float y, float w, float h, const ColorRGB& color)
{
    const float xy[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    AddQuad(xy, color);
}

void ColorBatch::AddLine(float ax, float ay, float bx, float by, float thickness, const ColorRGB& color)
{
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        return;
    }
    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;
    const float xy[8] = { ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny };
    AddQuad(xy, color);
}

void ColorBatch::AddDisc(float cx, float cy, float radius, const ColorRGB& color, int segments)
{
    const float step = 6.2831853f / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        AddTriangle(cx, cy, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
            cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
}

void ColorBatch::Flush(BlendMode mode)
{
    const uint32_t count = cursor_ - flushed_;
    if (count == 0) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    common_->SetBlendMode(mode);
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootMaterial, materialResource_->GetGPUVirtualAddress());
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootTransform, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        BasicPipeline::kRootTexture, common_->GetTextureManager()->GetSrvHandleGPU(textureHandle_));
    commandList->DrawInstanced(count, 1, flushed_, 0); FrameStats::Count(FrameStats::kSprite, count);
    flushed_ = cursor_;
}

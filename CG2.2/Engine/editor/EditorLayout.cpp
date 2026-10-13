#include "EditorLayout.h"

#ifdef USE_IMGUI
#include <algorithm>
#include <cmath>

#include "../base/WinApp.h"
#include "imgui/imgui.h"

namespace
{
    // 各パネルの大きさ（ピクセル）。変えたい場合はここを編集する
    constexpr float kLeftWidth = 260.0f;    // Hierarchy
    constexpr float kRightWidth = 360.0f;   // Inspector
    constexpr float kBottomHeight = 200.0f; // System

    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    // Viewportの状態（画像が表示されている範囲と、マウス入力）
    EditorLayout::ViewportInput g_input;
    ImVec2 g_imageMin{ 0.0f, 0.0f };
    ImVec2 g_imageMax{ 0.0f, 0.0f };

    void SetNextRect(ImVec2 pos, ImVec2 size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    }
}

namespace EditorLayout
{
    bool Begin(const char* title, Panel panel)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float top = ImGui::GetFrameHeight(); // メニューバーの高さ
        const float middleHeight = display.y - top - kBottomHeight;

        switch (panel) {
        case Panel::Hierarchy:
            SetNextRect({ 0.0f, top }, { kLeftWidth, middleHeight });
            break;
        case Panel::Inspector:
            SetNextRect({ display.x - kRightWidth, top }, { kRightWidth, middleHeight });
            break;
        case Panel::System:
            SetNextRect({ 0.0f, display.y - kBottomHeight }, { display.x, kBottomHeight });
            break;
        }
        return ImGui::Begin(title, nullptr, kPanelFlags);
    }

    void DrawViewport(D3D12_GPU_DESCRIPTOR_HANDLE gameTexture)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float top = ImGui::GetFrameHeight();
        SetNextRect(
            { kLeftWidth, top },
            { display.x - kLeftWidth - kRightWidth, display.y - top - kBottomHeight });

        ImGui::Begin("ビューポート", nullptr,
            kPanelFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // パネルに収まる最大サイズをアスペクト比を保って求め、中央に置く
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float scale = (std::min)(avail.x / WinApp::kGameWidth, avail.y / WinApp::kGameHeight);
        const ImVec2 size = { WinApp::kGameWidth * scale, WinApp::kGameHeight * scale };

        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos({ cursor.x + (avail.x - size.x) * 0.5f, cursor.y + (avail.y - size.y) * 0.5f });
        ImGui::Image((ImTextureID)gameTexture.ptr, size);

        // マウス位置をゲーム画面のピクセル座標に変換して保存する
        g_imageMin = ImGui::GetItemRectMin();
        g_imageMax = ImGui::GetItemRectMax();
        const ImGuiIO& io = ImGui::GetIO();
        const bool hovered = ImGui::IsItemHovered();
        const float imageWidth = g_imageMax.x - g_imageMin.x;
        const float imageHeight = g_imageMax.y - g_imageMin.y;

        g_input.valid = imageWidth > 0.0f && imageHeight > 0.0f;
        if (g_input.valid) {
            g_input.mouse = {
                (io.MousePos.x - g_imageMin.x) / imageWidth * WinApp::kGameWidth,
                (io.MousePos.y - g_imageMin.y) / imageHeight * WinApp::kGameHeight
            };
        }
        g_input.clicked = g_input.valid && hovered && ImGui::IsMouseClicked(0);
        g_input.down = g_input.valid && ImGui::IsMouseDown(0);
        g_input.shift = io.KeyShift;
        g_input.rightClicked = g_input.valid && hovered && ImGui::IsMouseClicked(1);
        g_input.rightDown = g_input.valid && ImGui::IsMouseDown(1);
        g_input.middleClicked = g_input.valid && hovered && ImGui::IsMouseClicked(2);
        g_input.middleDown = g_input.valid && ImGui::IsMouseDown(2);
        g_input.mouseDelta = { io.MouseDelta.x, io.MouseDelta.y };
        g_input.wheel = hovered ? io.MouseWheel : 0.0f;

        ImGui::End();
    }

    const ViewportInput& GetViewportInput()
    {
        return g_input;
    }

    void DrawViewportOverlay(const Vector2* points, int count, bool closed)
    {
        if (!g_input.valid || count < 2) {
            return;
        }
        const float scaleX = (g_imageMax.x - g_imageMin.x) / WinApp::kGameWidth;
        const float scaleY = (g_imageMax.y - g_imageMin.y) / WinApp::kGameHeight;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(g_imageMin, g_imageMax, true); // Viewportの外にはみ出さない
        for (int i = 0; i < count; ++i) {
            const int next = i + 1;
            if (next >= count && !closed) {
                break;
            }
            const Vector2& a = points[i];
            const Vector2& b = points[next % count];
            drawList->AddLine(
                { g_imageMin.x + a.x * scaleX, g_imageMin.y + a.y * scaleY },
                { g_imageMin.x + b.x * scaleX, g_imageMin.y + b.y * scaleY },
                IM_COL32(255, 200, 0, 255), 2.0f);
        }
        drawList->PopClipRect();
    }

    void DrawViewportHandles(const Vector2* points, int count)
    {
        if (!g_input.valid) {
            return;
        }
        const float scaleX = (g_imageMax.x - g_imageMin.x) / WinApp::kGameWidth;
        const float scaleY = (g_imageMax.y - g_imageMin.y) / WinApp::kGameHeight;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(g_imageMin, g_imageMax, true);
        constexpr float kHalf = 5.0f; // 画面上での半径(px)
        for (int i = 0; i < count; ++i) {
            const ImVec2 center = { g_imageMin.x + points[i].x * scaleX, g_imageMin.y + points[i].y * scaleY };
            drawList->AddRectFilled({ center.x - kHalf, center.y - kHalf }, { center.x + kHalf, center.y + kHalf }, IM_COL32(255, 255, 255, 255));
            drawList->AddRect({ center.x - kHalf, center.y - kHalf }, { center.x + kHalf, center.y + kHalf }, IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);
        }
        drawList->PopClipRect();
    }

    void DrawViewportMarker(const Vector2& position, const char* label, unsigned int color, bool selected)
    {
        if (!g_input.valid) {
            return;
        }
        const float scaleX = (g_imageMax.x - g_imageMin.x) / WinApp::kGameWidth;
        const float scaleY = (g_imageMax.y - g_imageMin.y) / WinApp::kGameHeight;
        ImVec2 p = { g_imageMin.x + position.x * scaleX, g_imageMin.y + position.y * scaleY };

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(g_imageMin, g_imageMax, true);

        constexpr float kMargin = 14.0f;
        const bool inside = p.x >= g_imageMin.x && p.x <= g_imageMax.x && p.y >= g_imageMin.y && p.y <= g_imageMax.y;
        const float thickness = selected ? 3.0f : 1.5f;
        const ImU32 shadow = IM_COL32(0, 0, 0, 200);

        if (inside) {
            const float r = selected ? 9.0f : 6.0f;
            drawList->AddCircle(p, r + 1.0f, shadow, 16, thickness + 1.5f);
            drawList->AddCircle(p, r, color, 16, thickness);
            drawList->AddLine({ p.x - r - 4.0f, p.y }, { p.x + r + 4.0f, p.y }, color, thickness);
            drawList->AddLine({ p.x, p.y - r - 4.0f }, { p.x, p.y + r + 4.0f }, color, thickness);
            if (label && label[0] != '\0') {
                const ImVec2 textPos = { p.x + r + 6.0f, p.y - r - 2.0f };
                const ImVec2 size = ImGui::CalcTextSize(label);
                drawList->AddRectFilled({ textPos.x - 2.0f, textPos.y - 1.0f }, { textPos.x + size.x + 2.0f, textPos.y + size.y + 1.0f }, IM_COL32(0, 0, 0, 160));
                drawList->AddText(textPos, color, label);
            }
        }
        else {
            // Off-screen: clamp to the edge and draw a triangle pointing toward the real position
            const ImVec2 center = { (g_imageMin.x + g_imageMax.x) * 0.5f, (g_imageMin.y + g_imageMax.y) * 0.5f };
            ImVec2 edge = {
                p.x < g_imageMin.x + kMargin ? g_imageMin.x + kMargin : (p.x > g_imageMax.x - kMargin ? g_imageMax.x - kMargin : p.x),
                p.y < g_imageMin.y + kMargin ? g_imageMin.y + kMargin : (p.y > g_imageMax.y - kMargin ? g_imageMax.y - kMargin : p.y)
            };
            float dx = p.x - center.x;
            float dy = p.y - center.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.0001f) {
                dx /= len;
                dy /= len;
            }
            const float s = selected ? 10.0f : 7.0f;
            const ImVec2 tip = { edge.x + dx * s, edge.y + dy * s };
            const ImVec2 left = { edge.x - dx * s - dy * s * 0.7f, edge.y - dy * s + dx * s * 0.7f };
            const ImVec2 right = { edge.x - dx * s + dy * s * 0.7f, edge.y - dy * s - dx * s * 0.7f };
            drawList->AddTriangleFilled(tip, left, right, color);
            drawList->AddTriangle(tip, left, right, shadow, 1.5f);
        }
        drawList->PopClipRect();
    }
}
#endif // USE_IMGUI

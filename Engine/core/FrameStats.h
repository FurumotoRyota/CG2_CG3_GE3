#pragma once
// 1フレームの描画量と処理時間を数えるだけの小さな道具（どこからでも呼べる）
//   ・描画命令（DrawInstanced）を出すところで Count を呼ぶ → 回数と三角形の数がたまる
//   ・フレームの頭で BeginFrame を呼ぶと、前のフレームの結果が Last() に入る
#include <chrono>
#include <cstdint>

namespace FrameStats {

enum Kind { kModel = 0, kShadow, kSprite, kParticle, kPlanes, kKindCount };

struct Data
{
    uint32_t calls[kKindCount] = {};   // 描画命令の回数
    uint64_t vertices[kKindCount] = {}; // 描いた頂点の数（三角形 = 頂点 ÷ 3）
    float updateMs = 0.0f;              // Update にかかった時間
    float drawMs = 0.0f;                // Draw で描画命令を作るのにかかった時間（CPU側）
    float frameMs = 0.0f;               // 1フレームの間隔（前回の BeginFrame から今回まで）
    float uiMs = 0.0f;                  // ImGuiの画面を組み立てるのにかかった時間
    float presentMs = 0.0f;             // 画面に出す(Present)＋GPUの終わり待ち。ここが長い＝GPUが重い／VSync待ち
    float sleepMs = 0.0f;               // 60FPS固定のための待ち時間（余った時間）
};

inline Data& Cur() { static Data d; return d; }
inline Data& Last() { static Data d; return d; }

inline void Count(Kind kind, uint64_t vertexCount, uint32_t instances = 1)
{
    Cur().calls[kind] += 1;
    Cur().vertices[kind] += vertexCount * instances;
}

// フレームの頭で1回呼ぶ。前のフレームの結果を Last() に移して、数え直す
inline void BeginFrame()
{
    using clock = std::chrono::steady_clock;
    static clock::time_point prev = clock::now();
    const clock::time_point now = clock::now();
    Cur().frameMs = std::chrono::duration<float, std::milli>(now - prev).count();
    prev = now;
    Last() = Cur();
    Cur() = Data{};
}

inline uint32_t TotalCalls(const Data& d)
{
    uint32_t n = 0;
    for (uint32_t c : d.calls) { n += c; }
    return n;
}
inline uint64_t TotalTriangles(const Data& d, bool includeShadow)
{
    uint64_t v = 0;
    for (int k = 0; k < kKindCount; ++k) {
        if (k == kShadow && !includeShadow) { continue; }
        v += d.vertices[k];
    }
    return v / 3;
}

// 関数が終わるまでの時間を、指定の変数に入れる
struct ScopedMs
{
    float& out;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit ScopedMs(float& o) : out(o) {}
    ~ScopedMs() { out = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count(); }
};

} // namespace FrameStats

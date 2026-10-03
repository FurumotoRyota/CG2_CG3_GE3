#pragma once

/// <summary>
/// ブレンドモード（描く色を、すでに描かれている色とどう混ぜるか）
///  None     ：混ぜない（不透明。今までの描画）
///  Normal   ：通常のαブレンド  結果 = 描く色*α + 背景*(1-α)
///  Add      ：加算             結果 = 背景 + 描く色*α        （光・炎など。明るくなる）
///  Subtract ：減算             結果 = 背景 - 描く色*α        （暗くなる）
///  Multiply ：乗算             結果 = 背景 * 描く色           （影・色を重ねる。暗くなる）
///  Screen   ：スクリーン        結果 = 背景 + 描く色*(1-背景)  （加算より白飛びしにくい）
/// </summary>
enum class BlendMode
{
    None,
    Normal,
    Add,
    Subtract,
    Multiply,
    Screen,
    Count // 個数（配列の大きさ用）
};

inline constexpr int kBlendModeCount = static_cast<int>(BlendMode::Count);

// ImGuiのComboなどに渡す表示名（BlendModeの並びと同じ順）
inline constexpr const char* kBlendModeNames[kBlendModeCount] = {
    "None", "Normal", "Add", "Subtract", "Multiply", "Screen"
};

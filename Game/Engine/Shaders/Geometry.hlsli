// ジオメトリシェーダーのデモ用の共通定義（VS / GS / PS で共有）
// C++ の GeometryPipeline.h の GeoParams と同じ並び
struct GeoParams
{
    float4x4 viewProj; // カメラのビュー × 射影
    float4x4 world;    // ワールド行列
    float4 color;      // 色
    float4 lightDir;   // 光の進む向き(xyz)
    float4 camRight;   // カメラの右方向(ワールド)。ビルボード用
    float4 camUp;      // カメラの上方向(ワールド)。ビルボード用

    float mode;        // モデル加工の種類（0:そのまま 1:爆発 2:縮小 3:回転）
    float amount;      // 加工の量
    float time;        // 経過時間[秒]
    float animate;     // 1なら時間でアニメーションする

    float size;        // 点から作る図形の大きさ
    float useTexture;  // 1ならテクスチャを使う
    float shape;       // 点から作る図形（0:三角形 1:テクスチャ付き四角形）
    float pad;
};
ConstantBuffer<GeoParams> gParams : register(b0);

// ---- モデル加工（80_00 / 80_01） ----
// 頂点シェーダー出力 = ジオメトリシェーダー入力（SV_POSITION はGSで計算する）
struct ModelVSOutput
{
    float3 worldPosition : TEXCOORD0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD1;
};

// ジオメトリシェーダー出力 = ピクセルシェーダー入力
struct ModelGSOutput
{
    float4 svpos : SV_POSITION; // システム用頂点座標
    float3 normal : NORMAL0;    // 法線ベクトル
    float2 uv : TEXCOORD0;      // uv値
};

// ---- 点から図形を作る（80_02 / 80_03） ----
struct PointVSOutput
{
    float3 worldPosition : TEXCOORD0;
    float size : TEXCOORD1;
    float4 color : COLOR0;
    float seed : TEXCOORD2;
};

struct PointGSOutput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

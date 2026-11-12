// スカイボックス：画面全体を覆う三角形を SV_VertexID から作り、各ピクセルの視線方向をPSへ渡す
struct SkyConstants
{
    float4x4 invView;
    float projScaleX; // 射影行列の m[0][0]
    float projScaleY; // 射影行列の m[1][1]
    float mode;       // 0:キューブマップ 1:時間帯の空の色から計算
    float padding;
    float4 horizon;   // 地平線の色
    float4 zenith;    // 真上の色
    float4 ground;    // 地面側の色
    float4 toSun;     // 太陽（月）のある方向（xyz）
    float4 discColor; // 太陽の円盤の色（HDR。沈んでいれば0）
    float4 glowColor; // 太陽まわりのにじみの色
    float4 cloud;     // x=雲の量 y=星の量 z,w=雲の流れた量
    float4 cloudLit;  // rgb=雲の明るい側の色 w=経過秒
    float4 cloudShade;// rgb=雲の暗い側の色
    float4 moonDir;   // 月のある方向
    float4 moonColor; // 月の明るさ
};
ConstantBuffer<SkyConstants> gSky : register(b0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    // (0,0) (2,0) (0,2) → NDC (-1,1) (3,1) (-1,-3) で画面全体を覆う
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    VSOutput output;
    output.position = float4(ndc, 1.0f, 1.0f);

    // ビュー空間の視線方向 → ワールド空間へ（位置は無視して向きだけ使う）
    float3 viewDir = float3(ndc.x / gSky.projScaleX, ndc.y / gSky.projScaleY, 1.0f);
    output.direction = mul(float4(viewDir, 0.0f), gSky.invView).xyz;
    return output;
}

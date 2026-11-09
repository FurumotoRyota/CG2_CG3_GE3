// スカイボックス：画面全体を覆う三角形を SV_VertexID から作り、各ピクセルの視線方向をPSへ渡す
struct SkyConstants
{
    float4x4 invView;
    float projScaleX; // 射影行列の m[0][0]
    float projScaleY; // 射影行列の m[1][1]
    float2 padding;
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

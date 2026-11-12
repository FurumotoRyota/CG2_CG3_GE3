// 板ポリ大量配置用PixelShader：テクスチャの色 × 板の色。Lightingは行わない

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    float4 color = gTexture.Sample(gSampler, input.texcoord) * input.color;

    // 透明な部分は書かない（奥の物が見える）
    if (color.a <= 0.0f)
    {
        discard;
    }
    return color;
}

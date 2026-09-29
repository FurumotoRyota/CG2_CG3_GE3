struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    // texcoordは0~1。中心(0.5,0.5)からの距離で円形のソフトなアルファを作る（テクスチャ不要）
    float2 centered = input.texcoord - 0.5f;
    float dist = length(centered) * 2.0f; // 0(中心)～1(端)
    float alpha = saturate(1.0f - dist);
    alpha = alpha * alpha; // ソフトなフォールオフ

    output.color = float4(input.color.rgb, input.color.a * alpha);
    return output;
}

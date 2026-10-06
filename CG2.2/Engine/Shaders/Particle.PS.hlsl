// パーティクル用PixelShader
// テクスチャなしの粒は、四角のポリゴンの上で「丸」や「ぼかし」を計算で作る。テクスチャありの粒は画像に色を掛ける。Lightingは行わない

Texture2D<float4> gTexture : register(t1);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float shape : TEXCOORD1;
    float useTexture : TEXCOORD2;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    // テクスチャありの粒：画像の色に、粒の色(寿命で変わる色・アルファ)を掛ける
    if (input.useTexture > 0.5f)
    {
        float4 textured = gTexture.Sample(gSampler, input.texcoord) * input.color;
        if (textured.a <= 0.0f)
        {
            discard;
        }
        return textured;
    }

    // 中心からの距離（中心0～ふち1）
    float2 p = input.texcoord * 2.0f - 1.0f;
    float d = length(p);

    float mask = 1.0f;
    if (input.shape < 0.5f)
    {
        // SoftCircle：中心ほど濃く、ふちに向かってなめらかに消える
        float v = saturate(1.0f - d);
        mask = v * v;
    }
    else if (input.shape < 1.5f)
    {
        // Circle：くっきりした丸（ふちだけ少しぼかす）
        mask = 1.0f - smoothstep(0.85f, 1.0f, d);
    }
    // Square：mask = 1 のまま

    float4 color = input.color;
    color.a *= mask;

    // 完全に透明なら書かない
    if (color.a <= 0.0f)
    {
        discard;
    }
    return color;
}

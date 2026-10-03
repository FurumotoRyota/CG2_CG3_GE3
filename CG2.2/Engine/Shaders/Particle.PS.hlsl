// パーティクル用PixelShader
// テクスチャは使わず、四角のポリゴンの上で「丸」や「ぼかし」を計算で作る。Lightingは行わない

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float shape : TEXCOORD1;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
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

#include "Geometry.hlsli"

// サンプル共通のピクセルシェーダー。useTexture=0 なら単色（点・線用）
float4 main(ModelGSOutput input) : SV_TARGET0
{
    if (gParams.useTexture < 0.5f)
    {
        return gParams.color;
    }
    float4 texColor = gTexture.Sample(gSampler, input.uv);
    return float4(texColor.rgb * gParams.color.rgb, texColor.a * gParams.color.a);
}

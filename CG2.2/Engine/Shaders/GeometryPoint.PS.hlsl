#include "Geometry.hlsli"

float4 main(PointGSOutput input) : SV_TARGET0
{
    float4 color = input.color;
    if (gParams.useTexture > 0.5f)
    {
        color *= gTexture.Sample(gSampler, input.uv);
    }
    if (color.a < 0.01f)
    {
        discard; // ほぼ透明なピクセルは描かない
    }
    return color;
}

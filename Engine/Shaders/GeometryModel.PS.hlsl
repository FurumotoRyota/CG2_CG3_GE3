#include "Geometry.hlsli"

float4 main(ModelGSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET0
{
    float3 normal = normalize(input.normal);
    if (!isFrontFace)
    {
        normal = -normal; // 裏面が見えているときは法線を反転して、裏側でも明るさが合うようにする
    }
    float3 L = normalize(-gParams.lightDir.xyz);
    float halfLambert = dot(normal, L) * 0.5f + 0.5f;
    float lit = pow(halfLambert, 2.0f);

    float4 texColor = gTexture.Sample(gSampler, input.uv);
    return float4(texColor.rgb * gParams.color.rgb * lit, texColor.a * gParams.color.a);
}

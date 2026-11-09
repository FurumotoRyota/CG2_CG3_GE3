TextureCube<float4> gEnvironment : register(t0);
SamplerState gSampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET0
{
    float3 color = gEnvironment.SampleLevel(gSampler, normalize(input.direction), 0).rgb;
    // HDR（太陽など1を超える値）を簡単にトーンマップして画面に出す
    color = color / (1.0f + color * 0.15f);
    return float4(color, 1.0f);
}

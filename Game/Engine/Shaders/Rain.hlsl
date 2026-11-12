// 3Dの雨：雨粒の筋と、地面の波紋・しぶきを、頂点バッファなしで描く。
//   位置は全部、インスタンス番号と時間から計算する（CPUは定数を渡すだけ）。
//   雨粒は「カメラのまわりの箱」の中に降る。箱は世界に固定された格子で繰り返すので、カメラが動いても雨粒は流れない。
//   エントリ: VS_Streak（雨粒） / VS_Ripple（波紋・しぶき） / PS_Rain

cbuffer RainParams : register(b0)
{
    float4x4 gViewProj;
    float4 gCamPos;   // xyz=カメラの位置
    float4 gFocus;    // xyz=箱の中心（注目点）, w=経過秒
    float4 gShape;    // x=箱の広さ(XZ), y=箱の高さ, z=落ちる速さ, w=筋の長さ
    float4 gWind;     // x,z=風で流れる速さ(m/s), w=筋の太さ
    float4 gColor;    // rgb=色, a=強さ
    float4 gRipple;   // x=地面の高さ, y=波紋の最大半径, z=波紋の周期(秒), w=波紋を出す範囲(XZ)
}

struct VSOut
{
    float4 pos : SV_POSITION;
    float4 data : TEXCOORD0; // streak: x=along, y=明るさ / ripple: x,y=波紋内の座標(-1〜1), z=age, w=明るさ
    float kind : TEXCOORD1;  // 0:雨粒 1:波紋
};

uint WangHash(uint x)
{
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x = x ^ (x >> 4);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

float Rand(uint seed)
{
    return float(WangHash(seed) & 0xFFFFFFu) / 16777216.0f;
}

// 四角形1枚(6頂点) → 角の番号(0〜3)
static const uint kCorner[6] = { 0, 1, 2, 2, 1, 3 };

VSOut VS_Streak(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    uint corner = kCorner[vid];
    float along = (corner >> 1) == 0 ? 0.0f : 1.0f;      // 0:先端(下) 1:しっぽ(上)
    float side = (corner & 1u) == 0 ? -1.0f : 1.0f;

    float B = gShape.x;
    float H = gShape.y;
    float speed = gShape.z;

    float rx = Rand(iid * 4u + 0u);
    float ry = Rand(iid * 4u + 1u);
    float rz = Rand(iid * 4u + 2u);
    float rv = Rand(iid * 4u + 3u);

    // 上から落ちる位置(0:地面〜1:上)。時間で下がり、地面に着いたら上へ戻る
    float age = frac(ry - gFocus.w * speed / H * (0.85f + 0.3f * rv));
    float y = age * H;

    // 風で流れる：上にいたときほど、横にずれている
    float fallTime = (1.0f - age) * H / speed;
    float2 baseXZ = float2(rx, rz) * B;
    // 世界に固定された格子で、注目点の近くに折り返す
    baseXZ -= B * floor((baseXZ - gFocus.xz) / B + 0.5f);
    float2 xz = baseXZ + gWind.xz * fallTime;

    float3 head = float3(xz.x, y, xz.y);
    float3 vdir = normalize(float3(gWind.x, -speed, gWind.z));
    float3 p = head - vdir * gShape.w * along;

    float3 toCam = normalize(gCamPos.xyz - head);
    float3 sdir = normalize(cross(vdir, toCam));
    float dist = length(gCamPos.xyz - head);
    float width = gWind.w * (1.0f + dist * 0.02f);
    p += sdir * side * width * 0.5f;

    VSOut o;
    o.pos = mul(float4(p, 1.0f), gViewProj);
    float groundFade = saturate((y - gRipple.x) * 2.0f);          // 地面に近いと薄れる
    o.data = float4(along, (0.5f + 0.5f * rv) * groundFade, 0.0f, 0.0f);
    o.kind = 0.0f;
    return o;
}

VSOut VS_Ripple(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    uint corner = kCorner[vid];
    float2 c = float2((corner & 1u) == 0 ? -1.0f : 1.0f, (corner >> 1) == 0 ? -1.0f : 1.0f);

    float B = gRipple.w;
    float rx = Rand(iid * 4u + 0u + 90001u);
    float rz = Rand(iid * 4u + 1u + 90001u);
    float rp = Rand(iid * 4u + 2u + 90001u);
    float rs = Rand(iid * 4u + 3u + 90001u);

    float2 center = float2(rx, rz) * B;
    center -= B * floor((center - gFocus.xz) / B + 0.5f);

    float age = frac(rp + gFocus.w / gRipple.z);
    float radius = gRipple.y * (0.6f + 0.4f * rs);

    float3 p = float3(center.x + c.x * radius, gRipple.x + 0.02f, center.y + c.y * radius);

    VSOut o;
    o.pos = mul(float4(p, 1.0f), gViewProj);
    o.data = float4(c.x, c.y, age, 0.6f + 0.4f * rs);
    o.kind = 1.0f;
    return o;
}

float4 PS_Rain(VSOut i) : SV_TARGET
{
    float a;
    if (i.kind < 0.5f)
    {
        // 雨粒：先端が明るく、しっぽへ薄れる
        a = (1.0f - i.data.x * 0.9f) * i.data.y;
    }
    else
    {
        // 波紋：広がるリングと、最初の小さなしぶき
        float r = length(i.data.xy);
        float age = i.data.z;
        if (r > 1.0f)
        {
            discard;
        }
        float ring = smoothstep(0.10f, 0.0f, abs(r - age));
        float splash = smoothstep(0.35f, 0.0f, r) * smoothstep(0.25f, 0.0f, age);
        a = (ring * (1.0f - age) * (1.0f - age) + splash * 0.8f) * i.data.w;
    }
    a *= gColor.a;
    return float4(gColor.rgb * a, a);
}

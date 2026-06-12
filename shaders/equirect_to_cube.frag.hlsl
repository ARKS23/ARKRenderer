struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct EquirectToCubeUniform {
    uint faceIndex;
    float outputResolution;
    float padding0;
    float padding1;
};

[[vk::binding(0, 0)]]
ConstantBuffer<EquirectToCubeUniform> g_Conversion;

[[vk::binding(1, 0)]]
Texture2D<float4> g_SourceEnvironment;

[[vk::binding(2, 0)]]
SamplerState g_SourceSampler;

static const float PI = 3.14159265359f;

float2 directionToEquirectUv(float3 direction) {
    const float3 d = normalize(direction);
    const float u = atan2(d.z, d.x) * (1.0f / (2.0f * PI)) + 0.5f;
    const float v = acos(clamp(d.y, -1.0f, 1.0f)) * (1.0f / PI);
    return float2(u, v);
}

float3 faceUvToDirection(uint faceIndex, float2 uv) {
    const float2 xy = uv * 2.0f - 1.0f;
    const float x = xy.x;
    const float y = xy.y;

    // Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
    switch (faceIndex) {
    case 0:
        return normalize(float3(1.0f, -y, -x));
    case 1:
        return normalize(float3(-1.0f, -y, x));
    case 2:
        return normalize(float3(x, 1.0f, y));
    case 3:
        return normalize(float3(x, -1.0f, -y));
    case 4:
        return normalize(float3(x, -y, 1.0f));
    default:
        return normalize(float3(-x, -y, -1.0f));
    }
}

float4 main(PSInput input) : SV_Target0 {
    const float2 uv = saturate(input.uv);
    const float3 direction = faceUvToDirection(g_Conversion.faceIndex, uv);
    const float3 hdrColor = g_SourceEnvironment.Sample(g_SourceSampler, directionToEquirectUv(direction)).rgb;
    return float4(hdrColor, 1.0f);
}

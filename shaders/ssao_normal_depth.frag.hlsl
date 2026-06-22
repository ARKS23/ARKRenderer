struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 viewNormal : NORMAL0;
    [[vk::location(1)]] float viewDepth : TEXCOORD0;
    [[vk::location(2)]] float2 uv0 : TEXCOORD1;
    [[vk::location(3)]] float2 uv1 : TEXCOORD2;
};

[[vk::binding(1, 0)]]
Texture2D<float4> g_BaseColorTexture;

[[vk::binding(2, 0)]]
SamplerState g_BaseColorSampler;

struct SsaoMaterialUniform {
    float4 baseColorFactor;
    float4 baseColorUvTransform0;
    float4 baseColorUvTransform1;
    float alphaCutoff;
    float alphaMode;
    float baseColorTexCoord;
    float padding;
};

[[vk::binding(3, 0)]]
ConstantBuffer<SsaoMaterialUniform> g_Material;

static const float AlphaModeMask = 1.0f;

float2 selectUv(float selector, float2 uv0, float2 uv1) {
    return selector >= 0.5f ? uv1 : uv0;
}

float2 transformUv(float2 uv, float2 offset, float2 scale, float rotation) {
    const float s = sin(rotation);
    const float c = cos(rotation);
    const float2 scaled = uv * scale;
    return float2(
        c * scaled.x - s * scaled.y,
        s * scaled.x + c * scaled.y
    ) + offset;
}

float4 main(PSInput input) : SV_Target0 {
    if (g_Material.alphaMode == AlphaModeMask) {
        const float2 baseColorUv = transformUv(
            selectUv(g_Material.baseColorTexCoord, input.uv0, input.uv1),
            g_Material.baseColorUvTransform0.xy,
            g_Material.baseColorUvTransform0.zw,
            g_Material.baseColorUvTransform1.x);
        const float alpha =
            g_BaseColorTexture.Sample(g_BaseColorSampler, baseColorUv).a *
            g_Material.baseColorFactor.a;
        if (alpha < g_Material.alphaCutoff) {
            discard;
        }
    }

    const float3 encodedNormal = normalize(input.viewNormal) * 0.5f + 0.5f;
    return float4(encodedNormal, input.viewDepth);
}

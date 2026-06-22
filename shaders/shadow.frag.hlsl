struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv0 : TEXCOORD0;
    [[vk::location(1)]] float2 uv1 : TEXCOORD1;
};

[[vk::binding(1, 0)]]
Texture2D<float4> g_BaseColorTexture;

[[vk::binding(2, 0)]]
SamplerState g_BaseColorSampler;

struct ShadowMaterialUniform {
    float4 baseColorFactor;
    float4 baseColorUvTransform0;
    float4 baseColorUvTransform1;
    float alphaCutoff;
    float alphaMode;
    float baseColorTexCoord;
    float padding;
};

[[vk::binding(3, 0)]]
ConstantBuffer<ShadowMaterialUniform> g_ShadowMaterial;

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

void main(PSInput input) {
    if (g_ShadowMaterial.alphaMode == AlphaModeMask) {
        const float2 baseColorUv = transformUv(
            selectUv(g_ShadowMaterial.baseColorTexCoord, input.uv0, input.uv1),
            g_ShadowMaterial.baseColorUvTransform0.xy,
            g_ShadowMaterial.baseColorUvTransform0.zw,
            g_ShadowMaterial.baseColorUvTransform1.x);
        const float alpha =
            g_BaseColorTexture.Sample(g_BaseColorSampler, baseColorUv).a *
            g_ShadowMaterial.baseColorFactor.a;
        if (alpha < g_ShadowMaterial.alphaCutoff) {
            discard;
        }
    }
}

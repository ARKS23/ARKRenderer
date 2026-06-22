struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float2 uv0 : TEXCOORD0;
    [[vk::location(2)]] float2 uv1 : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv0 : TEXCOORD0;
    [[vk::location(1)]] float2 uv1 : TEXCOORD1;
};

struct ShadowUniform {
    float4x4 lightViewProjection;
    float4x4 model;
};

[[vk::binding(0, 0)]]
ConstantBuffer<ShadowUniform> g_Shadow;

VSOutput main(VSInput input) {
    VSOutput output;
    const float4 worldPosition = mul(g_Shadow.model, float4(input.position, 1.0f));
    output.position = mul(g_Shadow.lightViewProjection, worldPosition);
    output.uv0 = input.uv0;
    output.uv1 = input.uv1;
    return output;
}

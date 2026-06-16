struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
};

struct ShadowUniform {
    float4x4 lightViewProjection;
    float4x4 model;
};

[[vk::binding(0, 0)]]
ConstantBuffer<ShadowUniform> g_Shadow;

float4 main(VSInput input) : SV_Position {
    const float4 worldPosition = mul(g_Shadow.model, float4(input.position, 1.0f));
    return mul(g_Shadow.lightViewProjection, worldPosition);
}

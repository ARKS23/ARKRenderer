struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv0 : TEXCOORD0;
    [[vk::location(3)]] float2 uv1 : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 viewNormal : NORMAL0;
    [[vk::location(1)]] float viewDepth : TEXCOORD0;
    [[vk::location(2)]] float2 uv0 : TEXCOORD1;
    [[vk::location(3)]] float2 uv1 : TEXCOORD2;
};

struct SsaoGeometryUniform {
    float4x4 view;
    float4x4 projection;
    float4x4 model;
    float4x4 normalMatrix;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SsaoGeometryUniform> g_Geometry;

VSOutput main(VSInput input) {
    VSOutput output;
    const float4 worldPosition = mul(g_Geometry.model, float4(input.position, 1.0f));
    const float4 viewPosition = mul(g_Geometry.view, worldPosition);
    output.position = mul(g_Geometry.projection, viewPosition);

    const float3 worldNormal = normalize(mul((float3x3)g_Geometry.normalMatrix, input.normal));
    output.viewNormal = normalize(mul((float3x3)g_Geometry.view, worldNormal));
    output.viewDepth = max(-viewPosition.z, 0.0f);
    output.uv0 = input.uv0;
    output.uv1 = input.uv1;
    return output;
}

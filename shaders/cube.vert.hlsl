struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR0;
};

struct CameraUniform {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> g_Camera;

VSOutput main(VSInput input) {
    VSOutput output;
    const float4 worldPosition = mul(g_Camera.model, float4(input.position, 1.0f));
    const float4 viewPosition = mul(g_Camera.view, worldPosition);
    output.position = mul(g_Camera.projection, viewPosition);
    output.color = input.color;
    return output;
}

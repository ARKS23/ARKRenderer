struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv0 : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPosition : POSITION0;
    [[vk::location(1)]] float3 worldNormal : NORMAL0;
    [[vk::location(2)]] float4 worldTangent : TANGENT0;
    [[vk::location(3)]] float2 uv0 : TEXCOORD0;
};

struct CameraUniform {
    float4x4 view;
    float4x4 projection;
};

struct ObjectUniform {
    float4x4 model;
    float4x4 normalMatrix;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> g_Camera;

[[vk::binding(3, 0)]]
ConstantBuffer<ObjectUniform> g_Object;

VSOutput main(VSInput input) {
    VSOutput output;
    const float4 worldPosition = mul(g_Object.model, float4(input.position, 1.0f));
    const float4 viewPosition = mul(g_Camera.view, worldPosition);
    output.position = mul(g_Camera.projection, viewPosition);
    output.worldPosition = worldPosition.xyz;
    const float3 worldNormal = normalize(mul((float3x3)g_Object.normalMatrix, input.normal));
    float3 worldTangent = normalize(mul((float3x3)g_Object.model, input.tangent.xyz));
    worldTangent = normalize(worldTangent - worldNormal * dot(worldNormal, worldTangent));
    output.worldNormal = worldNormal;
    output.worldTangent = float4(worldTangent, input.tangent.w);
    output.uv0 = input.uv0;
    return output;
}

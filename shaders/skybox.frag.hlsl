struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct SkyboxUniform {
    float4x4 inverseProjection;
    float4x4 inverseViewRotation;
    float4 settings;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SkyboxUniform> g_Skybox;

[[vk::binding(1, 0)]]
TextureCube<float4> g_SkyboxCube;

[[vk::binding(2, 0)]]
SamplerState g_SkyboxSampler;

float3 reconstructWorldDirection(float2 uv) {
    const float2 ndc = uv * 2.0f - 1.0f;
    const float4 clipPosition = float4(ndc, 1.0f, 1.0f);
    const float4 viewPosition = mul(g_Skybox.inverseProjection, clipPosition);
    const float3 viewDirection = normalize(viewPosition.xyz / viewPosition.w);
    return normalize(mul((float3x3)g_Skybox.inverseViewRotation, viewDirection));
}

float4 main(PSInput input) : SV_Target0 {
    const float3 worldDirection = reconstructWorldDirection(input.uv);
    const float3 color = g_SkyboxCube.Sample(g_SkyboxSampler, worldDirection).rgb * g_Skybox.settings.x;
    return float4(color, 1.0f);
}

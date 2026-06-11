struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Texture2D<float4> g_SceneColor;

[[vk::binding(1, 0)]]
SamplerState g_SceneSampler;

struct ToneMappingUniform {
    float exposure;
    float inverseOutputGamma;
    float padding0;
    float padding1;
};

[[vk::binding(2, 0)]]
ConstantBuffer<ToneMappingUniform> g_ToneMapping;

float3 applyToneMapping(float3 color) {
    color *= g_ToneMapping.exposure;
    return color / (color + 1.0f);
}

float3 linearToOutput(float3 color) {
    return pow(saturate(color), g_ToneMapping.inverseOutputGamma);
}

float4 main(PSInput input) : SV_Target0 {
    const float3 hdrColor = g_SceneColor.Sample(g_SceneSampler, input.uv).rgb;
    const float3 toneMapped = applyToneMapping(hdrColor);
    return float4(linearToOutput(toneMapped), 1.0f);
}

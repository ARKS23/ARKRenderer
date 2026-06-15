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
    float operatorType;
    float padding1;
};

[[vk::binding(2, 0)]]
ConstantBuffer<ToneMappingUniform> g_ToneMapping;

float3 toneMapLinear(float3 color) {
    return color;
}

float3 toneMapReinhard(float3 color) {
    return color / (color + 1.0f);
}

float3 toneMapACES(float3 color) {
    // ACES fitted approximation for opt-in sandbox / validation previews.
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 applyToneMapping(float3 color) {
    color *= g_ToneMapping.exposure;

    const uint operatorType = (uint)(g_ToneMapping.operatorType + 0.5f);
    if (operatorType == 1u) {
        return toneMapLinear(color);
    }

    if (operatorType == 2u) {
        return toneMapACES(color);
    }

    return toneMapReinhard(color);
}

float3 linearToOutput(float3 color) {
    return pow(saturate(color), g_ToneMapping.inverseOutputGamma);
}

float4 main(PSInput input) : SV_Target0 {
    const float3 hdrColor = g_SceneColor.Sample(g_SceneSampler, input.uv).rgb;
    const float3 toneMapped = applyToneMapping(hdrColor);
    return float4(linearToOutput(toneMapped), 1.0f);
}

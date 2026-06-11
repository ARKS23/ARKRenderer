struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Texture2D<float4> g_SceneColor;

[[vk::binding(1, 0)]]
SamplerState g_SceneSampler;

static const float Exposure = 1.0f;

float3 applyToneMapping(float3 color) {
    color *= Exposure;
    return color / (color + 1.0f);
}

float3 linearToSrgb(float3 color) {
    return pow(saturate(color), 1.0f / 2.2f);
}

float4 main(PSInput input) : SV_Target0 {
    const float3 hdrColor = g_SceneColor.Sample(g_SceneSampler, input.uv).rgb;
    const float3 toneMapped = applyToneMapping(hdrColor);
    return float4(linearToSrgb(toneMapped), 1.0f);
}

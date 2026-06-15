struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

// Bloom runs on linear HDR scene color before tone mapping.
[[vk::binding(0, 0)]]
Texture2D<float4> g_Source0;

[[vk::binding(1, 0)]]
Texture2D<float4> g_Source1;

[[vk::binding(2, 0)]]
SamplerState g_BloomSampler;

struct BloomUniform {
    uint mode;
    float intensity;
    float scatter;
    float threshold;
    float softKnee;
    float texelSizeX;
    float texelSizeY;
    float padding0;
};

[[vk::binding(3, 0)]]
ConstantBuffer<BloomUniform> g_Bloom;

static const uint BloomModePrefilter = 0u;
static const uint BloomModeDownsample = 1u;
static const uint BloomModeUpsample = 2u;
static const uint BloomModeComposite = 3u;

float luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float softThresholdResponse(float brightness) {
    const float threshold = max(g_Bloom.threshold, 0.0f);
    const float knee = max(threshold * g_Bloom.softKnee, 0.0001f);
    float soft = brightness - threshold + knee;
    soft = saturate(soft / (2.0f * knee));
    soft = soft * soft * knee;

    const float contribution = max(brightness - threshold, 0.0f) + soft;
    return brightness > 0.0f ? saturate(contribution / max(brightness, 0.0001f)) : 0.0f;
}

float3 sampleBloomFilter(Texture2D<float4> source, float2 uv) {
    const float2 texel = float2(g_Bloom.texelSizeX, g_Bloom.texelSizeY);
    float3 color = source.Sample(g_BloomSampler, uv).rgb * 0.5f;
    color += source.Sample(g_BloomSampler, uv + texel * float2(-1.0f, 0.0f)).rgb * 0.125f;
    color += source.Sample(g_BloomSampler, uv + texel * float2(1.0f, 0.0f)).rgb * 0.125f;
    color += source.Sample(g_BloomSampler, uv + texel * float2(0.0f, -1.0f)).rgb * 0.125f;
    color += source.Sample(g_BloomSampler, uv + texel * float2(0.0f, 1.0f)).rgb * 0.125f;
    return color;
}

float3 prefilterBloom(float2 uv) {
    const float3 hdrColor = sampleBloomFilter(g_Source0, uv);
    return hdrColor * softThresholdResponse(luminance(hdrColor));
}

float3 downsampleBloom(float2 uv) {
    return sampleBloomFilter(g_Source0, uv);
}

float3 upsampleBloom(float2 uv) {
    const float3 currentLevel = g_Source1.Sample(g_BloomSampler, uv).rgb;
    const float3 expandedLevel = sampleBloomFilter(g_Source0, uv);
    return currentLevel + expandedLevel * g_Bloom.scatter;
}

float3 compositeBloom(float2 uv) {
    const float3 sceneColor = g_Source0.Sample(g_BloomSampler, uv).rgb;
    const float3 bloomColor = sampleBloomFilter(g_Source1, uv);
    return sceneColor + bloomColor * g_Bloom.intensity;
}

float4 main(PSInput input) : SV_Target0 {
    if (g_Bloom.mode == BloomModePrefilter) {
        return float4(prefilterBloom(input.uv), 1.0f);
    }

    if (g_Bloom.mode == BloomModeDownsample) {
        return float4(downsampleBloom(input.uv), 1.0f);
    }

    if (g_Bloom.mode == BloomModeUpsample) {
        return float4(upsampleBloom(input.uv), 1.0f);
    }

    return float4(compositeBloom(input.uv), 1.0f);
}

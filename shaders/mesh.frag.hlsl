struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL0;
    [[vk::location(1)]] float2 uv0 : TEXCOORD0;
};

[[vk::binding(1, 0)]]
Texture2D<float4> g_BaseColorTexture;

[[vk::binding(2, 0)]]
SamplerState g_BaseColorSampler;

[[vk::binding(5, 0)]]
Texture2D<float4> g_NormalTexture;

[[vk::binding(6, 0)]]
SamplerState g_NormalSampler;

[[vk::binding(7, 0)]]
Texture2D<float4> g_MetallicRoughnessTexture;

[[vk::binding(8, 0)]]
SamplerState g_MetallicRoughnessSampler;

[[vk::binding(9, 0)]]
Texture2D<float4> g_OcclusionTexture;

[[vk::binding(10, 0)]]
SamplerState g_OcclusionSampler;

[[vk::binding(11, 0)]]
Texture2D<float4> g_EmissiveTexture;

[[vk::binding(12, 0)]]
SamplerState g_EmissiveSampler;

struct MaterialUniform {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
};

[[vk::binding(4, 0)]]
ConstantBuffer<MaterialUniform> g_Material;

float4 main(PSInput input) : SV_Target0 {
    const float4 baseColor = g_BaseColorTexture.Sample(g_BaseColorSampler, input.uv0) * g_Material.baseColorFactor;
    const float3 normalSample = g_NormalTexture.Sample(g_NormalSampler, input.uv0).xyz * 2.0f - 1.0f;
    const float4 metallicRoughnessSample =
        g_MetallicRoughnessTexture.Sample(g_MetallicRoughnessSampler, input.uv0);
    const float occlusionSample = g_OcclusionTexture.Sample(g_OcclusionSampler, input.uv0).r;
    const float3 emissive = g_EmissiveTexture.Sample(g_EmissiveSampler, input.uv0).rgb *
                            g_Material.emissiveFactor.rgb;

    const float occlusion = lerp(1.0f, occlusionSample, saturate(g_Material.occlusionStrength));
    const float2 pbrData = float2(metallicRoughnessSample.b * g_Material.metallicFactor,
                                  metallicRoughnessSample.g * g_Material.roughnessFactor);
    const float3 scaledNormal = normalize(float3(normalSample.xy * g_Material.normalScale, normalSample.z));

    // Phase 0.16 只打通 texture slot 数据链路；MR/normal 的真实光照解释留给后续 PBR。
    const bool invalidDebugSample = any(abs(scaledNormal) > float3(8.0f, 8.0f, 8.0f)) ||
                                    any(pbrData < float2(-1.0f, -1.0f));
    if (invalidDebugSample) {
        discard;
    }

    return float4(baseColor.rgb * occlusion + emissive, baseColor.a);
}

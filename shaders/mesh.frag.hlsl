struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPosition : POSITION0;
    [[vk::location(1)]] float3 worldNormal : NORMAL0;
    [[vk::location(2)]] float4 worldTangent : TANGENT0;
    [[vk::location(3)]] float2 uv0 : TEXCOORD0;
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

struct LightingUniform {
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 cameraPosition;
};

[[vk::binding(13, 0)]]
ConstantBuffer<LightingUniform> g_Lighting;

struct PbrInputs {
    float4 baseColor;
    float3 worldNormal;
    float metallic;
    float roughness;
    float occlusion;
    float3 emissive;
};

float3 decodeNormal(float3 encodedNormal) {
    return encodedNormal * 2.0f - 1.0f;
}

float3 buildWorldNormal(float3 tangentSpaceNormal, float3 inputNormal, float4 inputTangent) {
    const float3 n = normalize(inputNormal);
    const float3 t = normalize(inputTangent.xyz);
    const float3 b = normalize(cross(n, t) * inputTangent.w);
    return normalize(mul(tangentSpaceNormal, float3x3(t, b, n)));
}

PbrInputs readPbrInputs(PSInput input) {
    PbrInputs inputs;
    inputs.baseColor = g_BaseColorTexture.Sample(g_BaseColorSampler, input.uv0) * g_Material.baseColorFactor;

    const float3 normalSample = decodeNormal(g_NormalTexture.Sample(g_NormalSampler, input.uv0).xyz);
    const float3 tangentSpaceNormal =
        normalize(float3(normalSample.xy * g_Material.normalScale, normalSample.z));
    inputs.worldNormal = buildWorldNormal(tangentSpaceNormal, input.worldNormal, input.worldTangent);

    const float4 metallicRoughnessSample =
        g_MetallicRoughnessTexture.Sample(g_MetallicRoughnessSampler, input.uv0);
    inputs.metallic = saturate(metallicRoughnessSample.b * g_Material.metallicFactor);
    inputs.roughness = clamp(metallicRoughnessSample.g * g_Material.roughnessFactor, 0.04f, 1.0f);

    const float occlusionSample = g_OcclusionTexture.Sample(g_OcclusionSampler, input.uv0).r;
    inputs.occlusion = lerp(1.0f, occlusionSample, saturate(g_Material.occlusionStrength));
    inputs.emissive = g_EmissiveTexture.Sample(g_EmissiveSampler, input.uv0).rgb *
                      g_Material.emissiveFactor.rgb;
    return inputs;
}

float3 evaluateDirectLighting(PbrInputs inputs, float3 worldPosition) {
    const float3 n = normalize(inputs.worldNormal);
    const float3 l = normalize(-g_Lighting.lightDirection.xyz);
    const float3 v = normalize(g_Lighting.cameraPosition.xyz - worldPosition);
    const float3 h = normalize(l + v);
    const float nDotL = saturate(dot(n, l));
    const float nDotH = saturate(dot(n, h));

    const float3 diffuseColor = inputs.baseColor.rgb * (1.0f - inputs.metallic);
    const float3 specularColor = lerp(float3(0.04f, 0.04f, 0.04f), inputs.baseColor.rgb, inputs.metallic);
    const float specularPower = lerp(96.0f, 8.0f, inputs.roughness);
    const float specularStrength = pow(nDotH, specularPower) * (1.0f - inputs.roughness * 0.5f);

    // Phase 0.17 是 direct-light-only 解释：使用 glTF PBR 输入，但不声明完整 BRDF/IBL。
    const float3 ambient = g_Lighting.ambientColor.rgb * inputs.baseColor.rgb;
    const float3 direct = g_Lighting.lightColor.rgb * nDotL *
                          (diffuseColor + specularColor * specularStrength);
    return (ambient + direct) * inputs.occlusion + inputs.emissive;
}

float4 main(PSInput input) : SV_Target0 {
    const PbrInputs inputs = readPbrInputs(input);
    return float4(evaluateDirectLighting(inputs, input.worldPosition), inputs.baseColor.a);
}

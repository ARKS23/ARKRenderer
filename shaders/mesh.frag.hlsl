struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPosition : POSITION0;
    [[vk::location(1)]] float3 worldNormal : NORMAL0;
    [[vk::location(2)]] float4 worldTangent : TANGENT0;
    [[vk::location(3)]] float2 uv0 : TEXCOORD0;
    [[vk::location(4)]] float2 uv1 : TEXCOORD1;
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

[[vk::binding(14, 0)]]
Texture2D<float4> g_EnvironmentTexture;

[[vk::binding(15, 0)]]
SamplerState g_EnvironmentSampler;

[[vk::binding(16, 0)]]
TextureCube<float4> g_IrradianceCube;

[[vk::binding(17, 0)]]
SamplerState g_IrradianceSampler;

[[vk::binding(18, 0)]]
TextureCube<float4> g_PrefilteredSpecularCube;

[[vk::binding(19, 0)]]
SamplerState g_PrefilteredSpecularSampler;

[[vk::binding(20, 0)]]
Texture2D<float4> g_BrdfLut;

[[vk::binding(21, 0)]]
SamplerState g_BrdfLutSampler;

struct MaterialUniform {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    float alphaMode;
    float baseColorTexCoord;
    float normalTexCoord;
    float metallicRoughnessTexCoord;
    float occlusionTexCoord;
    float emissiveTexCoord;
    float padding;
    float4 baseColorUvTransform0;
    float4 baseColorUvTransform1;
    float4 normalUvTransform0;
    float4 normalUvTransform1;
    float4 metallicRoughnessUvTransform0;
    float4 metallicRoughnessUvTransform1;
    float4 occlusionUvTransform0;
    float4 occlusionUvTransform1;
    float4 emissiveUvTransform0;
    float4 emissiveUvTransform1;
};

[[vk::binding(4, 0)]]
ConstantBuffer<MaterialUniform> g_Material;

static const float AlphaModeMask = 1.0f;
static const float AlphaModeBlend = 2.0f;
static const float PI = 3.14159265359f;

struct LightingUniform {
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 cameraPosition;
    float4 environment;
    float4 environmentSpecular;
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

float2 selectUv(float selector, float2 uv0, float2 uv1) {
    return selector >= 0.5f ? uv1 : uv0;
}

float2 transformUv(float2 uv, float2 offset, float2 scale, float rotation) {
    const float s = sin(rotation);
    const float c = cos(rotation);
    const float2 scaled = uv * scale;
    return float2(
        c * scaled.x - s * scaled.y,
        s * scaled.x + c * scaled.y
    ) + offset;
}

PbrInputs readPbrInputs(PSInput input) {
    PbrInputs inputs;
    const float2 baseColorUv = transformUv(selectUv(g_Material.baseColorTexCoord, input.uv0, input.uv1),
                                           g_Material.baseColorUvTransform0.xy,
                                           g_Material.baseColorUvTransform0.zw,
                                           g_Material.baseColorUvTransform1.x);
    const float2 normalUv = transformUv(selectUv(g_Material.normalTexCoord, input.uv0, input.uv1),
                                        g_Material.normalUvTransform0.xy,
                                        g_Material.normalUvTransform0.zw,
                                        g_Material.normalUvTransform1.x);
    const float2 metallicRoughnessUv =
        transformUv(selectUv(g_Material.metallicRoughnessTexCoord, input.uv0, input.uv1),
                    g_Material.metallicRoughnessUvTransform0.xy,
                    g_Material.metallicRoughnessUvTransform0.zw,
                    g_Material.metallicRoughnessUvTransform1.x);
    const float2 occlusionUv = transformUv(selectUv(g_Material.occlusionTexCoord, input.uv0, input.uv1),
                                           g_Material.occlusionUvTransform0.xy,
                                           g_Material.occlusionUvTransform0.zw,
                                           g_Material.occlusionUvTransform1.x);
    const float2 emissiveUv = transformUv(selectUv(g_Material.emissiveTexCoord, input.uv0, input.uv1),
                                          g_Material.emissiveUvTransform0.xy,
                                          g_Material.emissiveUvTransform0.zw,
                                          g_Material.emissiveUvTransform1.x);

    inputs.baseColor = g_BaseColorTexture.Sample(g_BaseColorSampler, baseColorUv) * g_Material.baseColorFactor;

    const float3 normalSample = decodeNormal(g_NormalTexture.Sample(g_NormalSampler, normalUv).xyz);
    const float3 tangentSpaceNormal =
        normalize(float3(normalSample.xy * g_Material.normalScale, normalSample.z));
    inputs.worldNormal = buildWorldNormal(tangentSpaceNormal, input.worldNormal, input.worldTangent);

    const float4 metallicRoughnessSample =
        g_MetallicRoughnessTexture.Sample(g_MetallicRoughnessSampler, metallicRoughnessUv);
    inputs.metallic = saturate(metallicRoughnessSample.b * g_Material.metallicFactor);
    inputs.roughness = clamp(metallicRoughnessSample.g * g_Material.roughnessFactor, 0.04f, 1.0f);

    const float occlusionSample = g_OcclusionTexture.Sample(g_OcclusionSampler, occlusionUv).r;
    inputs.occlusion = lerp(1.0f, occlusionSample, saturate(g_Material.occlusionStrength));
    inputs.emissive = g_EmissiveTexture.Sample(g_EmissiveSampler, emissiveUv).rgb *
                      g_Material.emissiveFactor.rgb;
    return inputs;
}

float distributionGGX(float nDotH, float roughness) {
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominatorTerm = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(PI * denominatorTerm * denominatorTerm, 0.0001f);
}

float geometrySchlickGGX(float nDotV, float roughness) {
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    return nDotV / max(nDotV * (1.0f - k) + k, 0.0001f);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
    const float ggxV = geometrySchlickGGX(nDotV, roughness);
    const float ggxL = geometrySchlickGGX(nDotL, roughness);
    return ggxV * ggxL;
}

float3 fresnelSchlick(float cosTheta, float3 f0) {
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 f0, float roughness) {
    const float oneMinusRoughness = 1.0f - roughness;
    const float3 grazing = max(float3(oneMinusRoughness, oneMinusRoughness, oneMinusRoughness), f0);
    return f0 + (grazing - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float2 directionToEquirectUv(float3 direction) {
    const float3 d = normalize(direction);
    const float u = atan2(d.z, d.x) * (1.0f / (2.0f * PI)) + 0.5f;
    const float v = acos(clamp(d.y, -1.0f, 1.0f)) * (1.0f / PI);
    return float2(u, v);
}

float3 sampleEnvironment(float3 direction) {
    return g_EnvironmentTexture.Sample(g_EnvironmentSampler, directionToEquirectUv(direction)).rgb;
}

float3 sampleIrradiance(float3 normal) {
    return g_IrradianceCube.Sample(g_IrradianceSampler, normalize(normal)).rgb;
}

float3 sampleDiffuseIbl(float3 normal, float3 albedo) {
    if (g_Lighting.environment.z > 0.5f) {
        const float3 irradiance = sampleIrradiance(normal) * g_Lighting.environment.x;
        return irradiance * albedo;
    }

    if (g_Lighting.environment.y > 0.5f) {
        const float3 environmentRadiance = sampleEnvironment(normal) * g_Lighting.environment.x;
        return environmentRadiance * albedo;
    }

    return g_Lighting.ambientColor.rgb * albedo;
}

float3 samplePrefilteredSpecular(float3 reflectionDirection, float roughness) {
    const float maxMip = max(g_Lighting.environmentSpecular.x, 0.0f);
    const float mipLevel = saturate(roughness) * maxMip;
    return g_PrefilteredSpecularCube.SampleLevel(
        g_PrefilteredSpecularSampler,
        normalize(reflectionDirection),
        mipLevel
    ).rgb;
}

float2 sampleBrdfLut(float nDotV, float roughness) {
    return g_BrdfLut.Sample(g_BrdfLutSampler, float2(saturate(nDotV), saturate(roughness))).rg;
}

float3 evaluateIndirectLighting(float3 n,
                                float3 v,
                                float3 albedo,
                                float metallic,
                                float roughness,
                                float3 f0) {
    const float nDotV = max(saturate(dot(n, v)), 0.0001f);
    const float3 fresnel = fresnelSchlickRoughness(nDotV, f0, roughness);
    const float3 kd = (1.0f - fresnel) * (1.0f - metallic);

    const float3 diffuseIbl = sampleDiffuseIbl(n, albedo);
    float3 specularIbl = float3(0.0f, 0.0f, 0.0f);
    if (g_Lighting.environment.w > 0.5f) {
        const float3 reflectionDirection = reflect(-v, n);
        const float3 prefilteredSpecular =
            samplePrefilteredSpecular(reflectionDirection, roughness) * g_Lighting.environment.x;
        const float2 brdf = sampleBrdfLut(nDotV, roughness);
        specularIbl = prefilteredSpecular * (f0 * brdf.x + brdf.y);
    }

    return diffuseIbl * kd + specularIbl;
}

float3 evaluateDirectLighting(PbrInputs inputs, float3 worldPosition) {
    const float3 n = normalize(inputs.worldNormal);
    const float3 l = normalize(-g_Lighting.lightDirection.xyz);
    const float3 v = normalize(g_Lighting.cameraPosition.xyz - worldPosition);
    const float3 halfVectorSource = l + v;
    const float3 h = dot(halfVectorSource, halfVectorSource) > 0.0001f ? normalize(halfVectorSource) : n;
    const float nDotL = saturate(dot(n, l));
    const float nDotV = max(saturate(dot(n, v)), 0.0001f);
    const float nDotH = saturate(dot(n, h));
    const float vDotH = saturate(dot(v, h));

    const float3 albedo = inputs.baseColor.rgb;
    const float metallic = saturate(inputs.metallic);
    const float roughness = clamp(inputs.roughness, 0.04f, 1.0f);

    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    const float3 fresnel = fresnelSchlick(vDotH, f0);
    const float normalDistribution = distributionGGX(nDotH, roughness);
    const float geometry = geometrySmith(nDotV, nDotL, roughness);
    const float specularDenominator = max(4.0f * nDotV * nDotL, 0.001f);
    const float3 specular = (normalDistribution * geometry * fresnel) / specularDenominator;

    const float3 kd = (1.0f - fresnel) * (1.0f - metallic);
    const float3 diffuse = kd * albedo / PI;

    const float3 indirect = evaluateIndirectLighting(n, v, albedo, metallic, roughness, f0);
    const float3 direct = g_Lighting.lightColor.rgb * nDotL * (diffuse + specular);
    return (indirect + direct) * inputs.occlusion + inputs.emissive;
}

float4 main(PSInput input) : SV_Target0 {
    const PbrInputs inputs = readPbrInputs(input);
    if (g_Material.alphaMode == AlphaModeMask && inputs.baseColor.a < g_Material.alphaCutoff) {
        discard;
    }

    const float outputAlpha = g_Material.alphaMode == AlphaModeBlend ? inputs.baseColor.a : 1.0f;
    return float4(evaluateDirectLighting(inputs, input.worldPosition), outputAlpha);
}

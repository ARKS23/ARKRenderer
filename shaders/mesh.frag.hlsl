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

float4 main(PSInput input) : SV_Target0 {
    const float4 baseColor = g_BaseColorTexture.Sample(g_BaseColorSampler, input.uv0) * g_Material.baseColorFactor;
    const float3 normalSample = g_NormalTexture.Sample(g_NormalSampler, input.uv0).xyz * 2.0f - 1.0f;
    const float4 metallicRoughnessSample =
        g_MetallicRoughnessTexture.Sample(g_MetallicRoughnessSampler, input.uv0);
    const float occlusionSample = g_OcclusionTexture.Sample(g_OcclusionSampler, input.uv0).r;
    const float3 emissive = g_EmissiveTexture.Sample(g_EmissiveSampler, input.uv0).rgb *
                            g_Material.emissiveFactor.rgb;

    const float occlusion = lerp(1.0f, occlusionSample, saturate(g_Material.occlusionStrength));
    const float metallic = saturate(metallicRoughnessSample.b * g_Material.metallicFactor);
    const float roughness = saturate(metallicRoughnessSample.g * g_Material.roughnessFactor);
    const float3 tangentSpaceNormal =
        normalize(float3(normalSample.xy * g_Material.normalScale, normalSample.z));

    const float3 n = normalize(input.worldNormal);
    const float3 t = normalize(input.worldTangent.xyz);
    const float3 b = normalize(cross(n, t) * input.worldTangent.w);
    const float3 worldNormal = normalize(mul(tangentSpaceNormal, float3x3(t, b, n)));
    const float3 lightDirection = normalize(-g_Lighting.lightDirection.xyz);
    const float3 viewDirection = normalize(g_Lighting.cameraPosition.xyz - input.worldPosition);
    const float3 halfVector = normalize(lightDirection + viewDirection);
    const float diffuse = saturate(dot(worldNormal, lightDirection));
    const float specularPower = lerp(64.0f, 8.0f, roughness);
    const float specular = pow(saturate(dot(worldNormal, halfVector)), specularPower) *
                           lerp(0.04f, 1.0f, metallic);
    const float3 lighting = g_Lighting.ambientColor.rgb + g_Lighting.lightColor.rgb * (diffuse + specular);

    return float4(baseColor.rgb * lighting * occlusion + emissive, baseColor.a);
}

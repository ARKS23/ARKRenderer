struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct SpecularPrefilterUniform {
    uint faceIndex;
    uint mipLevel;
    uint sampleCount;
    float roughness;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SpecularPrefilterUniform> g_Prefilter;

[[vk::binding(1, 0)]]
TextureCube<float4> g_SourceEnvironmentCube;

[[vk::binding(2, 0)]]
SamplerState g_SourceSampler;

static const float PI = 3.14159265359f;

float3 faceUvToDirection(uint faceIndex, float2 uv) {
    const float2 xy = uv * 2.0f - 1.0f;
    const float x = xy.x;
    const float y = xy.y;

    // Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
    switch (faceIndex) {
    case 0:
        return normalize(float3(1.0f, -y, -x));
    case 1:
        return normalize(float3(-1.0f, -y, x));
    case 2:
        return normalize(float3(x, 1.0f, y));
    case 3:
        return normalize(float3(x, -1.0f, -y));
    case 4:
        return normalize(float3(x, -y, 1.0f));
    default:
        return normalize(float3(-x, -y, -1.0f));
    }
}

float radicalInverseVdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 Hammersley(uint index, uint count) {
    return float2(float(index) / max(float(count), 1.0f), radicalInverseVdc(index));
}

void buildBasis(float3 normal, out float3 tangent, out float3 bitangent) {
    const float3 helper = abs(normal.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
    tangent = normalize(cross(helper, normal));
    bitangent = normalize(cross(normal, tangent));
}

float3 ImportanceSampleGGX(float2 xi, float roughness, float3 normal) {
    const float alpha = max(roughness * roughness, 0.001f);
    const float alpha2 = alpha * alpha;
    const float phi = 2.0f * PI * xi.x;
    const float cosTheta = sqrt((1.0f - xi.y) / max(1.0f + (alpha2 - 1.0f) * xi.y, 0.0001f));
    const float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));

    const float3 halfVectorTangent = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 tangent;
    float3 bitangent;
    buildBasis(normal, tangent, bitangent);
    return normalize(tangent * halfVectorTangent.x + bitangent * halfVectorTangent.y + normal * halfVectorTangent.z);
}

float4 main(PSInput input) : SV_Target0 {
    const float3 normal = faceUvToDirection(g_Prefilter.faceIndex, saturate(input.uv));
    const float3 viewDirection = normal;
    const float roughness = saturate(g_Prefilter.roughness);
    const uint sampleCount = clamp(g_Prefilter.sampleCount, 1u, 1024u);

    float3 prefilteredColor = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
        const float2 xi = Hammersley(sampleIndex, sampleCount);
        const float3 halfVector = ImportanceSampleGGX(xi, roughness, normal);
        const float3 lightDirection = normalize(2.0f * dot(viewDirection, halfVector) * halfVector - viewDirection);
        const float nDotL = saturate(dot(normal, lightDirection));

        if (nDotL > 0.0f) {
            prefilteredColor += g_SourceEnvironmentCube.SampleLevel(g_SourceSampler, lightDirection, 0.0f).rgb * nDotL;
            totalWeight += nDotL;
        }
    }

    prefilteredColor = totalWeight > 0.0f ? prefilteredColor / totalWeight : prefilteredColor;
    return float4(prefilteredColor, 1.0f);
}

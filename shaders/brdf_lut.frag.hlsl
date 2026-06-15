struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct BrdfLutUniform {
    uint sampleCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<BrdfLutUniform> g_BrdfLut;

static const float PI = 3.14159265359f;

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

float3 ImportanceSampleGGX(float2 xi, float roughness, float3 normal) {
    const float alpha = max(roughness * roughness, 0.001f);
    const float alpha2 = alpha * alpha;
    const float phi = 2.0f * PI * xi.x;
    const float cosTheta = sqrt((1.0f - xi.y) / max(1.0f + (alpha2 - 1.0f) * xi.y, 0.0001f));
    const float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));

    const float3 halfway = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    const float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(tangent * halfway.x + bitangent * halfway.y + normal * halfway.z);
}

float geometrySchlickGGX(float nDotV, float roughness) {
    const float k = (roughness * roughness) * 0.5f;
    return nDotV / max(nDotV * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

float2 IntegrateBRDF(float nDotV, float roughness, uint sampleCount) {
    const float3 normal = float3(0.0f, 0.0f, 1.0f);
    const float3 viewDirection = float3(sqrt(max(1.0f - nDotV * nDotV, 0.0f)), 0.0f, nDotV);

    float scale = 0.0f;
    float bias = 0.0f;

    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
        const float2 xi = Hammersley(sampleIndex, sampleCount);
        const float3 halfVector = ImportanceSampleGGX(xi, roughness, normal);
        const float3 lightDirection = normalize(2.0f * dot(viewDirection, halfVector) * halfVector - viewDirection);

        const float nDotL = saturate(lightDirection.z);
        const float nDotH = saturate(halfVector.z);
        const float vDotH = saturate(dot(viewDirection, halfVector));

        if (nDotL > 0.0f) {
            const float geometry = GeometrySmith(nDotV, nDotL, roughness);
            const float geometryVisibility = geometry * vDotH / max(nDotH * nDotV, 0.0001f);
            const float fresnel = pow(1.0f - vDotH, 5.0f);

            scale += (1.0f - fresnel) * geometryVisibility;
            bias += fresnel * geometryVisibility;
        }
    }

    return float2(scale, bias) / max(float(sampleCount), 1.0f);
}

float4 main(PSInput input) : SV_Target0 {
    const float nDotV = clamp(input.uv.x, 0.001f, 1.0f);
    const float roughness = saturate(input.uv.y);
    const uint sampleCount = clamp(g_BrdfLut.sampleCount, 1u, 4096u);
    const float2 brdf = IntegrateBRDF(nDotV, roughness, sampleCount);
    return float4(brdf.x, brdf.y, 0.0f, 1.0f);
}

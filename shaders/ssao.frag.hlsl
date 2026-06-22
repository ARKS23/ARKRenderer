struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Texture2D<float4> g_Source0;

[[vk::binding(1, 0)]]
Texture2D<float4> g_Source1;

[[vk::binding(2, 0)]]
SamplerState g_SsaoSampler;

struct SsaoFullscreenUniform {
    float4x4 projection;
    float4x4 inverseProjection;
    // x: radius, y: intensity, z: bias, w: power.
    float4 parameters0;
    // x: sampleCount, y: blurRadius, z: debugMode, w: fullscreen mode.
    float4 parameters1;
    // xy: source texel size, zw: scene texel size.
    float4 texelSize;
};

[[vk::binding(3, 0)]]
ConstantBuffer<SsaoFullscreenUniform> g_Ssao;

static const uint SsaoModeEvaluate = 0;
static const uint SsaoModeBlur = 1;
static const uint SsaoModeComposite = 2;
static const uint SsaoDebugNone = 0;
static const uint SsaoDebugOcclusion = 1;
static const uint SsaoDebugNormalDepth = 2;
static const float PI = 3.14159265359f;
static const uint MaxSsaoSamples = 64;

float hash21(float2 value) {
    return frac(sin(dot(value, float2(127.1f, 311.7f))) * 43758.5453123f);
}

float3 decodeViewNormal(float3 encodedNormal) {
    return normalize(encodedNormal * 2.0f - 1.0f);
}

float3 reconstructViewPosition(float2 uv, float viewDepth) {
    const float2 ndc = uv * 2.0f - 1.0f;
    const float4 clipPoint = float4(ndc, 1.0f, 1.0f);
    float4 viewRay = mul(g_Ssao.inverseProjection, clipPoint);
    viewRay.xyz *= abs(viewRay.w) > 0.00001f ? rcp(viewRay.w) : 1.0f;
    const float rayDepth = -viewRay.z;
    if (rayDepth <= 0.00001f || viewDepth <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    return viewRay.xyz * (viewDepth / rayDepth);
}

float2 projectViewToUv(float3 viewPosition, out bool valid) {
    valid = false;
    const float4 clip = mul(g_Ssao.projection, float4(viewPosition, 1.0f));
    if (clip.w <= 0.00001f) {
        return float2(0.0f, 0.0f);
    }

    const float3 ndc = clip.xyz / clip.w;
    const float2 uv = ndc.xy * 0.5f + 0.5f;
    valid = uv.x >= 0.0f && uv.x <= 1.0f &&
            uv.y >= 0.0f && uv.y <= 1.0f &&
            ndc.z >= 0.0f && ndc.z <= 1.0f;
    return uv;
}

float3x3 buildTangentBasis(float3 normal, float2 uv) {
    const float angle = hash21(uv * 4096.0f) * 2.0f * PI;
    const float3 randomVector = normalize(float3(cos(angle), sin(angle), 0.0f));
    float3 tangent = randomVector - normal * dot(randomVector, normal);
    if (dot(tangent, tangent) < 0.0001f) {
        tangent = abs(normal.y) < 0.95f
            ? normalize(cross(float3(0.0f, 1.0f, 0.0f), normal))
            : normalize(cross(float3(1.0f, 0.0f, 0.0f), normal));
    } else {
        tangent = normalize(tangent);
    }

    const float3 bitangent = normalize(cross(normal, tangent));
    return float3x3(tangent, bitangent, normal);
}

float evaluateSsao(float2 uv) {
    const float4 normalDepth = g_Source0.Sample(g_SsaoSampler, uv);
    const float viewDepth = normalDepth.a;
    if (viewDepth <= 0.0f) {
        return 1.0f;
    }

    const float3 normal = decodeViewNormal(normalDepth.rgb);
    const float3 viewPosition = reconstructViewPosition(uv, viewDepth);
    const float3x3 basis = buildTangentBasis(normal, uv);
    const uint sampleCount = (uint)clamp(g_Ssao.parameters1.x, 1.0f, (float)MaxSsaoSamples);
    const float radius = max(g_Ssao.parameters0.x, 0.0001f);
    const float bias = max(g_Ssao.parameters0.z, 0.0f);

    float occlusion = 0.0f;
    [loop]
    for (uint sampleIndex = 0; sampleIndex < MaxSsaoSamples; ++sampleIndex) {
        if (sampleIndex >= sampleCount) {
            break;
        }

        const float sequence = ((float)sampleIndex + 0.5f) / max((float)sampleCount, 1.0f);
        const float angle = (float)sampleIndex * 2.39996323f + hash21(uv * 2048.0f) * 2.0f * PI;
        const float diskRadius = sqrt(sequence);
        const float3 localSample = float3(
            cos(angle) * diskRadius,
            sin(angle) * diskRadius,
            sqrt(saturate(1.0f - sequence)));
        const float scale = lerp(0.1f, 1.0f, sequence * sequence);
        const float3 sampleVector = mul(localSample, basis) * (radius * scale);
        const float3 samplePosition = viewPosition + sampleVector;
        const float sampleDepth = -samplePosition.z;
        if (sampleDepth <= 0.0f) {
            continue;
        }

        bool validSample = false;
        const float2 sampleUv = projectViewToUv(samplePosition, validSample);
        if (!validSample) {
            continue;
        }

        const float sampledDepth = g_Source0.Sample(g_SsaoSampler, sampleUv).a;
        if (sampledDepth <= 0.0f) {
            continue;
        }

        const float rangeWeight = smoothstep(0.0f, 1.0f, radius / max(abs(viewDepth - sampledDepth), 0.0001f));
        const float isOccluded = sampledDepth <= sampleDepth - bias ? 1.0f : 0.0f;
        occlusion += isOccluded * rangeWeight;
    }

    const float rawAo = 1.0f - occlusion / max((float)sampleCount, 1.0f) * g_Ssao.parameters0.y;
    return pow(saturate(rawAo), max(g_Ssao.parameters0.w, 0.0001f));
}

float blurSsao(float2 uv) {
    const int radius = (int)round(clamp(g_Ssao.parameters1.y, 0.0f, 8.0f));
    if (radius <= 0) {
        return g_Source0.Sample(g_SsaoSampler, uv).r;
    }

    float sum = 0.0f;
    float weight = 0.0f;
    [loop]
    for (int y = -radius; y <= radius; ++y) {
        [loop]
        for (int x = -radius; x <= radius; ++x) {
            const float2 sampleUv = uv + float2((float)x, (float)y) * g_Ssao.texelSize.xy;
            sum += g_Source0.Sample(g_SsaoSampler, sampleUv).r;
            weight += 1.0f;
        }
    }

    return sum / max(weight, 1.0f);
}

float4 compositeSsao(float2 uv) {
    const uint debugMode = (uint)round(g_Ssao.parameters1.z);
    const float4 sceneColor = g_Source0.Sample(g_SsaoSampler, uv);
    const float4 aux = g_Source1.Sample(g_SsaoSampler, uv);

    if (debugMode == SsaoDebugNormalDepth) {
        return float4(aux.rgb, 1.0f);
    }

    const float ao = saturate(aux.r);
    if (debugMode == SsaoDebugOcclusion) {
        return float4(ao, ao, ao, 1.0f);
    }

    return float4(sceneColor.rgb * ao, sceneColor.a);
}

float4 main(PSInput input) : SV_Target0 {
    const uint mode = (uint)round(g_Ssao.parameters1.w);
    if (mode == SsaoModeEvaluate) {
        const float ao = evaluateSsao(input.uv);
        return float4(ao, ao, ao, 1.0f);
    }

    if (mode == SsaoModeBlur) {
        const float ao = blurSsao(input.uv);
        return float4(ao, ao, ao, 1.0f);
    }

    return compositeSsao(input.uv);
}

struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct IrradianceUniform {
    uint faceIndex;
    float sampleDelta;
    float padding0;
    float padding1;
};

[[vk::binding(0, 0)]]
ConstantBuffer<IrradianceUniform> g_Irradiance;

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

void buildBasis(float3 normal, out float3 right, out float3 up) {
    const float3 helper = abs(normal.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
    right = normalize(cross(helper, normal));
    up = normalize(cross(normal, right));
}

float4 main(PSInput input) : SV_Target0 {
    const float3 normal = faceUvToDirection(g_Irradiance.faceIndex, saturate(input.uv));
    float3 right;
    float3 up;
    buildBasis(normal, right, up);

    const float sampleDelta = max(g_Irradiance.sampleDelta, 0.005f);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    float sampleCount = 0.0f;

    for (float phi = 0.0f; phi < 2.0f * PI; phi += sampleDelta) {
        for (float theta = 0.0f; theta < 0.5f * PI; theta += sampleDelta) {
            const float sinTheta = sin(theta);
            const float cosTheta = cos(theta);
            const float3 tangentSample =
                float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
            const float3 sampleDirection =
                normalize(tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal);

            irradiance += g_SourceEnvironmentCube.Sample(g_SourceSampler, sampleDirection).rgb *
                          cosTheta * sinTheta;
            sampleCount += 1.0f;
        }
    }

    irradiance = sampleCount > 0.0f ? PI * irradiance / sampleCount : irradiance;
    return float4(irradiance, 1.0f);
}

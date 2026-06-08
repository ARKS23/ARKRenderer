struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL0;
    [[vk::location(1)]] float2 uv0 : TEXCOORD0;
};

[[vk::binding(1, 0)]]
Texture2D<float4> g_BaseColorTexture;

[[vk::binding(2, 0)]]
SamplerState g_BaseColorSampler;

float4 main(PSInput input) : SV_Target0 {
    return g_BaseColorTexture.Sample(g_BaseColorSampler, input.uv0);
}

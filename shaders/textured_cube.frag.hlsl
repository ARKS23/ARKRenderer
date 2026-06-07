struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[[vk::binding(1, 0)]]
Texture2D<float4> g_CubeTexture;

[[vk::binding(2, 0)]]
SamplerState g_CubeSampler;

float4 main(PSInput input) : SV_Target0 {
    return g_CubeTexture.Sample(g_CubeSampler, input.uv);
}

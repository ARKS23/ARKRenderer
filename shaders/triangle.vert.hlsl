struct VSInput {
    [[vk::location(0)]] float2 position : POSITION0;
    [[vk::location(1)]] float3 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}
